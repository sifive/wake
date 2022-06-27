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

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "result.h"

namespace wcl {

class unique_fd {
 private:
  int fd = -1;

  // We don't want user's to be able to construct like
  // this directly but we need to be able to do this.
  explicit unique_fd(int fd) : fd(fd) {}

 public:
  unique_fd() = default;
  unique_fd(const unique_fd&) = delete;

  unique_fd& operator=(unique_fd&& f) {
    fd = f.fd;
    f.fd = -1;
    return *this;
  }
  unique_fd(unique_fd&& f) : fd(f.fd) { f.fd = -1; }

  ~unique_fd() {
    if (fd > 0) {
      // It's a bit annoying but we aren't allowed to
      // error handle in anyway here. We add an assert
      // so that we are likely to catch this issue.
      assert(close(fd) > -1);
    }
  }

  int get() const { return fd; }

  // Opens a file just as posix's open would, returning
  // either a unique_fd or the errno value.
  static result<unique_fd, int> open(const char* str, int flags) {
    int fd = ::open(str, flags);
    if (fd == -1) {
      return make_error<unique_fd, int>(errno);
    }
    return make_result<unique_fd, int>(unique_fd(fd));
  }

  // Opens a file just as posix's open would, returning
  // either a unique_fd or the errno value.
  static result<unique_fd, int> open(const char* str, int flags, mode_t mode) {
    int fd = ::open(str, flags, mode);
    if (fd == -1) {
      return make_error<unique_fd, int>(errno);
    }
    return make_result<unique_fd, int>(unique_fd(fd));
  }
};

}  // namespace wcl
