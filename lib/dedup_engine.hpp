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

#ifndef __DEDUP_ENGINE
#define __DEDUP_ENGINE

#include <tr1/unordered_set>

#include <boost/unordered_map.hpp>
#include <boost/functional/hash.hpp>
#include <boost/pool/pool_alloc.hpp>
#include <boost/mpi.hpp>

#include "cow_allocator.hpp"

class page_hashes_entry_t;
typedef std::tr1::unordered_set<page_hashes_entry_t,
				boost::hash<page_hashes_entry_t>, std::equal_to<page_hashes_entry_t>,
				boost::fast_pool_allocator<page_hashes_entry_t, no_reclaim_allocator>
				> page_hashes_t;

class stats_t {
public:
    unsigned int local, global, total;
    stats_t(unsigned int l, unsigned int g, unsigned int t) : local(l), global(g), total(t) { }
    stats_t() : local(0), global(0), total(0) { }
private:
    friend class boost::serialization::access;
    template <class Archive> void serialize(Archive &ar, unsigned int /*version*/) {
    	ar & local & global & total;
    }
};

class dedup_engine {    
private:
    typedef std::pair<char *, bool> page_ptr_map_entry_t;
    typedef boost::unordered_map<char *, bool,
				 boost::hash<char *>, std::equal_to<char *>,
				 boost::fast_pool_allocator<page_ptr_map_entry_t, no_reclaim_allocator>
			       > page_ptr_map_t;
			      
    page_hashes_t page_hashes;
    page_ptr_map_t page_ptr_map;

    stats_t stats;
    boost::mpi::communicator *mpi_comm_world;
   
public:
    dedup_engine(boost::mpi::communicator *comm);
    ~dedup_engine();
    void process_page(char *buff);
    bool check_page(char *buff);
    void global_dedup();
    void clear();

    void finalize_local();
    std::string get_stats();
};

#endif
