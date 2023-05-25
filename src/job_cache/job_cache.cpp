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

#include <sys/socket.h>
#include <sys/un.h>
#include <util/execpath.h>
#include <wcl/filepath.h>
#include <wcl/optional.h>
#include <wcl/unique_fd.h>
#include <wcl/xoshiro_256.h>

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

#include "daemon_cache.h"
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

  // TODO: We should make this read more robust. It's mostly fine if
  //       it returns fewer bytes than we asked for but if it gives
  //       us EINTR that could cause some strange failures that should
  //       be avoided. That's quite unlikely however.
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

Cache::Cache(std::string dir, uint64_t max, uint64_t low) {
  mkdir_no_fail(dir.c_str());

  // launch the daemon
  if (daemonize(dir.c_str())) {
    // We are the daemon, launch the cache

    std::string job_cache = wcl::make_canonical(find_execpath() + "/../bin/job-cache");
    std::string low_str = std::to_string(low);
    std::string max_str = std::to_string(max);
    execl(job_cache.c_str(), "job-cached", dir.c_str(), low_str.c_str(), max_str.c_str(), nullptr);
    std::cerr << "exec(" << job_cache << "): " << strerror(errno) << std::endl;
    exit(1);
  }

  // connect to the daemon with backoff.
  // TODO: Put this into a function so that we can call it again if
  // the daemon fails unexpectedly.
  wcl::xoshiro_256 rng(wcl::xoshiro_256::get_rng_seed());
  useconds_t backoff = 1000;
  for (int i = 0; i < 10; i++) {
    auto fd_opt = try_connect(dir);
    if (!fd_opt) {
      std::uniform_int_distribution<useconds_t> variance(0, backoff);
      usleep(backoff + variance(rng));
      backoff *= 2;
      continue;
    }

    socket_fd = std::move(*fd_opt);
    break;
  }

  if (!socket_fd.valid()) {
    log_fatal("could not connect to daemon. dir = %s", dir.c_str());
  }
}

FindJobResponse Cache::read(const FindJobRequest &find_request) {
  JAST request(JSON_OBJECT);
  request.add("method", "cache/read");
  request.add("params", find_request.to_json());

  // serialize the request, send it, deserialize the response, return it
  send_json_message(socket_fd.get(), request);
  MessageParser parser(socket_fd.get());
  std::vector<std::string> messages;

  while (true) {
    MessageParserState state = parser.read_messages(messages);

    if (state == MessageParserState::StopFail) {
      // TODO: Try to reconnect to the daemon, launching our own if need be.
      //       if that fails then depending on user preference either fail
      //       or return a cache miss.
      log_fatal("Cache::read(): failed receiving message");
    }

    if (state == MessageParserState::StopSuccess && messages.empty()) {
      // TODO: Try to reconnect to the daemon, launching our own if need be.
      //       if that fails then depending on user preference either fail
      //       or return a cache miss.
      log_fatal("Cache::read(): daemon exited without responding");
    }

    // MessageParser tries to avoid this but we should defend against
    // the case where no error has yet occured but messages is still empty.
    if (state == MessageParserState::Continue && messages.empty()) {
      continue;
    }

    if (messages.size() != 1) {
      log_info("message.size() == %llu", messages.size());
      for (const auto &message : messages) {
        log_info("message.size() = %llu, message = '%s'", message.size(), message.c_str());
      }
      log_fatal("Cache::read(): daemon responded with too many results");
    }

    break;
  }

  // TODO: make a client log file
  // log_info("Cache::read(): message rx: %s", messages[0].c_str());

  JAST json;
  std::stringstream parseErrors;
  if (!JAST::parse(messages[0], parseErrors, json)) {
    log_fatal("Cache::read(): failed to parse daemon response");
  }

  return FindJobResponse(json);
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
