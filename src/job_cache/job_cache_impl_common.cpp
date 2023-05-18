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

#include "job_cache_impl_common.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <future>
#include <thread>

#include "logging.h"
#include "wcl/filepath.h"
#include "wcl/result.h"
#include "wcl/unique_fd.h"
#include "wcl/xoshiro_256.h"

// moves the file or directory, crashes on error
void rename_no_fail(const char *old_path, const char *new_path) {
  if (rename(old_path, new_path) < 0) {
    log_fatal("rename(%s, %s): %s", old_path, new_path, strerror(errno));
  }
}

// Ensures the the given directory has been created
void mkdir_no_fail(const char *dir) {
  if (mkdir(dir, 0777) < 0 && errno != EEXIST) {
    log_fatal("mkdir(%s): %s", dir, strerror(errno));
  }
}

void chdir_no_fail(const char *dir) {
  if (chdir(dir) < 0) {
    log_fatal("chdir(%s): %s", dir, strerror(errno));
  }
}

void symlink_no_fail(const char *target, const char *symlink_path) {
  if (symlink(target, symlink_path) == -1) {
    log_fatal("symlink(%s, %s): %s", target, symlink_path, strerror(errno));
  }
}

// Ensures the given file has been deleted
void unlink_no_fail(const char *file) {
  if (unlink(file) < 0 && errno != ENOENT) {
    log_fatal("unlink(%s): %s", file, strerror(errno));
  }
}

// Ensures the the given directory no longer exists
void rmdir_no_fail(const char *dir) {
  if (rmdir(dir) < 0 && errno != ENOENT) {
    log_fatal("rmdir(%s): %s", dir, strerror(errno));
  }
}

// For apple and emscripten fallback on a dumb slow implementation
#if defined(__APPLE__) || defined(__EMSCRIPTEN__)

#include "util/term.h"

static void copy(int src_fd, int dst_fd) {
  FdBuf src(src_fd);
  FdBuf dst(dst_fd);
  std::ostream out(&dst);
  std::istream in(&src);

  struct stat buf = {};
  // There's a race here between the fstat and the copy_file_range
  if (fstat(src_fd, &buf) < 0) {
    log_fatal("fstat(src_fd = %d): %s", src_fd, strerror(errno));
  }

  // TODO: This is very slow because FdBuf is very slow
  // TODO: This will read large files into memory which is very bad
  std::vector<char> data_buf(buf.st_size);
  if (!in.read(data_buf.data(), buf.st_size)) {
    log_fatal("copy.read(src_fd = %d, NULL, dst_fd = %d, size = %d): %s", src_fd, dst_fd,
              buf.st_size, strerror(errno));
  }

  if (!out.write(data_buf.data(), buf.st_size)) {
    log_fatal("copy.write(src_fd = %d, NULL, dst_fd = %d, size = %d): %s", src_fd, dst_fd,
              buf.st_size, strerror(errno));
  }
}

// For modern linux use copy_file_range
#elif __GLIBC__ >= 2 && __GLIBC_MINOR__ >= 27

#include <linux/fs.h>

// This function just uses `copy_file_range` to make
// an efficent copy. It is however not atomic because
// we have to `fstat` before we call `copy_file_range`.
// This means that if an external party decides to mutate
// the file (espeically changing its size) then this function
// will not work as intended. External parties *are* allowed to
// unlink this file but they're not allowed to modify it in any
// other way or else this function will race with that external
// modification.
static void copy(int src_fd, int dst_fd) {
  struct stat buf = {};
  // There's a race here between the fstat and the copy_file_range
  if (fstat(src_fd, &buf) < 0) {
    log_fatal("fstat(src_fd = %d): %s", src_fd, strerror(errno));
  }
  if (copy_file_range(src_fd, nullptr, dst_fd, nullptr, buf.st_size, 0) < 0) {
    log_fatal("copy_file_range(src_fd = %d, NULL, dst_fd = %d, size = %d, 0): %s", src_fd, dst_fd,
              buf.st_size, strerror(errno));
  }
}

// For older linux distros use Linux's sendfile
#else

#include <sys/sendfile.h>
// This function just uses `sendfile` to make
// an efficent copy. It is however not atomic because
// we have to `fstat` before we call `sendfile`.
// This means that if an external party decides to mutate
// the file (espeically changing its size) then this function
// will not work as intended. External parties *are* allowed to
// unlink this file but they're not allowed to modify it in any
// other way or else this function will race with that external
// modification.

static void copy(int src_fd, int dst_fd) {
  struct stat buf = {};
  // There's a race here between the fstat and the copy_file_range
  if (fstat(src_fd, &buf) < 0) {
    log_fatal("fstat(src_fd = %d): %s", src_fd, strerror(errno));
  }
  off_t idx = 0;
  size_t size = buf.st_size;
  do {
    intptr_t written = sendfile(dst_fd, src_fd, &idx, size);
    if (written < 0) {
      log_fatal("sendfile(src_fd = %d, NULL, dst_fd = %d, size = %d, 0): %s", src_fd, dst_fd,
                buf.st_size, strerror(errno));
    }
    idx = written;
    size -= written;
  } while (size != 0);
}
#endif

#ifdef FICLONE

void copy_or_reflink(const char *src, const char *dst, mode_t mode, int extra_flags) {
  auto src_fd = wcl::unique_fd::open(src, O_RDONLY);
  if (!src_fd) {
    log_fatal("open(%s): %s", src, strerror(src_fd.error()));
  }
  auto dst_fd = wcl::unique_fd::open(dst, O_WRONLY | O_CREAT | extra_flags, mode);
  if (!dst_fd) {
    log_fatal("open(%s): %s", dst, strerror(dst_fd.error()));
  }

  if (ioctl(dst_fd->get(), FICLONE, src_fd->get()) < 0) {
    if (errno != EINVAL && errno != EOPNOTSUPP && errno != EXDEV) {
      log_fatal("ioctl(%s, FICLONE, %d): %s", dst, src, strerror(errno));
    }
    copy(src_fd->get(), dst_fd->get());
  }
}

#else

void copy_or_reflink(const char *src, const char *dst, mode_t mode, int extra_flags) {
  auto src_fd = wcl::unique_fd::open(src, O_RDONLY);
  if (!src_fd) {
    log_fatal("open(%s): %s", src, strerror(src_fd.error()));
  }
  auto dst_fd = wcl::unique_fd::open(dst, O_WRONLY | O_CREAT | extra_flags, mode);
  if (!dst_fd) {
    log_fatal("open(%s): %s", dst, strerror(dst_fd.error()));
  }

  copy(src_fd->get(), dst_fd->get());
}

#endif

void remove_backing_files(const std::string &dir, int64_t job_id) {
  uint8_t group_id = job_id & 0xFF;
  std::string job_dir = wcl::join_paths(dir, wcl::to_hex(&group_id), std::to_string(job_id));
  auto dir_range = wcl::directory_range::open(job_dir);
  if (!dir_range) {
    log_fatal("opendir(%s): %s", job_dir.c_str(), strerror(dir_range.error()));
  }

  // loop over the directory now and delete any files as we go.
  for (const auto &entry : *dir_range) {
    if (!entry) {
      log_fatal("readdir(%s): %s", job_dir.c_str(), strerror(entry.error()));
    }
    if (entry->name == "." || entry->name == "..") continue;
    if (entry->type != wcl::file_type::regular) {
      log_fatal("remove_backing_files(%s): found non-regular entry: %s", job_dir.c_str(),
                entry->name.c_str());
    }
    unlink_no_fail(entry->name.c_str());
  }
}

void remove_backing_files(std::string dir, const std::vector<int64_t> &job_ids,
                          size_t min_removals_per_thread, size_t max_number_of_threads) {
  // Calculate a good number of threads to use.
  size_t actual_num_threads =
      std::min(max_number_of_threads, std::max(1UL, job_ids.size() / min_removals_per_thread));

  // Calculate how many removals will be done by each task
  size_t actual_removals_per_thread =
      job_ids.size() / actual_num_threads + !!(job_ids.size() % actual_num_threads);

  // Kick off each task
  std::vector<std::future<void>> tasks;
  auto iter = job_ids.begin();
  auto end = job_ids.end();
  while (iter < end) {
    auto task_end = iter + actual_removals_per_thread;
    tasks.emplace_back(std::async(std::launch::async, [&dir, iter, task_end, end]() {
      auto i = iter;
      for (; i < task_end && i < end; ++i) {
        remove_backing_files(dir, *i);
      }
    }));
    iter = task_end;
  }

  // Now join the tasks
  for (auto &task : tasks) {
    task.wait();
  }
}

void send_json_message(int fd, const JAST &json) {
  std::stringstream s;
  s << json;
  std::string json_str = s.str();
  json_str += '\0';

  size_t start = 0;
  while (start < json_str.size()) {
    int res = write(fd, json_str.data() + start, json_str.size() - start);
    if (res == -1) {
      if (errno == EINTR) {
        continue;
      }
      log_fatal("write(%d): %s", fd, strerror(errno));
    }

    start += res;
  }
}
