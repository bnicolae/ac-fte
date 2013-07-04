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

#include <unistd.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/uio.h>

ssize_t read(int fd, void *buf, size_t size) {
    memset(buf, 0, size);
    return syscall(SYS_read, fd, buf, size);
}

ssize_t readv(int fd, const struct iovec *iov, int iovcnt) {
    int i;
    for (i = 0; i < iovcnt; i++)
	memset(iov[i].iov_base, 0, iov[i].iov_len);
    return syscall(SYS_readv, fd, iov, iovcnt);
}
