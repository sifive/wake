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

#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <json/json5.h>
#include <wcl/trie.h>

#include <map>
#include <string>
#include <unordered_map>

#include "bloom.h"
#include "hash.h"

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

  // Property of the client, not the job
  std::string client_cwd;

  FindJobRequest() = delete;
  FindJobRequest(const FindJobRequest &) = default;
  FindJobRequest(FindJobRequest &&) = default;

  explicit FindJobRequest(const JAST &json);
  JAST to_json() const;
};

struct FindJobResponse {
  wcl::optional<MatchingJob> match;

  FindJobResponse() = delete;
  explicit FindJobResponse(wcl::optional<MatchingJob> job) : match(std::move(job)) {}

  // The (de)serialized keys are
  //   - found: bool
  //   - match: MatchingJob
  // 'found' is determined implicitly based on if a MatchingJob is set and vice versa
  explicit FindJobResponse(JAST json);
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

  // Property of the client, not the job
  std::string client_cwd;

  AddJobRequest(const AddJobRequest &) = default;
  AddJobRequest(AddJobRequest &&) = default;

  explicit AddJobRequest(const JAST &json);
  JAST to_json() const;

  static AddJobRequest from_implicit(const JAST &json);
};
}  // namespace job_cache
