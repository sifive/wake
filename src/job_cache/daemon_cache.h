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

#include <util/poll.h>
#include <wcl/xoshiro_256.h>

#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>

#include "job_cache.h"
#include "job_cache_impl_common.h"
#include "message_parser.h"
#include "message_sender.h"

namespace job_cache {

struct CacheDbImpl;

class DaemonCache {
 private:
  wcl::xoshiro_256 rng;
  std::unique_ptr<CacheDbImpl> impl;  // pimpl
  int evict_stdin;
  int evict_stdout;
  int evict_pid;
  EvictionConfig config;
  std::string key;
  int listen_socket_fd;
  EPoll poll;
  std::unordered_map<int, MessageParser> message_parsers;
  std::unordered_map<int, MessageSender> message_senders;
  bool exit_now = false;

  void launch_evict_loop();
  void reap_evict_loop();

  FindJobResponse read(const FindJobRequest &find_request);
  void add(const AddJobRequest &add_request);
  void remove_corrupt_job(int64_t job_id);
  void close_client(int client_fd);

  void handle_new_client();
  void handle_read_msg(int fd);
  void handle_write(int fd);

 public:
  ~DaemonCache();

  DaemonCache() = delete;
  DaemonCache(const DaemonCache &) = delete;

  DaemonCache(std::string dir, std::string bulk_logging_dir, EvictionConfig config);

  int run();
};

}  // namespace job_cache
