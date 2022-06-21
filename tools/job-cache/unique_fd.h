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

#pragma once

// Open Group Base Specifications Issue 7
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "logging.h"

class UniqueFd {
 private:
  int fd = -1;

  explicit UniqueFd(int fd) : fd(fd) {}

 public:
  UniqueFd() = default;
  UniqueFd(const UniqueFd&) = delete;

  UniqueFd& operator=(UniqueFd&& f) {
    fd = f.fd;
    f.fd = -1;
    return *this;
  }
  UniqueFd(UniqueFd&& f) : fd(f.fd) { f.fd = -1; }

  ~UniqueFd() {
    if (fd > 0) {
      if (close(fd) == -1) {
        log_fatal("close: %s", strerror(errno));
      }
    }
  }

  int get() const { return fd; }

  static UniqueFd open_fd(const char* str, int flags) {
    int fd = open(str, flags);
    if (fd == -1) {
      log_fatal("open(%s): %s", str, strerror(errno));
    }
    return UniqueFd(fd);
  }

  // Helper that only returns successful file opens and exits
  // otherwise.
  static UniqueFd open_fd(const char* str, int flags, mode_t mode) {
    int fd = open(str, flags, mode);
    if (fd == -1) {
      log_fatal("open(%s): %s", str, strerror(errno));
    }
    return UniqueFd(fd);
  }
};
