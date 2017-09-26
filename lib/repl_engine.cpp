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
#include <algorithm>
#include <boost/bind.hpp>

repl_engine::repl_engine(boost::mpi::communicator *comm, unsigned int r, size_t ps) : 
    mpi_comm_world(comm), rep(r), page_size(ps), load_info(comm->size()), send_neighbors(rep, 0),
    recv_neighbors(rep, 0), send_sum(comm->size(), 0), shuffle_index(comm->size()), offsets(rep, 0)  { 
    for (int i = 0; i < comm->size(); i++)
	load_info[i] = std::vector<unsigned int>(rep, 0);
}

bool repl_engine::compare_traffic(unsigned int i, unsigned int j) { 
    return send_sum[i] > send_sum[j];
}

// shuffle ranks to interleave large senders with small senders
unsigned int repl_engine::shuffle_ranks() {
    unsigned int n = (unsigned int)mpi_comm_world->size(), i;
    for (i = 0; i < n; i++)
	for (unsigned int j = 1; j < rep; j++)
	    send_sum[i] += load_info[i][j];
    std::sort(shuffle_index.begin(), shuffle_index.end(), boost::bind(&repl_engine::compare_traffic, this, _1, _2));
    shuffle_index.swap(send_sum);
    unsigned int head = 0, tail = n - 1;
    i = 0;
    while (i < n) {
	shuffle_index[i++] = send_sum[head++];
	for (unsigned int j = 1; j < rep && head < tail; j++)
	    shuffle_index[i++] = send_sum[tail--];
    }
    for (i = 0; i < n; i++) 
	if (shuffle_index[i] == (unsigned int)mpi_comm_world->rank())
	    break;
    return i;
}

// compute who and what will be sent/received to/from this rank, adjusting offsets accordingly.
size_t repl_engine::compute_neighbors() {
    size_t recv_pages = 0, send_pages = 0;
    unsigned int rank;

    recv_neighbors[0] = send_neighbors[0] = mpi_comm_world->rank();
    offsets.assign(rep, 0);
    for (unsigned int i = 0; i < (unsigned int)mpi_comm_world->size(); i++)
	shuffle_index[i] = i;
    // my_shuffled_rank = mpi_comm_world->rank();
    unsigned int my_shuffled_rank = shuffle_ranks();
    for (unsigned int i = 1; i < rep; i++) {
	rank = my_shuffled_rank;
	while (i * RANK_DISTANCE > rank)
	    rank += mpi_comm_world->size();
	rank -= i * RANK_DISTANCE;	
	recv_neighbors[i] = shuffle_index[rank];
	recv_pages += load_info[recv_neighbors[i]][i];
	rank = (my_shuffled_rank + i * RANK_DISTANCE) % mpi_comm_world->size();
	send_neighbors[i] = shuffle_index[rank];
	send_pages += load_info[send_neighbors[0]][i];
	for (unsigned int j = i + 1; j < rep; j++)
	    offsets[j] += load_info[send_neighbors[i]][j - i];
    }
    
    DBG("replication load statistics: send = " << send_pages << ", recv = " << recv_pages);

    return recv_pages;
}

void repl_engine::init(unsigned int local_load, std::string &ckpt_path_prefix, int seq_no) {
    std::vector<unsigned int> load(rep, local_load);
    init(NULL, load, ckpt_path_prefix, seq_no);
}

void repl_engine::init(dedup_engine::page_ptr_map_t *pi, std::vector<unsigned int> &load, std::string &ckpt_path_prefix, int seq_no) {
    page_info = pi;
    //size_t fr_offset = 0;

    std::stringstream ss;
    ss << ckpt_path_prefix << "/blobcr-ckpt-" << mpi_comm_world->rank() << "-" << seq_no << ".dat";
    std::string local_name = ss.str();
    fd = open(local_name.c_str(), O_RDWR | O_TRUNC | O_CREAT, 0666);
    ASSERT(fd != -1);

    boost::mpi::all_gather(*mpi_comm_world, load, load_info);

    // compute neighbors and how much data needs to be received from each 
    recv_size = compute_neighbors() * page_size;

    // create replication file and reserve space
    if (recv_size > 0) {
	ss.str(std::string());
	ss << ckpt_path_prefix << "/blobcr-repl-" << mpi_comm_world->rank() << "-" << seq_no << ".dat";
	local_name = ss.str();    
	fr = open(local_name.c_str(), O_RDWR | O_TRUNC | O_CREAT, 0666);
	ASSERT(fr != -1);
	ASSERT(ftruncate(fr, recv_size) == 0);
	fr_addr = (char *)mmap(NULL, recv_size, PROT_READ | PROT_WRITE, MAP_SHARED, fr, 0);
	ASSERT(fr_addr != MAP_FAILED);
	
	// add all corresponding recv requests
	/*
	for (unsigned int i = 1; i < rep; i++)
	    for (unsigned int j = 0; j < load_info[neighbors[i]][i]; j++) {
		requests.push_back(mpi_comm_world->irecv(neighbors[i], REPL_TAG, fr_addr + fr_offset, page_size));
		fr_offset += page_size;
	    }
	*/
	MPI_Win_create(fr_addr, recv_size, 1, MPI_INFO_NULL, *mpi_comm_world, &win);
	MPI_Win_fence(0, win);
    } else
	fr_addr = (char *)MAP_FAILED;
    //mpi_comm_world->barrier();
}

void repl_engine::add_send_request(char *addr, char *buff) {
    unsigned int copies = rep - 1;

    if (page_info != NULL) {
	auto it = page_info->find(addr);
	if (it != page_info->end() && it->second != NULL)
	    copies = rep - it->second->size();
    }

    for (unsigned int i = 1; i <= copies; i++) {
	MPI_Put(buff, page_size, MPI_CHAR, send_neighbors[i], offsets[i], page_size, MPI_CHAR, win);
	offsets[i] += page_size;
	//requests.push_back(mpi_comm_world->isend(send_to, REPL_TAG, buff, page_size));
	//mpi_comm_world->send(send_to, REPL_TAG, buff, page_size);
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
    if (fr_addr != MAP_FAILED)
	add_send_request(addr, buff);
}

void repl_engine::finalize() {
    fdatasync(fd);
    close(fd);
    /*
    boost::mpi::wait_all(requests.begin(), requests.end());
    requests.clear();
    */
    if (fr_addr != MAP_FAILED) {
	MPI_Win_fence(0, win);
	MPI_Win_free(&win);
	munmap(fr_addr, recv_size);
	fdatasync(fr);
	close(fr);
    }

}

repl_engine::~repl_engine() {
}
