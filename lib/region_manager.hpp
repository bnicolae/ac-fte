/*******************************************************************************
 Author: Bogdan Nicolae
 Copyright (C) 2012 IBM Corp.

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

#ifndef __REGION_MANAGER
#define __REGION_MANAGER

#include <fstream>

#include <boost/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/condition.hpp>
#include <boost/unordered_map.hpp>
#include <boost/pool/pool_alloc.hpp>
#include <boost/mpi.hpp>

#include "cow_allocator.hpp"
#include "dedup_engine.hpp"

class region_manager {
public:
    // Where to store access order
    typedef std::pair<char *, char> touched_entry_t;    
    typedef std::vector<touched_entry_t, 
			boost::pool_allocator<touched_entry_t, no_reclaim_allocator>
			> touched_t;
private:
    // Page state
    static const char PAGE_SCHEDULED = 1, PAGE_INPROGRESS = 2, PAGE_COMMITTED = 3;
    // Page access type
    static const char PAGE_WAIT = 1, PAGE_COW = 2, PAGE_AFTER = 3, PAGE_DELAYED = 4;
    
    boost::uint64_t page_size;
    std::string ckpt_path_prefix;
    boost::uint64_t cow_threshold;
    bool incremental_flag, access_order_flag, dedup_flag, global_dedup_flag;
    
    touched_t touched, new_touched;
    
    struct page_info_t {
	char *cow_ptr;
	
	char state;
	page_info_t() : 
	    cow_ptr(NULL), state(PAGE_COMMITTED) { }
    };

    typedef std::pair<char *, page_info_t> page_entry_t;
    typedef boost::unordered_map<char *, page_info_t, 
				 boost::hash<char *>, std::equal_to<char *>, 			  
				 boost::fast_pool_allocator<page_entry_t, no_reclaim_allocator>
				 > page_map_t;
    page_map_t pages;

    boost::uint64_t total_mem_size;
    unsigned int no_blocks, seq_no;
    unsigned stats_page_cow, stats_page_wait, stats_page_after, stats_page_delayed;
    bool checkpoint_in_progress;

    boost::mutex page_lock, work_lock;
    boost::condition_variable work_cond, page_cond;
    boost::thread async_io_thread;

    boost::mpi::environment mpi_env;
    boost::mpi::communicator mpi_comm_world;
    dedup_engine *dup_engine;
    std::ofstream ckpt_log_file;

    void async_io_exec();
    std::string construct_stats();
    void handle_page(char *addr, int fd);
    
public:
    region_manager(boost::uint64_t page_size, std::string &ckpt_path_prefix, std::string &ckpt_log_prefix,
		   boost::uint64_t cow_mem, bool inc_flag, bool aorder_flag, bool dup_flag, bool global_dup_flag);
    ~region_manager();

    bool add_region(const void *buff, boost::uint64_t size);
    boost::uint64_t remove_region(const void *buff, 
				  boost::uint64_t size = 0);
    bool checkpoint();
    void wait_for_completion();
    bool handle_segfault(void *addr);
    void display_stats();
};

#endif
