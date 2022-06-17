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

#ifndef RUSAGE_H
#define RUSAGE_H

#include <stdint.h>

struct RUsage {
  double utime;       // Time spent running userspace in seconds
  double stime;       // Time spent running kernel calls
  uint64_t ibytes;    // read from disk
  uint64_t obytes;    // written to disk
  uint64_t membytes;  // maximum resident set size
};

#ifdef __cplusplus
extern "C" {
#endif

extern struct RUsage rusage_sub(struct RUsage x, struct RUsage y);

// Resources used by all waited-for child processes.
// This includes grandchildren if their parents waited for them.
// This values reported only change after a call wait*()
extern struct RUsage getRUsageChildren();

#ifdef __cplusplus
};
#endif

#endif
