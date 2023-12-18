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

#include <json/json5.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <util/execpath.h>
#include <wcl/defer.h>
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
#include "types.h"

namespace job_cache {

// Helper to replace an existing file handle with
// a new one. For instance to replace stdin with
// /dev/null or stdout with a log file etc...
static void replace_fd(int old_fd, int new_fd) {
  if (dup2(new_fd, old_fd) == -1) {
    wcl::log::error("dup2: %s", strerror(errno)).urgent()();
    exit(1);
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
    wcl::log::error("fork1: %s", strerror(errno)).urgent()();
    exit(1);
  }

  if (pid != 0) {
    return false;
  }

  wcl::log::info("fork1: success")();

  {
    // Replace stdin with /dev/null so we can't receive input
    auto null_fd = wcl::unique_fd::open("/dev/null", O_RDONLY);
    if (!null_fd) {
      wcl::log::error("open(%s): %s", "/dev/null", strerror(null_fd.error())).urgent()();
      exit(1);
    }
    replace_fd(STDIN_FILENO, null_fd->get());
  }

  {
    // Replace stdout with dir/.stdout
    std::string log_path = dir + "/.stdout";
    auto log_fd = wcl::unique_fd::open(log_path.c_str(), O_CREAT | O_RDWR | O_APPEND, 0644);
    if (!log_fd) {
      wcl::log::error("open(%s): %s", log_path.c_str(), strerror(log_fd.error())).urgent()();
      exit(1);
    }
    replace_fd(STDOUT_FILENO, log_fd->get());
  }

  {
    // Replace stderr with dir/.stderr
    std::string error_log_path = dir + "/.stderr";
    auto err_log_fd = wcl::unique_fd::open(error_log_path.c_str(), O_CREAT | O_RDWR | O_APPEND,
                                           0644);  // S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (!err_log_fd) {
      wcl::log::error("open(%s): %s", error_log_path.c_str(), strerror(err_log_fd.error()))
          .urgent()();
      exit(1);
    }
    replace_fd(STDERR_FILENO, err_log_fd->get());
  }

  // setsid so we're in our own group
  int sid = setsid();
  if (sid == -1) {
    wcl::log::error("setsid: %s", strerror(errno)).urgent()();
    exit(1);
  }

  // second fork so we can't undo any of the above
  pid = fork();
  if (pid == -1) {
    wcl::log::error("fork2: %s", strerror(errno)).urgent()();
    exit(1);
  }

  if (pid != 0) {
    // Exit cleanly from orig parent
    wcl::log::info("fork2: success")();
    exit(0);
  }

  wcl::log::info("Daemon successfully created: sid = %d", sid)();
  return true;
}

wcl::optional<wcl::unique_fd> try_connect(std::string dir) {
  wcl::unique_fd socket_fd;
  {
    int local_socket_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (local_socket_fd == -1) {
      wcl::log::error("socket(AF_UNIX, ..., 0): %s\n", strerror(errno)).urgent()();
      exit(1);
    }
    socket_fd = wcl::unique_fd(local_socket_fd);
  }

  std::string key_path = dir + "/.key";

  char key[33] = {0};
  auto fd = wcl::unique_fd::open(key_path.c_str(), O_RDONLY);
  if (!fd) {
    wcl::log::info("open(%s): %s", key_path.c_str(), strerror(fd.error()))();
    return {};
  }

  // TODO: We should make this read more robust. It's mostly fine if
  //       it returns fewer bytes than we asked for but if it gives
  //       us EINTR that could cause some strange failures that should
  //       be avoided. That's quite unlikely however.
  if (::read(fd->get(), key, sizeof(key)) == -1) {
    wcl::log::error("read(%s): %s", key_path.c_str(), strerror(errno)).urgent()();
    exit(1);
  }

  sockaddr_un addr = {0};
  addr.sun_family = AF_UNIX;
  addr.sun_path[0] = '\0';

  wcl::log::info("key = %s, sizeof(key) = %lu", key, sizeof(key))();
  memcpy(addr.sun_path + 1, key, sizeof(key));
  if (connect(socket_fd.get(), reinterpret_cast<const sockaddr *>(&addr), sizeof(key)) == -1) {
    wcl::log::info("connect(%s): %s", key, strerror(errno))();
    return {};
  }

  return wcl::make_some<wcl::unique_fd>(std::move(socket_fd));
}

// Launch the job cache daemon
void Cache::launch_daemon() {
  wcl::log::info("Relaunching the daemon.")();
  // We are the daemon, launch the cache
  if (daemonize(cache_dir.c_str())) {
    std::string job_cache = wcl::make_canonical(find_execpath() + "/../bin/job-cache");
    switch (config.type) {
      case EvictionPolicyType::LRU: {
        std::string low_str = std::to_string(config.lru.low_size);
        std::string max_str = std::to_string(config.lru.max_size);
        execl(job_cache.c_str(), "job-cached", "--cache-dir", cache_dir.c_str(),
              "--bulk-logging-dir", bulk_logging_dir.c_str(), "--eviction-type", "lru",
              "--low-cache-size", low_str.c_str(), "--max-cache-size", max_str.c_str(), nullptr);
      } break;
      case EvictionPolicyType::TTL: {
        std::string ttl = std::to_string(config.ttl.seconds_to_live);
        execl(job_cache.c_str(), "job-cached", "--cache-dir", cache_dir.c_str(),
              "--bulk-logging-dir", bulk_logging_dir.c_str(), "--eviction-type", "ttl",
              "--seconds-to-live", ttl.c_str(), nullptr);
      } break;
    }

    wcl::log::error("exec(%s): %s", job_cache.c_str(), strerror(errno)).urgent()();
    exit(1);
  }
}

// Connect to the job cache daemon with backoff.
wcl::result<wcl::unique_fd, ConnectError> Cache::backoff_try_connect(int attempts) {
  wcl::xoshiro_256 rng(wcl::xoshiro_256::get_rng_seed());
  useconds_t backoff = 1000;
  wcl::unique_fd socket_fd;
  for (int i = 0; i < attempts; i++) {
    // We normally connect in about 3 tries, sometimes 4 on fresh
    // connect so if we haven't connected at this point its a good
    // spot to start start trying.
    if (i > 4) {
      launch_daemon();
    }

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
    return wcl::make_error<wcl::unique_fd, ConnectError>(ConnectError::TooManyAttempts);
  }

  return wcl::make_result<wcl::unique_fd, ConnectError>(std::move(socket_fd));
}

template <class Iter>
static void mkdir_all(std::string acc, Iter begin, Iter end) {
  for (; begin != end; ++begin) {
    acc += *begin + "/";
    mkdir_no_fail(acc.c_str());
  }
}

Cache::Cache(std::string dir, std::string bulk_dir, EvictionConfig cfg, TimeoutConfig tcfg,
             bool miss) {
  cache_dir = dir;
  bulk_logging_dir = bulk_dir;
  miss_on_failure = miss;
  config = cfg;
  timeout_config = tcfg;

  auto fp_range = wcl::make_filepath_range_ref(cache_dir);
  mkdir_all(wcl::is_relative(cache_dir) ? "" : "/", fp_range.begin(), fp_range.end());

  launch_daemon();
}

wcl::result<FindJobResponse, FindJobError> Cache::read_impl(const FindJobRequest &find_request) {
  JAST request(JSON_OBJECT);
  request.add("method", "cache/read");
  request.add("params", find_request.to_json());

  // serialize the request, send it, deserialize the response, return it
  auto socket_fd = backoff_try_connect(timeout_config.connect_retries);
  if (!socket_fd) {
    return wcl::result_error<FindJobResponse>(FindJobError::CouldNotConnect);
  }
  auto write_error =
      sync_send_json_message(socket_fd->get(), request, timeout_config.message_timeout_seconds);

  if (write_error) {
    return wcl::result_error<FindJobResponse>(FindJobError::FailedRequest);
  }

  // Read the message with a 10 second timeout.
  auto messages_res = sync_read_message(socket_fd->get(), timeout_config.message_timeout_seconds);

  if (!messages_res) {
    if (messages_res.error() == SyncMessageReadError::Fail) {
      wcl::log::error("Cache::read(): failed receiving message")();
      return wcl::result_error<FindJobResponse>(FindJobError::FailedMessageReceive);
    }

    if (messages_res.error() == SyncMessageReadError::Timeout) {
      wcl::log::error("Cache::read(): timed out reading from the cache daemon")();
      return wcl::result_error<FindJobResponse>(FindJobError::Timeout);
    }

    assert(false);
  }

  std::vector<std::string> messages = std::move(*messages_res);

  if (messages.size() == 0) {
    wcl::log::error("Cache::read(): daemon exited without responding")();
    return wcl::result_error<FindJobResponse>(FindJobError::NoResponse);
  }

  if (messages.size() > 1) {
    wcl::log::info("message.size() == %lu", messages.size())();
    for (const auto &message : messages) {
      wcl::log::info("message.size() = %lu, message = '%s'", message.size(), message.c_str())();
    }
    wcl::log::error("Cache::read(): daemon responded with too many results")();
    return wcl::result_error<FindJobResponse>(FindJobError::TooManyResponses);
  }

  wcl::log::info("Cache::read(): message rx")();

  JAST json;
  std::stringstream parseErrors;
  if (!JAST::parse(messages[0], parseErrors, json)) {
    wcl::log::error("Cache::read(): failed to parse daemon response")();
    return wcl::result_error<FindJobResponse>(FindJobError::FailedParseResponse);
  }

  return wcl::result_value<FindJobError>(FindJobResponse(json));
}

static int misses_from_failure = 0;

FindJobResponse Cache::read(const FindJobRequest &find_request) {
  if (misses_from_failure > timeout_config.max_misses_from_failure) {
    return FindJobResponse(wcl::optional<MatchingJob>{});  
  }

  wcl::log::info("Cache::read enter")();
  auto defer = wcl::make_defer([]() { wcl::log::info("Cache::read exit")(); });

  wcl::xoshiro_256 rng(wcl::xoshiro_256::get_rng_seed());
  useconds_t backoff = 1000;

  bool failed_on_connect = false;
  for (int i = 0; i < timeout_config.read_retries; i++) {
    auto response = read_impl(find_request);
    if (response) {
      wcl::log::info("Returning job response: cache_hit = %d", int(bool(response->match)))();
      return *response;
    }

    failed_on_connect |= response.error() == FindJobError::CouldNotConnect;

    if (miss_on_failure && misses_from_failure > timeout_config.max_misses_from_failure) {
      wcl::log::warning(
          "Cache::read(): reached maximum cache misses for this invocation. Triggering early "
          "miss.")();
      return FindJobResponse(wcl::optional<MatchingJob>{});
    }

    std::uniform_int_distribution<useconds_t> variance(0, backoff);
    usleep(backoff + variance(rng));
    backoff *= 2;

    // Make sure the daemon is actully launched
    wcl::log::info("Ensuring daemon is alive by attempting to launch it")();
    launch_daemon();
  }

  if (failed_on_connect) {
    wcl::log::error("Cache::read(): at least one connect failure occured")();
  }

  wcl::log::error("Cache::read(): Failed to read from daemon cache.").urgent()();

  if (miss_on_failure) {
    misses_from_failure++;
    return FindJobResponse(wcl::optional<MatchingJob>{});
  }

  exit(1);
}

void Cache::add(const AddJobRequest &add_request) {
  if (misses_from_failure > timeout_config.max_misses_from_failure) {
    return;  
  }
  wcl::log::info("Cache::add enter")();
  auto defer = wcl::make_defer([]() { wcl::log::info("Cache::add exit")(); });
  // serialize the request, send it, deserialize the response, return it
  JAST request(JSON_OBJECT);
  request.add("method", "cache/add");
  request.add("params", add_request.to_json());

  // serialize the request and send it, we ignore an error
  // if it occurs here and we keep moving.
  auto socket_fd = backoff_try_connect(timeout_config.connect_retries);
  if (!socket_fd) {
    wcl::log::error("Cache::add(): Failed to connect")();
    return;
  }
  sync_send_json_message(socket_fd->get(), request, timeout_config.message_timeout_seconds);
}

}  // namespace job_cache
