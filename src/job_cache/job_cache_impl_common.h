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

#include <cstdint>
#include <string>
#include <vector>

// This function removes all the baking files of a specific job.
// While not technically unsafe to use on a job still in the databse
// this should be avoided.
void remove_backing_files(const std::string &dir, int64_t job_id);

// Like remove_backing_files(int64_t job_id) but removes many
// in parallel.
// NOTE: This should not be used from the wake process itself
//       because it can spawn threads.
void remove_backing_files(std::string dir, const std::vector<int64_t> &job_ids,
                          size_t min_removals_per_thread, size_t max_number_of_threads);

// Tries to reflink src to dst but copies if that fails.
void copy_or_reflink(const char *src, const char *dst, mode_t mode = 0644);

// These functions handle errors for us by calling log_fatal
// if we get an error we don't like.
void rename_no_fail(const char *old_path, const char *new_path);
void mkdir_no_fail(const char *dir);
void symlink_no_fail(const char *target, const char *symlink_path);
void unlink_no_fail(const char *file);
void rmdir_no_fail(const char *dir);
