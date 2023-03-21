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

// Open Group Base Specifications Issue 7
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "logging.h"

// moves the file or directory, crashes on error
inline void rename_no_fail(const char *old_path, const char *new_path) {
  if (rename(old_path, new_path) < 0) {
    log_fatal("rename(%s, %s): %s", old_path, new_path, strerror(errno));
  }
}

// Ensures the the given directory has been created
inline void mkdir_no_fail(const char *dir) {
  if (mkdir(dir, 0777) < 0 && errno != EEXIST) {
    log_fatal("mkdir(%s): %s", dir, strerror(errno));
  }
}

inline void symlink_no_fail(const char *target, const char *symlink_path) {
  if (symlink(target, symlink_path) == -1) {
    log_fatal("symlink(%s, %s): %s", target, symlink_path, strerror(errno));
  }
}

// Ensures the given file has been deleted
inline void unlink_no_fail(const char *file) {
  if (unlink(file) < 0 && errno != ENOENT) {
    log_fatal("unlink(%s): %s", file, strerror(errno));
  }
}

// Ensures the the given directory no longer exists
inline void rmdir_no_fail(const char *dir) {
  if (rmdir(dir) < 0 && errno != ENOENT) {
    log_fatal("rmdir(%s): %s", dir, strerror(errno));
  }
}
