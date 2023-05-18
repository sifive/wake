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
#include <util/poll.h>
#include <wcl/optional.h>
#include <wcl/trie.h>
#include <wcl/unique_fd.h>

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "bloom.h"
#include "hash.h"
#include "message_parser.h"

namespace job_cache {

struct CachedOutputFile {
  std::string path;
  Hash256 hash;
  mode_t mode;

  CachedOutputFile() = default;
  explicit CachedOutputFile(const JAST &json);
  JAST to_json() const;
};

struct CachedOutputSymlink {
  std::string path;
  std::string value;

  CachedOutputSymlink() = default;
  explicit CachedOutputSymlink(const JAST &json);
  JAST to_json() const;
};

struct CachedOutputDir {
  std::string path;
  mode_t mode;

  CachedOutputDir() = default;
  explicit CachedOutputDir(const JAST &json);
  JAST to_json() const;
};

struct JobOutputInfo {
  std::string stdout_str;
  std::string stderr_str;
  int status;
  double runtime, cputime;
  uint64_t mem, ibytes, obytes;

  JobOutputInfo() = default;
  explicit JobOutputInfo(const JAST &json);
  JAST to_json() const;
};

struct MatchingJob {
  std::vector<CachedOutputFile> output_files;
  std::vector<CachedOutputSymlink> output_symlinks;
  std::vector<CachedOutputDir> output_dirs;
  std::vector<std::string> input_files;
  std::vector<std::string> input_dirs;
  JobOutputInfo output_info;

  MatchingJob() = default;
  explicit MatchingJob(const JAST &json);
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

  explicit FindJobRequest(const JAST &json);
  JAST to_json() const;
};

// JSON parsing stuff
struct InputFile {
  std::string path;
  Hash256 hash;

  InputFile() = default;
  explicit InputFile(const JAST &json);
  JAST to_json() const;
};

struct InputDir {
  std::string path;
  Hash256 hash;

  InputDir() = default;
  explicit InputDir(const JAST &json);
  JAST to_json() const;
};

struct OutputFile {
  std::string source;
  std::string path;
  Hash256 hash;
  mode_t mode;

  OutputFile() = default;
  explicit OutputFile(const JAST &json);
  JAST to_json() const;
};

struct OutputDirectory {
  std::string path;
  mode_t mode;

  OutputDirectory() = default;
  explicit OutputDirectory(const JAST &json);
  JAST to_json() const;
};

struct OutputSymlink {
  std::string value;
  std::string path;

  OutputSymlink() = default;
  explicit OutputSymlink(const JAST &json);
  JAST to_json() const;
};

struct AddJobRequest {
 private:
  AddJobRequest() = default;

 public:
  std::string cwd;
  std::string command_line;
  std::string envrionment;
  std::string stdin_str;
  BloomFilter bloom;
  std::vector<InputFile> inputs;
  std::vector<InputDir> directories;
  std::vector<OutputFile> outputs;
  std::vector<OutputDirectory> output_dirs;
  std::vector<OutputSymlink> output_symlinks;
  std::string stdout_str;
  std::string stderr_str;
  int status;
  double runtime, cputime;
  uint64_t mem, ibytes, obytes;

  AddJobRequest(const AddJobRequest &) = default;
  AddJobRequest(AddJobRequest &&) = default;

  explicit AddJobRequest(const JAST &json);
  JAST to_json() const;

  static AddJobRequest from_implicit(const JAST &json);
};

struct CacheDbImpl;

class DaemonCache {
 private:
  std::string dir;
  wcl::xoshiro_256 rng;
  std::unique_ptr<CacheDbImpl> impl;  // pimpl
  int evict_stdin;
  int evict_stdout;
  int evict_pid;
  uint64_t max_cache_size;
  uint64_t low_cache_size;
  std::string key;
  int listen_socket_fd;
  Poll poll;
  std::unordered_map<int, MessageParser> message_parsers;
  bool exit_now = false;

  void launch_evict_loop();
  void reap_evict_loop();

  wcl::optional<MatchingJob> read(const FindJobRequest &find_request);
  void add(const AddJobRequest &add_request);

  void handle_new_client();
  void handle_msg(int fd);

 public:
  ~DaemonCache();

  DaemonCache() = delete;
  DaemonCache(const DaemonCache &) = delete;

  DaemonCache(std::string _dir, uint64_t max, uint64_t low);

  int run();
};

class Cache {
 private:
  std::string _dir;
  uint64_t max;
  uint64_t low;
  wcl::unique_fd socket_fd;

 public:
  Cache() = delete;
  Cache(const Cache &) = delete;

  Cache(std::string _dir, uint64_t max, uint64_t low);

  wcl::optional<MatchingJob> read(const FindJobRequest &find_request);
  void add(const AddJobRequest &add_request);
};

}  // namespace job_cache
