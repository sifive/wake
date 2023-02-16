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

#include <json/json5.h>
#include <sys/stat.h>
#include <wcl/optional.h>
#include <wcl/trie.h>

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "bloom.h"
#include "hash.h"

namespace job_cache {

struct CachedOutputFile {
  std::string path;
  Hash256 hash;
  mode_t mode;
};

struct CachedOutputSymlink {
  std::string path;
  std::string value;
};

struct CachedOutputDir {
  std::string path;
  mode_t mode;
};

struct JobOutputInfo {
  std::string stdout_str;
  std::string stderr_str;
  int ret_code;
  double runtime, cputime;
  uint64_t mem, ibytes, obytes;
};

struct MatchingJob {
  int64_t job_id;
  std::vector<CachedOutputFile> output_files;
  std::vector<CachedOutputSymlink> output_symlinks;
  std::vector<CachedOutputDir> output_dirs;
  std::vector<std::string> input_files;
  std::vector<std::string> input_dirs;
  JobOutputInfo output_info;

  JAST to_json() const;
};

struct FindJobRequest {
 public:
  std::string cwd;
  std::string command_line;
  std::string envrionment;
  std::string stdin_str;
  wcl::trie<std::string, std::string> dir_redirects;
  BloomFilter bloom;
  // Using an ordered map is a neat trick here. It
  // gives us repeatable hashes on directories
  // later.
  std::map<std::string, Hash256> visible;
  std::unordered_map<std::string, Hash256> dir_hashes;

  FindJobRequest() = delete;
  FindJobRequest(const FindJobRequest &) = default;
  FindJobRequest(FindJobRequest &&) = default;

  explicit FindJobRequest(const JAST &find_job_json);
};

// JSON parsing stuff
struct InputFile {
  std::string path;
  Hash256 hash;
};

struct InputDir {
  std::string path;
  Hash256 hash;
};

struct OutputFile {
  std::string source;
  std::string path;
  Hash256 hash;
  mode_t mode;
};

struct OutputDirectory {
  std::string path;
  mode_t mode;
};

// TODO: Add mode to avoid having to fstat again
struct OutputSymlink {
  std::string value;
  std::string path;
};

struct AddJobRequest {
 public:
  std::string cwd;
  std::string command_line;
  std::string envrionment;
  std::string stdin_str;
  BloomFilter bloom;
  std::vector<InputFile> inputs;
  std::vector<InputDir> directories;
  std::vector<OutputFile> outputs;
  // TODO: Add mode to avoid having to fstat again
  std::vector<OutputDirectory> output_dirs;
  std::vector<OutputSymlink> output_symlinks;
  std::string stdout_str;
  std::string stderr_str;
  int ret_code;
  double runtime, cputime;
  uint64_t mem, ibytes, obytes;

  AddJobRequest() = delete;
  AddJobRequest(const AddJobRequest &) = default;
  AddJobRequest(AddJobRequest &&) = default;

  explicit AddJobRequest(const JAST &job_result_json);
};

struct CacheDbImpl;

class Cache {
 private:
  std::string dir;
  wcl::xoshiro_256 rng;
  std::unique_ptr<CacheDbImpl> impl;  // pimpl

 public:
  ~Cache();

  Cache() = delete;
  Cache(const Cache &) = delete;

  Cache(std::string _dir);

  wcl::optional<MatchingJob> read(const FindJobRequest &find_request);
  void add(const AddJobRequest &add_request);
};

}  // namespace job_cache
