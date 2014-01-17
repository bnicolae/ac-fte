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

#ifndef __REPL_ENGINE
#define __REPL_ENGINE

#include <boost/mpi.hpp>

#include "dedup_engine.hpp"

#define __DEBUG
#include "common/debug.hpp"

class repl_engine {
private:
    typedef std::vector<boost::mpi::request, boost::fast_pool_allocator<boost::mpi::request, no_reclaim_allocator> > vrequest_t;
    
    static const unsigned int RANK_DISTANCE = 1, REPL_TAG=0x0FFFAAAA;

    boost::mpi::communicator *mpi_comm_world;
    dedup_engine::page_ptr_map_t *page_info;
    unsigned int rep;
    size_t recv_size, page_size;
    std::vector<std::vector<unsigned int> > load_info;
    std::vector<unsigned int> neighbors;
    vrequest_t requests;
    int fd, fr;
    char *fr_addr;

    size_t compute_neighbors();
    void add_send_request(char *addr, char *buff);

public:
    repl_engine(boost::mpi::communicator *comm, unsigned int rep, size_t ps);
    ~repl_engine();

    void init(unsigned int local_load, std::string &ckpt_path_prefix, int seq_no);
    void init(dedup_engine::page_ptr_map_t *pi, std::vector<unsigned int> &load, std::string &ckpt_path_prefix, int seq_no);
    void finalize();
    void write_page(char *addr, char *buff);
};

#endif
