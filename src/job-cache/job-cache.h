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

#include <errno.h>
#include <fcntl.h>
#include <json/json5.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <util/execpath.h>
#include <util/mkdir_parents.h>
#include <wcl/filepath.h>
#include <wcl/trie.h>
#include <wcl/xoshiro_256.h>

#include <algorithm>
#include <future>
#include <iostream>
#include <map>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "bloom.h"
#include "logging.h"

namespace job_cache {

struct CachedOutputFile {
  std::string path;
  Hash256 hash;
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
  std::vector<std::string> input_files;
  std::vector<std::string> input_dirs;
  JobOutputInfo output_info;

  JAST to_json() const {
    JAST out(JSON_OBJECT);
    JAST output_files_json(JSON_ARRAY);
    JAST input_files_json(JSON_ARRAY);
    JAST input_dirs_json(JSON_ARRAY);

    // List the output files
    for (const auto &output_file : output_files) {
      // TODO: It's a shame we're throwing the hash out here :(
      output_files_json.add(output_file.path);
    }

    // List the input files
    for (const auto &input_file : input_files) {
      input_files_json.add(input_file);
    }

    // List the input dirs
    for (const auto &input_dir : input_dirs) {
      input_dirs_json.add(input_dir);
    }

    // Add all the fields
    out.add("output_files", std::move(output_files_json));
    out.add("input_files", std::move(input_files_json));
    out.add("input_dirs", std::move(input_dirs_json));
    out.add("stdout", output_info.stdout_str);
    out.add("stderr", output_info.stderr_str);
    out.add("status", output_info.ret_code);
    out.add("runtime", output_info.runtime);
    out.add("cputime", output_info.cputime);
    out.add("mem", static_cast<long long>(output_info.mem));
    out.add("ibytes", static_cast<long long>(output_info.ibytes));
    out.add("obytes", static_cast<long long>(output_info.obytes));

    return out;
  }
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

class CacheDbImpl;

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
