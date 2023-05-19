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

// the `Cache` class provides the full interface
// the the underlying complete cache directory.
// This requires interplay between the file system and
// the database and must be carefully orchestrated. This
// class handles all those details and provides a simple
// interface.

#pragma once

#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <wcl/optional.h>
#include <wcl/unique_fd.h>

#include <string>

#include "types.h"

namespace job_cache {

class Cache {
 private:
  wcl::unique_fd socket_fd;

 public:
  Cache() = delete;
  Cache(const Cache &) = delete;

  Cache(std::string dir, uint64_t max, uint64_t low);

  FindJobResponse read(const FindJobRequest &find_request);
  void add(const AddJobRequest &add_request);
};

}  // namespace job_cache
