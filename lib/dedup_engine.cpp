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

#include "dedup_engine.hpp"

#include <cstring>
#include <algorithm>
#include <list>

#include <openssl/sha.h>
#include <boost/serialization/list.hpp>
#include <boost/serialization/boost_unordered_set.hpp>
#include <boost/bind.hpp>

#define __DEBUG
#include "common/debug.hpp"

#define HASH_FCN   SHA1
#define HASH_SIZE  20

// how many top-k pages to keep
static const unsigned int THRESHOLD = 1 << 17;

class page_hashes_entry_t {
public:
    char hash[HASH_SIZE];
    mutable char *page_ptr;
    mutable unsigned int count;
    mutable rank_list_t ranks;

    page_hashes_entry_t(char *buff, unsigned int r) : count(1) {
	HASH_FCN((unsigned char *)buff, simple_sweep_allocator::get_page_size(), (unsigned char *)hash);
	page_ptr = buff;
	ranks.push_back(r);
    }
    page_hashes_entry_t() : page_ptr(NULL), count(0) { }
    bool operator==(page_hashes_entry_t const& other) const {
	return memcmp(hash, other.hash, HASH_SIZE) == 0;
    }
    bool operator<(page_hashes_entry_t const& other) const {
	return count >= other.count;
    }
    friend size_t hash_value(const page_hashes_entry_t &entry) {
	size_t result;
	memcpy(&result, entry.hash, sizeof(size_t));
	return result;
    }
private:    
    friend class boost::serialization::access;
    template <class Archive> void serialize(Archive &ar, unsigned int /*version*/) {
	for (unsigned int i = 0; i < HASH_SIZE; i++)
	    ar & hash[i];
	ar & count;
	ar & ranks;
    }
};

class hash_merger_t : public std::binary_function <page_hashes_t, page_hashes_t, page_hashes_t> {
private:    
    struct compare_count {
	bool operator() (const page_hashes_entry_t *x, const page_hashes_entry_t *y) const {
	    if (x->count == y->count)
		return x > y;
	    return x->count > y->count;
	}
    };
    typedef std::set<const page_hashes_entry_t *, compare_count,
		     boost::fast_pool_allocator<const page_hashes_entry_t *, no_reclaim_allocator> > ordered_hashes_t;
    
    std::vector<unsigned int, boost::fast_pool_allocator<unsigned int, no_reclaim_allocator> > page_load;    
    unsigned int rep;
    bool compare_load(unsigned int a, unsigned int b) {
	return page_load[a] < page_load[b];
    }

public:
    hash_merger_t(unsigned int size, unsigned int r) : page_load(size), rep(r) { }
    page_hashes_t &operator()(page_hashes_t &x, page_hashes_t &y) {
	ordered_hashes_t uncut_result;
	for (unsigned int i = 0; i < page_load.size(); i++)
	    page_load[i] = 0;
	for (page_hashes_t::iterator xi = x.begin(); xi != x.end(); ++xi)
	    if (y.find(*xi) == y.end()) {	
		for (auto xr = xi->ranks.begin(); xr != xi->ranks.end(); ++xr)
		    page_load[*xr]++;
		uncut_result.insert(&(*xi));
	    }
	for (page_hashes_t::iterator yi = y.begin(); yi != y.end(); ++yi)
	    if (x.find(*yi) == x.end()) {
		for (auto yr = yi->ranks.begin(); yr != yi->ranks.end(); ++yr)
		    page_load[*yr]++;
		uncut_result.insert(&(*yi));
	    }
	for (page_hashes_t::iterator yi = y.begin(); yi != y.end(); ++yi) {
	    page_hashes_t::iterator xi = x.find(*yi);
	    if (xi != x.end()) {
		yi->count += xi->count;
		yi->ranks.merge(xi->ranks, boost::bind(&hash_merger_t::compare_load, this, _1, _2));
		while (yi->ranks.size() > rep)
		    yi->ranks.pop_back();
		for (auto yr = yi->ranks.begin(); yr != yi->ranks.end(); ++yr)
		    page_load[*yr]++;
		uncut_result.insert(&(*yi));
	    }
	}
	page_hashes_t result;
	for (ordered_hashes_t::iterator i = uncut_result.begin(); i != uncut_result.end() && result.size() < THRESHOLD; ++i)
	    result.insert(**i);
	x.swap(result);
	return x;    
    }
};

class stats_merger_t : public std::binary_function <stats_t, stats_t, stats_t> {
public:
    stats_t operator()(stats_t &x, stats_t &y) {
	return stats_t(x.local + y.local, x.global + y.global, x.total + y.total);
    }
};

// mark collectives as commutative for increased efficiency
namespace boost { 
    namespace mpi {
	template<> struct is_commutative<hash_merger_t, page_hashes_t> : mpl::true_ { };
	template<> struct is_commutative<stats_merger_t, stats_t> : mpl::true_ { };
	template<> struct is_commutative<std::plus<unsigned int>, unsigned int> : mpl::true_ { };
    } 
}

dedup_engine::dedup_engine(boost::mpi::communicator *comm, unsigned int r) : 
    stats(0, 0, 0), mpi_comm_world(comm), rep(r), load(r, 0) { }

dedup_engine::~dedup_engine() {
}

void dedup_engine::clear() {
    page_ptr_map.clear();
    page_hashes.clear();
    stats.total = 0;
}

void dedup_engine::process_page(char *buff) {
    auto ret = page_hashes.insert(page_hashes_entry_t(buff, mpi_comm_world->rank()));
    if (ret.second)
	page_ptr_map[buff] = NULL;
    stats.total++;
}

bool dedup_engine::check_page(char *buff) {
    return page_ptr_map.find(buff) != page_ptr_map.end();    
}

void dedup_engine::finalize_local() {
    load.assign(rep, page_hashes.size());
    stats.local = page_hashes.size() * rep;
}

void dedup_engine::global_dedup() {
    page_hashes_t merge_result = page_hashes;
    merge_result = boost::mpi::all_reduce(*mpi_comm_world, merge_result, hash_merger_t(mpi_comm_world->size(), rep));
    load.assign(rep, 0);
    for (auto pi = page_hashes.begin(); pi != page_hashes.end();) {
	auto mi = merge_result.find(*pi);
	if (mi != merge_result.end()) {
	    if (std::find(mi->ranks.begin(), mi->ranks.end(), mpi_comm_world->rank()) == mi->ranks.end()) {
		page_ptr_map.erase(pi->page_ptr);
		pi = page_hashes.erase(pi);
	    } else {
		pi->ranks = mi->ranks;
		page_ptr_map[pi->page_ptr] = &pi->ranks;
		for (unsigned int i = 0; i <= rep - pi->ranks.size(); i++)
		    load[i]++;
		pi++;
	    }
	} else {
	    for (unsigned int i = 0; i < rep; i++)
		load[i]++;
	    pi++;
	}
    }
    stats.global = load[0];
    if (mpi_comm_world->rank() == 0) {
	std::vector<unsigned int, boost::fast_pool_allocator<unsigned int, no_reclaim_allocator> > hash_count(mpi_comm_world->size(), 0);
	for (auto mi = merge_result.begin(); mi != merge_result.end(); mi++)
	    hash_count[mi->count - 1]++;
	for (int i = 0; i < mpi_comm_world->size(); i++)
	    DBG(hash_count[i] << " hashes have frequency of appearance " << i + 1);
    }
}

std::string dedup_engine::get_stats() {
    stats_t out;
    boost::mpi::reduce(*mpi_comm_world, stats, out, stats_merger_t(), 0);
    if (mpi_comm_world->rank() == 0) {
	std::ostringstream ss;
	ss << "local = " << out.local << "/" << out.total << ", global = " << out.global << "/" << out.total;
	return  ss.str();
    } else
	return "";
}

std::vector<unsigned int> &dedup_engine::get_load_info() {
    return load;
}

dedup_engine::page_ptr_map_t *dedup_engine::get_page_info() {
    return &page_ptr_map;
}
