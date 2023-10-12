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

#pragma once

#include <string>
#include "wcl/result.h"
#include "wcl/optional.h"

enum class SquashFuseMountWaitErrorType {
  CannotOpenFifo,
  FailureToReadFifo,
  ReceivedZeroBytes,
  MountFailed
};
struct SquashFuseMountWaitError {
  SquashFuseMountWaitErrorType type;
  wcl::posix_error_t posix_error;
};

// Create a named pipe (FIFO) with a temporary and random name
wcl::result<std::string, wcl::posix_error_t> mktempfifo();

// Wait for a signal on the named pipe to confirm squashfuse mount
wcl::optional<SquashFuseMountWaitError> wait_for_squashfuse_mount(const std::string& squashfuse_fifo_path);

