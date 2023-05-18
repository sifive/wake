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

// Open Group Base Specifications Issue 7
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include "job_cache.h"

#include <errno.h>
#include <fcntl.h>
#include <json/json5.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#include <util/execpath.h>
#include <util/mkdir_parents.h>
#include <util/term.h>
#include <wcl/defer.h>
#include <wcl/filepath.h>
#include <wcl/trie.h>
#include <wcl/unique_fd.h>
#include <wcl/xoshiro_256.h>

#include <algorithm>
#include <future>
#include <iostream>
#include <map>
#include <random>
#include <thread>
#include <unordered_map>

#include "db_helpers.h"
#include "eviction_command.h"
#include "eviction_policy.h"
#include "job_cache_impl_common.h"
#include "logging.h"
#include "message_parser.h"

namespace job_cache {

// Helper to replace an existing file handle with
// a new one. For instance to replace stdin with
// /dev/null or stdout with a log file etc...
static void replace_fd(int old_fd, int new_fd) {
  if (dup2(new_fd, old_fd) == -1) {
    log_fatal("dup2: %s", strerror(errno));
  }
}

// Creates .log and .error.log in the given directory,
// removes our access to stdin/stdout/stderr. stdout and
// stderr are redirected to .log and .error.log respectivelly.
// The process is then daemonized by double forking with a
// setsid() in the middle. Lastly the sid and pid are logged.
//
// Returns true if the current process is the daemon.
static bool daemonize(std::string dir) {
  // first fork
  int pid = fork();
  if (pid == -1) {
    log_fatal("fork1: %s", strerror(errno));
  }

  if (pid != 0) {
    return false;
  }

  {
    // Replace stdin with /dev/null so we can't recivie input
    auto null_fd = wcl::unique_fd::open("/dev/null", O_RDONLY);
    if (!null_fd) {
      log_fatal("open(%s): %s", "/dev/null", strerror(null_fd.error()));
    }
    replace_fd(STDIN_FILENO, null_fd->get());
  }

  {
    // Replace stdout with dir/.log
    std::string log_path = dir + "/.log";
    auto log_fd = wcl::unique_fd::open(log_path.c_str(), O_CREAT | O_RDWR | O_APPEND, 0644);
    if (!log_fd) {
      log_fatal("open(%s): %s", log_path.c_str(), strerror(log_fd.error()));
    }
    replace_fd(STDOUT_FILENO, log_fd->get());
  }

  {
    // Open the error log
    std::string error_log_path = dir + "/.error.log";
    auto err_log_fd = wcl::unique_fd::open(error_log_path.c_str(), O_CREAT | O_RDWR | O_APPEND,
                                           0644);  // S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (!err_log_fd) {
      log_fatal("open(%s): %s", error_log_path.c_str(), strerror(err_log_fd.error()));
    }
    replace_fd(STDERR_FILENO, err_log_fd->get());
  }

  // setsid so we're in our own group
  int sid = setsid();
  if (sid == -1) {
    log_fatal("setsid: %s", strerror(errno));
  }

  // second fork so we can't undo any of the above
  pid = fork();
  if (pid == -1) {
    log_fatal("fork2: %s", strerror(errno));
  }

  if (pid != 0) {
    // Exit cleanly from orig parent
    log_exit("fork2: success");
  }

  // Log success with our pid the log
  pid = getpid();
  log_info("Daemon successfully created: sid = %d, pid = %d", sid, pid);
  return true;
}

wcl::optional<wcl::unique_fd> try_connect(std::string dir) {
  wcl::unique_fd socket_fd;
  {
    int local_socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (local_socket_fd == -1) {
      log_fatal("socket(AF_UNIX, SOCK_STREAM, 0): %s\n", strerror(errno));
    }
    socket_fd = wcl::unique_fd(local_socket_fd);
  }

  std::string key_path = dir + "/.key";

  char key[33] = {0};
  auto fd = wcl::unique_fd::open(key_path.c_str(), O_RDONLY);
  if (!fd) {
    // log_info("open(%s): %s", key_path.c_str(), strerror(fd.error()));
    return {};
  }

  if (::read(fd->get(), key, sizeof(key)) == -1) {
    log_fatal("read(%s): %s", key_path.c_str(), strerror(errno));
  }

  sockaddr_un addr = {0};
  addr.sun_family = AF_UNIX;
  addr.sun_path[0] = '\0';

  // TODO: make a client log file
  // log_info("key = %s, sizeof(key) = %d", key, sizeof(key));
  memcpy(addr.sun_path + 1, key, sizeof(key));
  if (connect(socket_fd.get(), reinterpret_cast<const sockaddr *>(&addr), sizeof(key)) == -1) {
    // TODO: make a client log file
    // log_info("connect(%s): %s", key, strerror(errno));
    return {};
  }

  return wcl::make_some<wcl::unique_fd>(std::move(socket_fd));
}

Cache::Cache(std::string _dir, uint64_t max, uint64_t low) : _dir(_dir), max(max), low(low) {
  mkdir_no_fail(_dir.c_str());

  // launch the daemon
  if (daemonize(_dir.c_str())) {
    // We are the daemon
    int ret_code = 1;
    {
      DaemonCache dcache(_dir, max, low);
      ret_code = dcache.run();
    }
    exit(ret_code);
  }

  // connect to the daemon with backoff
  wcl::xoshiro_256 rng(wcl::xoshiro_256::get_rng_seed());
  useconds_t backoff = 1000;
  for (int i = 0; i < 10; i++) {
    auto fd_opt = try_connect(_dir);
    if (!fd_opt) {
      std::uniform_int_distribution<useconds_t> variance(0, backoff);
      usleep(backoff + variance(rng));
      backoff *= 2;
      continue;
    }

    socket_fd = std::move(*fd_opt);
    break;
  }

  if (socket_fd.get() == -1) {
    log_fatal("could not connect to daemon. dir = %s", _dir);
  }
}

CachedOutputFile::CachedOutputFile(const JAST &json) {
  path = json.get("path").value;
  hash = Hash256::from_hex(json.get("hash").value);
  mode = std::stol(json.get("mode").value);
}

JAST CachedOutputFile::to_json() const {
  JAST json(JSON_OBJECT);
  json.add("path", path);
  json.add("hash", hash.to_hex());
  json.add("mode", int64_t(mode));
  return json;
}

CachedOutputSymlink::CachedOutputSymlink(const JAST &json) {
  path = json.get("path").value;
  value = json.get("value").value;
}

JAST CachedOutputSymlink::to_json() const {
  JAST json(JSON_OBJECT);
  json.add("path", path);
  json.add("value", value);
  return json;
}

CachedOutputDir::CachedOutputDir(const JAST &json) {
  path = json.get("path").value;
  mode = std::stol(json.get("mode").value);
}

JAST CachedOutputDir::to_json() const {
  JAST json(JSON_OBJECT);
  json.add("path", path);
  json.add("mode", int64_t(mode));
  return json;
}

JobOutputInfo::JobOutputInfo(const JAST &json) {
  stdout_str = json.get("stdout").value;
  stderr_str = json.get("stderr").value;
  status = std::stoi(json.get("status").value);
  runtime = std::stod(json.get("runtime").value);
  cputime = std::stod(json.get("cputime").value);
  mem = std::stoul(json.get("mem").value);
  ibytes = std::stoul(json.get("ibytes").value);
  obytes = std::stoul(json.get("obytes").value);
}

JAST JobOutputInfo::to_json() const {
  JAST json(JSON_OBJECT);
  json.add("stdout", stdout_str);
  json.add("stderr", stderr_str);
  json.add("status", status);
  json.add("runtime", runtime);
  json.add("cputime", cputime);
  json.add("mem", int64_t(mem));
  json.add("ibytes", int64_t(ibytes));
  json.add("obytes", int64_t(obytes));
  return json;
}

MatchingJob::MatchingJob(const JAST &json) {
  output_info = JobOutputInfo(json.get("output_info"));

  for (const auto &output_file_json : json.get("output_files").children) {
    output_files.push_back(CachedOutputFile(output_file_json.second));
  }

  for (const auto &output_dir_json : json.get("output_dirs").children) {
    output_dirs.push_back(CachedOutputDir(output_dir_json.second));
  }

  for (const auto &output_symlink_json : json.get("output_symlinks").children) {
    output_symlinks.push_back(CachedOutputSymlink(output_symlink_json.second));
  }

  for (const auto &input_file_json : json.get("input_files").children) {
    input_files.push_back(input_file_json.second.value);
  }

  for (const auto &input_dir_json : json.get("input_dirs").children) {
    input_dirs.push_back(input_dir_json.second.value);
  }
}

JAST MatchingJob::to_json() const {
  JAST json(JSON_OBJECT);

  json.add("output_info", output_info.to_json());

  JAST output_files_json(JSON_ARRAY);
  for (const auto &output_file : output_files) {
    output_files_json.add("", output_file.to_json());
  }
  json.add("output_files", std::move(output_files_json));

  JAST output_dirs_json(JSON_ARRAY);
  for (const auto &output_dir : output_dirs) {
    output_dirs_json.add("", output_dir.to_json());
  }
  json.add("output_dirs", std::move(output_dirs_json));

  JAST output_symlinks_json(JSON_ARRAY);
  for (const auto &output_symlink : output_symlinks) {
    output_symlinks_json.add("", output_symlink.to_json());
  }
  json.add("output_symlinks", std::move(output_symlinks_json));

  JAST input_files_json(JSON_ARRAY);
  for (const auto &input_file : input_files) {
    input_files_json.add("", input_file);
  }
  json.add("input_files", std::move(input_files_json));

  JAST input_dirs_json(JSON_ARRAY);
  for (const auto &input_dir : input_dirs) {
    input_dirs_json.add("", input_dir);
  }
  json.add("input_dirs", std::move(input_dirs_json));

  return json;
}

InputFile::InputFile(const JAST &json) {
  path = json.get("path").value;
  hash = Hash256::from_hex(json.get("hash").value);
}

JAST InputFile::to_json() const {
  JAST json(JSON_OBJECT);
  json.add("path", path);
  json.add("hash", hash.to_hex());
  return json;
}

InputDir::InputDir(const JAST &json) {
  path = json.get("path").value;
  hash = Hash256::from_hex(json.get("hash").value);
}

JAST InputDir::to_json() const {
  JAST json(JSON_OBJECT);
  json.add("path", path);
  json.add("hash", hash.to_hex());
  return json;
}

OutputFile::OutputFile(const JAST &json) {
  source = json.get("source").value;
  path = json.get("path").value;
  hash = Hash256::from_hex(json.get("hash").value);
  mode = std::stol(json.get("mode").value);
}

JAST OutputFile::to_json() const {
  JAST json(JSON_OBJECT);
  json.add("source", source);
  json.add("path", path);
  json.add("hash", hash.to_hex());
  json.add("mode", int64_t(mode));
  return json;
}

OutputDirectory::OutputDirectory(const JAST &json) {
  path = json.get("path").value;
  mode = std::stol(json.get("mode").value);
}

JAST OutputDirectory::to_json() const {
  JAST json(JSON_OBJECT);
  json.add("path", path);
  json.add("mode", int64_t(mode));
  return json;
}

OutputSymlink::OutputSymlink(const JAST &json) {
  path = json.get("path").value;
  value = json.get("value").value;
}

JAST OutputSymlink::to_json() const {
  JAST json(JSON_OBJECT);
  json.add("path", path);
  json.add("value", value);
  return json;
}

wcl::optional<MatchingJob> Cache::read(const FindJobRequest &find_request) {
  JAST request(JSON_OBJECT);
  request.add("method", "cache/read");
  request.add("params", find_request.to_json());

  // serialize the request, send it, deserialize the response, return it
  send_json_message(socket_fd.get(), request);
  MessageParser parser(socket_fd.get());
  std::vector<std::string> messages;

  MessageParserState state = parser.read_messages(messages);

  if (state == MessageParserState::StopFail) {
    log_fatal("Cache::read(): failed receiving message");
  }

  if (state == MessageParserState::StopSuccess && messages.empty()) {
    log_fatal("Cache::read(): daemon exited without responding");
  }

  if (messages.size() != 1) {
    log_fatal("Cache::read(): daemon responded with too many results");
  }

  // TODO: make a client log file
  // log_info("Cache::read(): message rx: %s", messages[0].c_str());

  JAST json;
  std::stringstream parseErrors;
  if (!JAST::parse(messages[0], parseErrors, json)) {
    log_fatal("Cache::read(): failed to parse daemon response");
  }

  if (json.get("params").kind == JSON_NULLVAL) {
    return {};
  }

  return wcl::make_some<MatchingJob>(MatchingJob(json.get("params")));
}

void Cache::add(const AddJobRequest &add_request) {
  // serialize the request, send it, deserialize the response, return it
  JAST request(JSON_OBJECT);
  request.add("method", "cache/add");
  request.add("params", add_request.to_json());

  // serialize the request, send it, deserialize the response, return it
  send_json_message(socket_fd.get(), request);
}

}  // namespace job_cache
