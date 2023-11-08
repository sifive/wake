/* Squashfuse mount helper fuctions
 *
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

#include "squashfuse_helper.h"

#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <wcl/defer.h>
#include <wcl/result.h>
#include <wcl/unique_fd.h>
#include <wcl/xoshiro_256.h>

#include <iostream>

// Works like mkfifo but it gets created in a mktemp() style.
wcl::result<std::string, wcl::posix_error_t> mktempfifo() {
  wcl::xoshiro_256 rng(wcl::xoshiro_256::get_rng_seed());
  std::string fifo_filepath = "/tmp/squashfuse_notify_pipe_fifo_" + rng.unique_name();

  int mkfifoat_result = mkfifo(fifo_filepath.c_str(), 0664);
  if (mkfifoat_result < 0) {
    return wcl::make_errno<std::string>();
  }

  return wcl::make_result<std::string, wcl::posix_error_t>(fifo_filepath);
}

wcl::optional<SquashFuseMountWaitError> wait_for_squashfuse_mount(
    const std::string& squashfuse_fifo_path) {
  auto squashfuse_notify_pipe_fd = wcl::unique_fd::open(squashfuse_fifo_path.c_str(), O_RDONLY);
  if (!squashfuse_notify_pipe_fd) {
    return wcl::some(SquashFuseMountWaitError{SquashFuseMountWaitErrorType::CannotOpenFifo, errno});
  }

  auto defer = wcl::make_defer([&]() { unlink(squashfuse_fifo_path.c_str()); });

  char squashfuse_notify_result = '\0';
  ssize_t bytes_read = read(squashfuse_notify_pipe_fd->get(), &squashfuse_notify_result,
                            sizeof(squashfuse_notify_result));
  if (bytes_read == -1) {
    return wcl::some(
        SquashFuseMountWaitError{SquashFuseMountWaitErrorType::FailureToReadFifo, errno});
  } else if (bytes_read == 0) {
    return wcl::some(SquashFuseMountWaitError{SquashFuseMountWaitErrorType::ReceivedZeroBytes, -1});
  }

  if (squashfuse_notify_result == 'f') {
    return wcl::some(SquashFuseMountWaitError{SquashFuseMountWaitErrorType::MountFailed, -1});
  }

  return {};
}
