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

#include <json/json5.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <cstdint>
#include <string>
#include <vector>

#include "wcl/optional.h"
#include "wcl/result.h"

using group_id_t = uint8_t;

int64_t current_time_microseconds();

// This function removes all the baking files of a specific job.
// While not technically unsafe to use on a job still in the databse
// this should be avoided.
void remove_job_backing_files(const std::string &dir, int64_t job_id);

// Like remove_backing_files(int64_t job_id) but removes many
// in parallel.
// NOTE: This should not be used from the wake process itself
//       because it can spawn threads.
void remove_backing_files(std::string dir, std::vector<std::pair<int64_t, std::string>> job_ids,
                          size_t max_number_of_threads);

// Tries to reflink src to dst but copies if that fails.
void copy_or_reflink(const char *src, const char *dst, mode_t mode = 0644, int extra_flags = 0);

// These functions handle errors for us by calling log_fatal
// if we get an error we don't like.
void rename_no_fail(const char *old_path, const char *new_path);
void mkdir_no_fail(const char *dir);
void chdir_no_fail(const char *dir);
void symlink_no_fail(const char *target, const char *symlink_path);
void unlink_no_fail(const char *file);
void rmdir_no_fail(const char *dir);

namespace job_cache {

enum class SyncMessageReadError {
  Fail,
  Timeout,
};

// Continues reading until fd is closed by the other side, an error occurs, or a timeout occurs.
// Returns every message read withint that time frame.
wcl::result<std::vector<std::string>, SyncMessageReadError> sync_read_message(
    int fd, uint64_t timeout_seconds);

// Write the serialized JAST to fd synchronously, returning an error from write
// ETIME is returned if a timeout occurs.
wcl::optional<wcl::posix_error_t> sync_send_json_message(int fd, const JAST &json,
                                                         uint64_t timeout_seconds);

}  // namespace job_cache