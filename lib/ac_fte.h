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

#ifndef __ACFTE__
#define __ACFTE__

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>

void start_checkpointer();
void terminate_checkpointer();

void *add_region(void *addr, size_t size);
void remove_region(void *addr, size_t size);
void *malloc_protected(size_t size);
void free_protected(void *ptr, size_t size);
int checkpoint();
void wait_for_checkpoint();
void display_stats();

#ifdef __cplusplus
}
#endif

#endif
