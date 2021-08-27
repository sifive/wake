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

// OS/X only makes ru.ru_maxrss available as an extension
#define _DARWIN_C_SOURCE 1

#include <sys/resource.h>
#include <assert.h>

#include "rusage.h"

RUsage::RUsage() : utime(0), stime(0), ibytes(0), obytes(0), membytes(0) { }

RUsage RUsage::operator - (const RUsage &other) const {
  RUsage out;
  out.utime    = utime    - other.utime;
  out.stime    = stime    - other.stime;
  out.ibytes   = ibytes   - other.ibytes;
  out.obytes   = obytes   - other.obytes;
  out.membytes = membytes - other.membytes;
  return out;
}

#if   defined(__APPLE__)
#define MEMBYTES(ru)    (ru.ru_maxrss)
#elif defined(__FreeBSD__)
#define MEMBYTES(ru)    (ru.ru_maxrss*1024)
#elif defined(__linux__)
#define MEMBYTES(ru)    (ru.ru_maxrss*1024)
#elif defined(__NetBSD__)
#define MEMBYTES(ru)    (ru.ru_maxrss*1024)
#elif defined(__OpenBSD__)
#define MEMBYTES(ru)    (ru.ru_maxrss*1024)
#elif defined(__sun)
#define MEMBYTES(ru)    (ru.ru_maxrss*getpagesize())
#elif defined(__EMSCRIPTEN__)
#define MEMBYTES(ru)	0
#else
#error Missing definition to access maxrss on this platform
#endif

RUsage getRUsageChildren() {
  RUsage out;
  struct rusage usage;

  // Can not fail (who and pointer are vaild)
  int ret = getrusage(RUSAGE_CHILDREN, &usage);
  assert (ret == 0);

  // These two are extremely portable:
  out.utime = usage.ru_utime.tv_sec + usage.ru_utime.tv_usec/1000000.0;
  out.stime = usage.ru_stime.tv_sec + usage.ru_stime.tv_usec/1000000.0;
  // These are non-standard, but relatively well supported:
  out.ibytes = usage.ru_inblock * UINT64_C(512);
  out.obytes = usage.ru_oublock * UINT64_C(512);
  // This one is super non-portable:
  out.membytes = MEMBYTES(usage);

  return out;
}
