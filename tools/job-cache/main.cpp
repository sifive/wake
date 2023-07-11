/*
 * Copyright 2023 SiFive, Inc.
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

#include <job_cache/daemon_cache.h>
#include <wcl/tracing.h>

// argv[0] = program name
// argv[1] = cache dir
// argv[2] = low cache size
// argv[3] = max cache size
int main(int argc, char **argv) {
  if (argc != 4) {
    std::cerr << "Usage: job-cache cache/directory 100 5000" << std::endl;
    return 1;
  }

  std::string cache_dir = std::string(argv[1]);
  uint64_t low_cache_size = std::stoull(argv[2]);
  uint64_t max_cache_size = std::stoull(argv[3]);

  int status = 1;
  {
    job_cache::DaemonCache dcache(std::move(cache_dir), max_cache_size, low_cache_size);
    status = dcache.run();
  }

  return status;
}
