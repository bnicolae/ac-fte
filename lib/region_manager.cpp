/*******************************************************************************
 Author: Bogdan Nicolae
 Copyright (C) 2013 IBM Corp.

 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
*******************************************************************************/

#include "region_manager.hpp"

#include <cstdlib>
#include <algorithm>

extern "C" {
#include <fcntl.h>
#include <sys/mman.h>
}

#define __DEBUG
#include "common/debug.hpp"

#define NO_RECLAIM_SIZE (1 << 29)

region_manager::region_manager(boost::uint64_t ps, std::string &cp, std::string &cl,
			       boost::uint64_t extra_mem, bool iflag, 
			       bool aflag, bool dflag, bool gdflag) :
    page_size(ps), ckpt_path_prefix(cp), cow_threshold(extra_mem / page_size),
    incremental_flag(iflag), access_order_flag(aflag), dedup_flag(dflag),
    global_dedup_flag(gdflag), total_mem_size(0), no_blocks(0), seq_no(0),
    stats_page_cow(0), stats_page_wait(0), stats_page_after(0), stats_page_delayed(0),
    checkpoint_in_progress(false), async_io_thread(boost::bind(&region_manager::async_io_exec, this))  {    
    no_reclaim_allocator::init(NO_RECLAIM_SIZE);
    simple_sweep_allocator::init(page_size, extra_mem);
    dup_engine = new dedup_engine(&mpi_comm_world);
    if (cl != "") {
	std::ostringstream ss;
	ss << cl << "/ckpt_messages-rank_" << mpi_comm_world.rank() << ".log";
	ckpt_log_file.clear();
	ckpt_log_file.open(ss.str());
	if (ckpt_log_file.good()) {
	    std::clog.copyfmt(ckpt_log_file);
	    std::clog.clear(ckpt_log_file.rdstate());
	    std::clog.rdbuf(ckpt_log_file.rdbuf());	    
	}
    }
}

region_manager::~region_manager() {
    async_io_thread.interrupt();
    async_io_thread.join(); 

    while (pages.size() > 0) {
	page_map_t::iterator p_it = pages.begin();
	mprotect(p_it->first, page_size, PROT_READ | PROT_WRITE);
	pages.erase(p_it);
    }
    delete dup_engine;
    no_reclaim_allocator::destroy();
    simple_sweep_allocator::destroy();
}

bool region_manager::add_region(const void *buff, boost::uint64_t size) {
    boost::mutex::scoped_lock lock(page_lock);
    //safe_printf("!!!REGION_ADD!!! add: %p %Lu %lu\n", buff, size, pthread_self());
    char *addr;

    for (addr = (char *)buff; addr < (char *)buff + size; addr += page_size) {
	pages.insert(page_entry_t(addr, page_info_t()));
	total_mem_size += page_size;
    }
    if (incremental_flag)
	mprotect((void *)buff, size, PROT_READ);

    return (addr < (char *)buff + size);
}

boost::uint64_t region_manager::remove_region(const void *buff, 
					      boost::uint64_t size) {
    //safe_printf("!!!REGION_REMOVE!!!: remove %p %Lu %lu\n", buff, size, pthread_self());
    for (char *addr = (char *)buff; addr < (char *)buff + size; addr += page_size)  {
	page_map_t::iterator p_it = pages.find(addr);
	if (p_it == pages.end())
	    continue;
	{
	    boost::mutex::scoped_lock lock(page_lock);
	    while (p_it->second.state != PAGE_COMMITTED)
		page_cond.wait(lock);
	    pages.erase(p_it);
	}
	total_mem_size -= page_size;
    }
    mprotect((void *)buff, size, PROT_READ | PROT_WRITE);
    return size;
}

bool region_manager::handle_segfault(void *addr) {
    char *buff = (char *)(((unsigned long)addr / page_size) * page_size);

    page_map_t::iterator p_it = pages.find(buff);
    if (p_it == pages.end()) {
	DBG("SIGSEGV trapped outside of protected regions (" << (unsigned long)buff << 
	    "), aborting...");
	return false;	
    }

    char access_type;
    if (p_it->second.state != PAGE_COMMITTED) {
	boost::mutex::scoped_lock lock(page_lock);
	if (p_it->second.state == PAGE_SCHEDULED && stats_page_cow < cow_threshold) {
	    char *new_page = simple_sweep_allocator::malloc(page_size);
	    ASSERT(new_page != NULL);
	    memcpy(new_page, buff, page_size);
	    p_it->second.cow_ptr = new_page;
	    access_type = PAGE_COW;
	    stats_page_cow++;
	} else if (p_it->second.state == PAGE_COMMITTED) {
	    if (checkpoint_in_progress) {
		access_type = PAGE_AFTER;
		stats_page_after++;
	    } else {
		access_type = PAGE_DELAYED;
		stats_page_delayed++;
	    }
	} else {
	    while (p_it->second.state != PAGE_COMMITTED)
		page_cond.wait(lock);
	    access_type = PAGE_WAIT;
	    stats_page_wait++;
	}
    } else {
	if (checkpoint_in_progress) {
	    access_type = PAGE_AFTER;
	    stats_page_after++;
	} else {
	    access_type = PAGE_DELAYED;
	    stats_page_delayed++;
	}
    }

    if (incremental_flag || access_type != PAGE_WAIT)
	mprotect(buff, page_size, PROT_READ | PROT_WRITE);
/*
    std::clog << "touched_size = " << new_touched.size() << ", ptr = " 
    << (unsigned long)buff << ", access_type = " << (int)access_type << std::endl;*/
    new_touched.push_back(touched_entry_t(buff, access_type));    

    return true;
}

void region_manager::wait_for_completion() {
    boost::mutex::scoped_lock lock(work_lock);
    while (checkpoint_in_progress)
	work_cond.wait(lock);
}

bool region_manager::checkpoint() {
    // first wait for the previous checkpoint to complete (if necessary)
    wait_for_completion();

    INFO("CHECKPOINT STARTED - " << construct_stats());

    // reset statistics
    stats_page_cow = stats_page_wait = stats_page_after = stats_page_delayed = 0;
    touched = new_touched;
    new_touched.clear();

    // de-duplication
    if (dedup_flag) {
	dup_engine->clear();
	if (incremental_flag)
	    for (touched_t::iterator t_it = touched.begin(); t_it != touched.end(); t_it++) {
		page_map_t::iterator p_it = pages.find(t_it->first);
		if (p_it != pages.end())
		    dup_engine->process_page(p_it->first);
	    }
	else
	    for (page_map_t::iterator p_it = pages.begin(); p_it != pages.end(); p_it++)
		dup_engine->process_page(p_it->first);
	dup_engine->finalize_local();
	if (global_dedup_flag)
	    dup_engine->global_dedup();
	// optionally display some stats:

	std::string dup_stats = dup_engine->get_stats();
	if (dup_stats != "")
	    DBG("DEDUP statistics: " << dup_stats);
    }

    // protect all memory regions
    for (page_map_t::iterator p_it = pages.begin(); p_it != pages.end(); p_it++)
	mprotect(p_it->first, page_size, PROT_READ);

    // schedule pages for eviction
    if (incremental_flag)
	for (touched_t::iterator t_it = touched.begin(); t_it != touched.end(); t_it++) {
	    page_map_t::iterator p_it = pages.find(t_it->first);
	    if (p_it != pages.end() && (!dedup_flag || dup_engine->check_page(p_it->first)))
		p_it->second.state = PAGE_SCHEDULED;
	}
    else
	for (page_map_t::iterator p_it = pages.begin(); p_it != pages.end(); p_it++)
	    if (!dedup_flag || dup_engine->check_page(p_it->first))
		p_it->second.state = PAGE_SCHEDULED;

    // signal the io thread to begin processing
    no_blocks = 0;
    checkpoint_in_progress = true;
    work_cond.notify_one();
    
    // optionally block until checkpointing is complete
    boost::this_thread::yield();
    wait_for_completion();
    
    return true;
}

std::string region_manager::construct_stats() {
    std::stringstream ss;

    ss << "rank = " << mpi_comm_world.rank() << 
	", total_tracked = " << (total_mem_size / (1 << 20)) << "MB" <<
	", seq_no = " << seq_no <<
	", pages_cow = " << stats_page_cow << 
	", pages_wait = " << stats_page_wait <<
	", pages_after = " << stats_page_after <<
	", pages_delayed = " << stats_page_delayed <<
	", committed_pages = " << no_blocks;
    
    return ss.str();
}
    
void region_manager::display_stats() {
    INFO("STATS SINCE LAST CKPT - " << construct_stats());
}

void region_manager::handle_page(char *addr, int fd) {
    char *buff;
    page_map_t::iterator p_it;

    {
	boost::mutex::scoped_lock lock(page_lock);

	p_it = pages.find(addr);
	if (p_it == pages.end() || p_it->second.state != PAGE_SCHEDULED)
	    return;
	p_it->second.state = PAGE_INPROGRESS;
	if (p_it->second.cow_ptr != NULL)
	    buff = p_it->second.cow_ptr;
	else
	    buff = addr;
    }
    ssize_t result; size_t progress = 0;
    while (progress < page_size) {
	result = write(fd, buff + progress, page_size - progress);
	if (result == -1) {
	    char msg[1024];
	    sprintf(msg, "handle page %p", addr);
	    perror(msg);
	}
	ASSERT(result != -1);
	progress += result;
    }
    {
	boost::mutex::scoped_lock lock(page_lock);
	p_it->second.state = PAGE_COMMITTED;
	p_it->second.cow_ptr = NULL;
	page_cond.notify_one();
    }
    if (buff != addr)
	simple_sweep_allocator::free(buff);
    else if (!incremental_flag)
	mprotect(buff, page_size, PROT_READ | PROT_WRITE);
    no_blocks++;
}

static bool no_order_comparator(const region_manager::touched_entry_t &e1, 
				const region_manager::touched_entry_t &e2) {
    return e1.first < e2.first;
}

static bool order_comparator(const region_manager::touched_entry_t &e1, 
			     const region_manager::touched_entry_t &e2) {
    if (e1.second < e2.second)
	return true;
    if (e1.second > e2.second)
	return false;
    return e1.first < e2.first;
}

void region_manager::async_io_exec() {
    std::stringstream ss;
    std::string local_name;
    int fd;

    while (1) {
	{
	    // wait for checkpoiniting signal
	    boost::mutex::scoped_lock lock(work_lock);
	    while (!checkpoint_in_progress)
		work_cond.wait(lock);
	}
	
	if (incremental_flag) {
	    if (access_order_flag) 
		std::sort(touched.begin(), touched.end(), &order_comparator);
	    else 
		std::sort(touched.begin(), touched.end(), &no_order_comparator);
	}
		    
	// now write the checkpointing data
	ss.str("");
	ss << ckpt_path_prefix << "/blobcr-ckpt-" << mpi_comm_world.rank() << "-" << seq_no << ".dat";
	local_name = ss.str();

	fd = open(local_name.c_str(), O_RDWR | O_TRUNC | O_CREAT, 0666);
	ASSERT(fd != -1);

	if (incremental_flag || access_order_flag)
	    for (int i = touched.size() - 1; i >= 0; i--) {
		boost::this_thread::interruption_point();
		handle_page(touched[i].first, fd);
	    }
	if (!incremental_flag)
	    for (page_map_t::iterator p_it = pages.begin(); p_it != pages.end(); p_it++) {
		boost::this_thread::interruption_point();
		handle_page(p_it->first, fd);
	    }
		
	close(fd);
	INFO("CHECKPOINT COMPLETE - " << construct_stats());
	seq_no++;
	checkpoint_in_progress = false;
	work_cond.notify_all(); 
    }
}
