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

#ifndef __COW_ALLOCATOR
#define __COW_ALLOCATOR

#include <cstdlib>
#include <boost/thread/mutex.hpp>

//#define __DEBUG
#include "common/debug.hpp"

struct no_reclaim_allocator {
    typedef std::size_t size_type;
    typedef std::ptrdiff_t difference_type;

    static char *region;
    static size_t current_size, max_size;
    static boost::mutex alloc_lock;

    static void init(size_type max_size);
    static void destroy();
    static char *malloc(const size_type size);
    static void free(char *const addr);
};

struct simple_sweep_allocator {
    typedef std::size_t size_type;
    typedef std::ptrdiff_t difference_type;

    static char *region, *alloc_bitmap;
    static size_t max_size, page_size;
    static boost::mutex alloc_lock;    

    static void init(size_type page_size, size_type extra_mem);
    static void destroy();
    static char *malloc(const size_type size);
    static void free(char *const addr);
    static size_t get_page_size();
};

#endif
