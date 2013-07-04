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

#include "cow_allocator.hpp"

extern "C" {
#include <sys/mman.h>
}

char *no_reclaim_allocator::region;
size_t no_reclaim_allocator::current_size, no_reclaim_allocator::max_size;
boost::mutex no_reclaim_allocator::alloc_lock;

char *simple_sweep_allocator::region, *simple_sweep_allocator::alloc_bitmap;
size_t simple_sweep_allocator::page_size, simple_sweep_allocator::max_size;
boost::mutex simple_sweep_allocator::alloc_lock;

void no_reclaim_allocator::init(size_type ms) {
    max_size = ms;
    current_size = 0;
    region = (char *)mmap(NULL, max_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
}

void simple_sweep_allocator::init(size_type ps, size_type em) {
    max_size = em;
    page_size = ps;
    region = (char *)mmap(NULL, em, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    alloc_bitmap = (char *)mmap(NULL, em / ps, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    memset(alloc_bitmap, 0, em / ps);
}

void no_reclaim_allocator::destroy() {
    munmap(region, max_size);    
}

void simple_sweep_allocator::destroy() {
    munmap(region, max_size);
    munmap(alloc_bitmap, max_size / page_size);
}

char *no_reclaim_allocator::malloc(const size_type size) {
    char *result = NULL;
    boost::mutex::scoped_lock(alloc_lock);
    if (current_size + size <= max_size) {
	result = region + current_size;
	current_size += size;
    }
    if (result == NULL)
	throw std::bad_alloc();
    return result;
}

void no_reclaim_allocator::free(char *const addr) {
    DBG("this is not implemented!");
}

char *simple_sweep_allocator::malloc(const size_type size) { 
    boost::mutex::scoped_lock(alloc_lock);
    unsigned int index = 0;
    while (alloc_bitmap[index] != 0)
	index++;
    if (index == max_size / page_size)
	return NULL;
    alloc_bitmap[index] = 1;
    return region + index * page_size;
}

size_t simple_sweep_allocator::get_page_size() {
    return simple_sweep_allocator::page_size;
}

void simple_sweep_allocator::free(char *const addr) {
    boost::mutex::scoped_lock(alloc_lock);
    unsigned long index = ((unsigned long)addr - (unsigned long)region) / page_size;
    if (index < max_size / page_size)
	alloc_bitmap[index] = 0;
}
