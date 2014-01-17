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

#include "ac_fte.h"
#include "region_manager.hpp" 

extern "C" {
#include <stdio.h>
#include <signal.h>
#include <sys/mman.h>
}

#include <fstream>

// Memory region manager
static region_manager *m = NULL;
static boost::mpi::environment *env;

static struct sigaction old_handler;

static void handler(int sig, siginfo_t *si, void *unused) {
    if (si->si_code == SEGV_ACCERR && m && m->handle_segfault(si->si_addr))
	return;
    old_handler.sa_sigaction(sig, si, unused);
}

void __attribute__ ((constructor)) blobcr_constructor() {
}

void __attribute__ ((destructor)) blobcr_destructor() {
}

extern "C" void start_checkpointer() {
    std::string ckpt_path_prefix, ckpt_log_prefix;
    unsigned cow_size, rep; 
    bool iflag, aflag, dflag, gdflag;

    char *str = getenv("CKPT_PATH_PREFIX");
    if (str != NULL)
	ckpt_path_prefix = std::string(str);
    else
	ckpt_path_prefix = "/tmp";

    str = getenv("CKPT_LOG_PREFIX");
    if (str != NULL) 
	ckpt_log_prefix = std::string(str);
    else
	ckpt_log_prefix = "";

    str = getenv("CKPT_MAX_COW_SIZE");
    if (str == NULL || sscanf(str, "%u", &cow_size) != 1) 
	cow_size = 27;

    str = getenv("INCREMENTAL_FLAG");
    iflag = (str != NULL && strcasecmp(str, "true") == 0);

    str = getenv("ACCESS_ORDER_FLAG");
    aflag = (str != NULL && strcasecmp(str, "true") == 0);

    str = getenv("DEDUP_FLAG");
    dflag = (str != NULL && strcasecmp(str, "true") == 0);

    str = getenv("GLOBAL_DEDUP_FLAG");
    gdflag = (str != NULL && strcasecmp(str, "true") == 0);

    str = getenv("REPLICATION_FACTOR");
    if (str == NULL || sscanf(str, "%u", &rep) != 1 || rep < 1)
	rep = 0;
    
    int argc = 0;
    char **argv = NULL;
    env = new boost::mpi::environment(argc, argv);
    m = new region_manager(getpagesize(), ckpt_path_prefix, ckpt_log_prefix,
			   (boost::uint64_t)1 << cow_size, iflag, aflag, dflag, gdflag, rep);

    struct sigaction sa;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = handler;
    if (sigaction(SIGSEGV, &sa, &old_handler) == -1) {
	perror("AC-FTE sigaction failure");
	delete m;
    } else
	INFO("INIT: ckpt_path_prefix = " << ckpt_path_prefix 
	     << ", cow_size = " << cow_size
	     << ", iflag = " << iflag
	     << ", aflag = " << aflag
	     << ", dflag = " << dflag
	     << ", gdflag = " << gdflag
	     << ", rep = " << rep);
}

extern "C" void *add_region(void *addr, size_t size) {
    if (m && size % getpagesize() == 0 && addr != MAP_FAILED)
	m->add_region(addr, size);
    return addr;
}

extern "C" void remove_region(void *addr, size_t size) {
    if (m)
	m->remove_region(addr, size);
}

extern "C" void *malloc_protected(size_t size) {
    void *buff;
    size_t extended_size = size - (size % getpagesize());
    
    if (extended_size < size)
	extended_size += getpagesize();
    buff = mmap(NULL, extended_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (buff == MAP_FAILED)
	return NULL;
    if (m)
	m->add_region(buff, extended_size);
    return buff;
}

extern "C" void free_protected(void *buff, size_t size) {
    if (m)
	m->remove_region(buff, size);
    if (size > 0)
	munmap(buff, size);
}

extern "C" int checkpoint() {
    if (m)
	return (int)m->checkpoint();
    else
	return 0;
}

extern "C" void display_stats() {
    if (m)
	m->display_stats();
}

extern "C" void wait_for_checkpoint() {
    if (m)
	m->wait_for_completion();
}

extern "C" void terminate_checkpointer() {
    if (m) {
	sigaction(SIGSEGV, &old_handler, NULL);
	m->display_stats();
	delete m;
	m = NULL;
    }
    if (env)
	delete env;
}
