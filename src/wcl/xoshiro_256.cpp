
/*
 * Copyright 2022 SiFive, Inc.
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

#include "xoshiro_256.h"

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <cstring>
#include <iostream>
#include <tuple>

namespace wcl {

std::tuple<uint64_t, uint64_t, uint64_t, uint64_t> xoshiro_256::get_rng_seed() {
  // TODO: This really needs to be using a unique_fd/return a result
  // That is currently blocked by landing wcl. Update this once
  // unique_fd and result land.

  int rng_fd = open("/dev/urandom", O_RDONLY, 0644);
  if (rng_fd == -1) {
    std::cerr << "Failed to open /dev/urandom: " << strerror(errno) << std::endl;
    exit(1);
  }

  uint8_t seed_data[32] = {0};
  if (read(rng_fd, seed_data, sizeof(seed_data)) < 0) {
    std::cerr << "Failed to read /dev/urandom: " << strerror(errno) << std::endl;
    exit(1);
  }
  close(rng_fd);

  uint64_t *data = reinterpret_cast<uint64_t *>(seed_data);
  return std::make_tuple(data[0], data[1], data[2], data[3]);
}

}  // namespace wcl
