/*
 * Copyright 2019 SiFive, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You should have received a copy of LICENSE.Apache2 along with
 * this software. If not, you may obtain a copy at
 *
 *    https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Open Group Base Specifications Issue 7
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#ifdef __APPLE__
#include <mach/mach_host.h>
#include <mach/mach_init.h>
#endif

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include "physmem.h"

uint64_t get_physical_memory() {
  uint64_t out;
#ifdef __APPLE__
  struct host_basic_info hostinfo;
  mach_msg_type_number_t count = HOST_BASIC_INFO_COUNT;
  int result = host_info(mach_host_self(), HOST_BASIC_INFO,  reinterpret_cast<host_info_t>(&hostinfo), &count);
  if (result != KERN_SUCCESS || count != HOST_BASIC_INFO_COUNT) {
    fprintf(stderr, "host_info failed\n");
    exit(1);
  }
  out = static_cast<uint64_t>(hostinfo.max_mem);
#else
  out = sysconf(_SC_PHYS_PAGES);
  out *= sysconf(_SC_PAGESIZE);
#endif
  return out;
}
