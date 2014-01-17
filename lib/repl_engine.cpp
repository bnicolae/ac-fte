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

#include "repl_engine.hpp"

#include <fcntl.h>
#include <sys/mman.h>

repl_engine::repl_engine(boost::mpi::communicator *comm, unsigned int r, size_t ps) : 
    mpi_comm_world(comm), rep(r), page_size(ps), load_info(comm->size()), neighbors(rep, 0) { }

size_t repl_engine::compute_neighbors() {
    size_t recv_pages = 0;
    unsigned int receive_from;

    neighbors[0] = mpi_comm_world->rank();    
    for (unsigned int i = 1; i < rep; i++) {
	receive_from = mpi_comm_world->rank();
	while (i * RANK_DISTANCE > receive_from)
	    receive_from += mpi_comm_world->size();
	receive_from -= i * RANK_DISTANCE;	
	neighbors[i] = receive_from;
	recv_pages += load_info[receive_from][i];
    }

    DBG("replication load statistics: " << load_info[neighbors[0]][0] << " + " << recv_pages);

    return recv_pages;
}

void repl_engine::init(unsigned int local_load, std::string &ckpt_path_prefix, int seq_no) {
    std::vector<unsigned int> load(rep, local_load);
    init(NULL, load, ckpt_path_prefix, seq_no);
}

void repl_engine::init(dedup_engine::page_ptr_map_t *pi, std::vector<unsigned int> &load, std::string &ckpt_path_prefix, int seq_no) {
    page_info = pi;
    size_t fr_offset = 0;

    std::stringstream ss;
    ss << ckpt_path_prefix << "/blobcr-ckpt-" << mpi_comm_world->rank() << "-" << seq_no << ".dat";
    std::string local_name = ss.str();
    fd = open(local_name.c_str(), O_RDWR | O_TRUNC | O_CREAT, 0666);
    ASSERT(fd != -1);

    boost::mpi::all_gather(*mpi_comm_world, load, load_info);

    // compute neighbors and how much data needs to be received from each 
    recv_size = compute_neighbors() * page_size;

    // create replication file and reserve space
    ss.str(std::string());
    ss << ckpt_path_prefix << "/blobcr-repl-" << mpi_comm_world->rank() << "-" << seq_no << ".dat";
    local_name = ss.str();    
    fr = open(local_name.c_str(), O_RDWR | O_TRUNC | O_CREAT, 0666);
    ASSERT(fr != -1);
    ftruncate(fr, recv_size);
    fr_addr = (char *)mmap(NULL, recv_size, PROT_READ | PROT_WRITE, MAP_SHARED, fr, 0);
    ASSERT(fr_addr != MAP_FAILED);

    // add all corresponding recv requests
    for (unsigned int i = 1; i < rep; i++)
	for (unsigned int j = 0; j < load_info[neighbors[i]][i]; j++) {
	    requests.push_back(mpi_comm_world->irecv(neighbors[i], REPL_TAG, fr_addr + fr_offset, page_size));
	    fr_offset += page_size;
	}
    mpi_comm_world->barrier();
}

void repl_engine::add_send_request(char *addr, char *buff) {
    unsigned int copies = rep - 1, send_to;

    if (page_info != NULL) {
	auto it = page_info->find(addr);
	if (it != page_info->end() && it->second != NULL)
	    copies = rep - it->second->size();
    }

    for (unsigned int i = 1; i <= copies; i++) {
	send_to = (mpi_comm_world->rank() + i * RANK_DISTANCE) % mpi_comm_world->size();	
	//requests.push_back(mpi_comm_world->isend(send_to, REPL_TAG, buff, page_size));
	mpi_comm_world->send(send_to, REPL_TAG, buff, page_size);
    }
}

void repl_engine::write_page(char *addr, char *buff) {
    ssize_t result; 
    size_t progress = 0;

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
    add_send_request(addr, buff);
}

void repl_engine::finalize() {
    close(fd);
    
    boost::mpi::wait_all(requests.begin(), requests.end());
    requests.clear();
    munmap(fr_addr, recv_size);
    close(fr);    
}

repl_engine::~repl_engine() {
}
