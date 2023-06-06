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
#include <wcl/tracing.h>
#include <wcl/unique_fd.h>
#include <wcl/xoshiro_256.h>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "daemon_cache.h"
#include "job_cache_impl_common.h"
#include "message_parser.h"

namespace job_cache {

// Helper to replace an existing file handle with
// a new one. For instance to replace stdin with
// /dev/null or stdout with a log file etc...
static void replace_fd(int old_fd, int new_fd) {
  if (dup2(new_fd, old_fd) == -1) {
    wcl::log::fatal("dup2: %s", strerror(errno));
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
    wcl::log::fatal("fork1: %s", strerror(errno));
  }

  if (pid != 0) {
    return false;
  }

  {
    // Replace stdin with /dev/null so we can't receive input
    auto null_fd = wcl::unique_fd::open("/dev/null", O_RDONLY);
    if (!null_fd) {
      wcl::log::fatal("open(%s): %s", "/dev/null", strerror(null_fd.error()));
    }
    replace_fd(STDIN_FILENO, null_fd->get());
  }

  {
    // Replace stdout with dir/.stdout
    std::string log_path = dir + "/.stdout";
    auto log_fd = wcl::unique_fd::open(log_path.c_str(), O_CREAT | O_RDWR | O_APPEND, 0644);
    if (!log_fd) {
      wcl::log::fatal("open(%s): %s", log_path.c_str(), strerror(log_fd.error()));
    }
    replace_fd(STDOUT_FILENO, log_fd->get());
  }

  {
    // Replace stderr with dir/.stderr
    std::string error_log_path = dir + "/.stderr";
    auto err_log_fd = wcl::unique_fd::open(error_log_path.c_str(), O_CREAT | O_RDWR | O_APPEND,
                                           0644);  // S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (!err_log_fd) {
      wcl::log::fatal("open(%s): %s", error_log_path.c_str(), strerror(err_log_fd.error()));
    }
    replace_fd(STDERR_FILENO, err_log_fd->get());
  }

  wcl::log::clear_subscribers();
  wcl::log::subscribe(std::make_unique<wcl::log::FormatSubscriber>(std::cout.rdbuf()));
  wcl::log::info("Reinitialized logging for job cache daemon");

  // setsid so we're in our own group
  int sid = setsid();
  if (sid == -1) {
    wcl::log::fatal("setsid: %s", strerror(errno));
  }

  // second fork so we can't undo any of the above
  pid = fork();
  if (pid == -1) {
    wcl::log::fatal("fork2: %s", strerror(errno));
  }

  if (pid != 0) {
    // Exit cleanly from orig parent
    wcl::log::exit("fork2: success");
  }

  // Log success with our pid the log
  pid = getpid();
  wcl::log::info("Daemon successfully created: sid = %d, pid = %d", sid, pid);
  return true;
}

wcl::optional<wcl::unique_fd> try_connect(std::string dir) {
  wcl::unique_fd socket_fd;
  {
    int local_socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (local_socket_fd == -1) {
      wcl::log::fatal("socket(AF_UNIX, SOCK_STREAM, 0): %s\n", strerror(errno));
    }
    socket_fd = wcl::unique_fd(local_socket_fd);
  }

  std::string key_path = dir + "/.key";

  char key[33] = {0};
  auto fd = wcl::unique_fd::open(key_path.c_str(), O_RDONLY);
  if (!fd) {
    wcl::log::info("open(%s): %s", key_path.c_str(), strerror(fd.error()));
    return {};
  }

  // TODO: We should make this read more robust. It's mostly fine if
  //       it returns fewer bytes than we asked for but if it gives
  //       us EINTR that could cause some strange failures that should
  //       be avoided. That's quite unlikely however.
  if (::read(fd->get(), key, sizeof(key)) == -1) {
    wcl::log::fatal("read(%s): %s", key_path.c_str(), strerror(errno));
  }

  sockaddr_un addr = {0};
  addr.sun_family = AF_UNIX;
  addr.sun_path[0] = '\0';

  wcl::log::info("key = %s, sizeof(key) = %lu", key, sizeof(key));
  memcpy(addr.sun_path + 1, key, sizeof(key));
  if (connect(socket_fd.get(), reinterpret_cast<const sockaddr *>(&addr), sizeof(key)) == -1) {
    wcl::log::info("connect(%s): %s", key, strerror(errno));
    return {};
  }

  return wcl::make_some<wcl::unique_fd>(std::move(socket_fd));
}

// Launch the job cache daemon
void Cache::launch_daemon() {
  // We are the daemon, launch the cache
  if (daemonize(cache_dir.c_str())) {
    std::string job_cache = wcl::make_canonical(find_execpath() + "/../bin/job-cache");
    std::string low_str = std::to_string(low_threshold);
    std::string max_str = std::to_string(max_size);
    execl(job_cache.c_str(), "job-cached", cache_dir.c_str(), low_str.c_str(), max_str.c_str(),
          nullptr);

    wcl::log::fatal("exec(%s): %s", job_cache.c_str(), strerror(errno));
  }
}

// Connect to the job cache daemon with backoff.
void Cache::backoff_try_connect(int attempts) {
  wcl::xoshiro_256 rng(wcl::xoshiro_256::get_rng_seed());
  useconds_t backoff = 1000;
  for (int i = 0; i < attempts; i++) {
    auto fd_opt = try_connect(cache_dir);
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
    wcl::log::fatal("could not connect to daemon. dir = %s", cache_dir.c_str());
  }
}

Cache::Cache(std::string dir, uint64_t max, uint64_t low) {
  cache_dir = dir;
  max_size = max;
  low_threshold = low;

  mkdir_no_fail(cache_dir.c_str());

  launch_daemon();
  backoff_try_connect(14);
}

wcl::result<FindJobResponse, FindJobError> Cache::read_impl(const FindJobRequest &find_request) {
  JAST request(JSON_OBJECT);
  request.add("method", "cache/read");
  request.add("params", find_request.to_json());

  // serialize the request, send it, deserialize the response, return it
  send_json_message(socket_fd.get(), request);
  MessageParser parser(socket_fd.get());
  std::vector<std::string> messages;

  MessageParserState state = MessageParserState::Continue;
  do {
    state = parser.read_messages(messages);

    if (state == MessageParserState::StopFail) {
      // TODO: Add config var to determine if fail is a cache miss
      if (false) {
        return wcl::result_value<FindJobError>(FindJobResponse(wcl::optional<MatchingJob>{}));
      }

      wcl::log::error("Cache::read(): failed receiving message");
      return wcl::result_error<FindJobResponse>(FindJobError::FailedMessageReceive);
    }

    if (state == MessageParserState::StopSuccess && messages.empty()) {
      // TODO: Add config var to determine if fail is a cache miss
      if (false) {
        return wcl::result_value<FindJobError>(FindJobResponse(wcl::optional<MatchingJob>{}));
      }

      wcl::log::error("Cache::read(): daemon exited without responding");
      return wcl::result_error<FindJobResponse>(FindJobError::NoResponse);
    }

    if (messages.size() > 1) {
      wcl::log::info("message.size() == %lu", messages.size());
      for (const auto &message : messages) {
        wcl::log::info("message.size() = %lu, message = '%s'", message.size(), message.c_str());
      }
      wcl::log::error("Cache::read(): daemon responded with too many results");
      return wcl::result_error<FindJobResponse>(FindJobError::TooManyResponses);
    }

    // We have a singular valid response
    if (messages.size() == 1) {
      break;
    }

    // MessageParser tries to avoid this but we should defend against
    // the case where no error has yet occured but messages is still empty.
  } while (state == MessageParserState::Continue);

  wcl::log::info("Cache::read(): message rx: %s", messages[0].c_str());

  JAST json;
  std::stringstream parseErrors;
  if (!JAST::parse(messages[0], parseErrors, json)) {
    wcl::log::error("Cache::read(): failed to parse daemon response");
    return wcl::result_error<FindJobResponse>(FindJobError::FailedParseResponse);
  }

  return wcl::result_value<FindJobError>(FindJobResponse(json));
}

FindJobResponse Cache::read(const FindJobRequest &find_request) {
  for (int i = 0; i < 10; i++) {
    auto response = read_impl(find_request);
    if (response) {
      return *response;
    }

    // Retry
    wcl::log::info("Relaunching the daemon.");
    launch_daemon();

    wcl::log::info("Reconnecting to daemon.");
    backoff_try_connect(10);
  }

  // TODO: Add config var to determine if fail is a cache miss
  if (true) {
    wcl::log::fatal("Cache::read(): Failed to read from daemon cache.");
  }

  return FindJobResponse(wcl::optional<MatchingJob>{});
}

void Cache::add(const AddJobRequest &add_request) {
  // serialize the request, send it, deserialize the response, return it
  JAST request(JSON_OBJECT);
  request.add("method", "cache/add");
  request.add("params", add_request.to_json());

  // serialize the request and send it
  send_json_message(socket_fd.get(), request);
}

}  // namespace job_cache
