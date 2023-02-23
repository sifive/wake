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

// Open Group Base Specifications Issue 7
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <algorithm>
#include <iostream>
#include <memory>
#include <vector>

#include "command.h"
#include "eviction-policy.h"
#include "gopt/gopt-arg.h"
#include "gopt/gopt.h"

void print_help(const char* argv0) {
  // clang-format off
  std::cerr << std::endl
     << "Usage: " << argv0 << " [OPTIONS]" << std::endl
     << "  --cache  DIR     Evict from shared cache DIR"  << std::endl
     << "  --policy POLICY  Evict using POLICY"           << std::endl
     << "  --help   -h      Print this message and exit"  << std::endl
     << "Commands (read from stdin):" << std::endl
     << "  write JOB_ID     JOB_ID was written into the shared cache" << std::endl
     << "  read JOB_ID      JOB_ID was read from the shared cache" << std::endl
     << "Available Policies:" << std::endl
     << "  nil              No op policy. Process commands but do nothing." << std::endl
     << std::endl;
  // clang-format on
}

std::unique_ptr<EvictionPolicy> make_policy(const char* argv0, const char* policy) {
  if (strcmp(policy, "nil") == 0) {
    return std::make_unique<NilEvictionPolicy>();
  }

  std::cerr << "Unknown policy: " << policy << std::endl;
  print_help(argv0);
  exit(EXIT_FAILURE);
}

bool stdstreams_still_open() {
  int status = fcntl(STDIN_FILENO, F_GETFD);

  if (status == -1 && errno == EBADF) {
    return false;
  }

  status = fcntl(STDOUT_FILENO, F_GETFD);

  if (status == -1 && errno == EBADF) {
    return false;
  }

  return true;
}

struct CommandParser {
  std::string command_buff = "";

  std::vector<std::string> read_commands() {
    std::vector<std::string> commands = {};
    while (stdstreams_still_open()) {
      uint8_t buffer[4096] = {};

      ssize_t count = read(STDIN_FILENO, static_cast<void*>(buffer), 4096);

      // Nothing new to process, yield control
      if (count == 0) {
        return commands;
      }

      // An error occured during read
      if (count < 0) {
        std::cerr << "Failed to read from stdin: " << strerror(errno) << std::endl;
        exit(EXIT_FAILURE);
      }

      // do the stuff
      uint8_t* iter = buffer;
      uint8_t* buffer_end = buffer + count;
      while (iter < buffer_end) {
        auto end = std::find(iter, buffer_end, 0);
        command_buff.append(iter, end);
        if (end != buffer_end) {
          commands.emplace_back(std::move(command_buff));
          command_buff = "";
        }
        iter = end + 1;
      }

      // last read consumed the buffer, yield control
      if (count < 4096) {
        return commands;
      }

      // yield if 100 commands have been buffered up without yielding
      if (commands.size() > 100) {
        return commands;
      }
    }
    // stdin or stdout were closed. Exit cleanly
    exit(EXIT_SUCCESS);
  }
};

int main(int argc, char** argv) {
  // clang-format off
  struct option options[] {
    {0, "cache", GOPT_ARGUMENT_REQUIRED},
    {0, "policy", GOPT_ARGUMENT_REQUIRED},
    {'h', "help", GOPT_ARGUMENT_FORBIDDEN},
    {0, 0, GOPT_LAST}
  };
  // clang-format on

  argc = gopt(argv, options);
  gopt_errors(argv[0], options);

  bool help = arg(options, "help")->count;
  const char* cache = arg(options, "cache")->argument;
  const char* policy_name = arg(options, "policy")->argument;

  if (help) {
    print_help(argv[0]);
    exit(EXIT_SUCCESS);
  }

  if (!cache) {
    std::cerr << "Cache directory not specified" << std::endl;
    print_help(argv[0]);
    exit(EXIT_FAILURE);
  }

  if (!policy_name) {
    std::cerr << "Eviction policy not specified" << std::endl;
    print_help(argv[0]);
    exit(EXIT_FAILURE);
  }

  std::unique_ptr<EvictionPolicy> policy = make_policy(argv[0], policy_name);
  policy->init();

  CommandParser cmd_parser;
  while (true) {
    const std::vector<std::string> cmds = cmd_parser.read_commands();
    for (const auto& c : cmds) {
      Command cmd;
      if (!Command::parse(c, cmd)) {
        exit(EXIT_FAILURE);
      }

      switch (cmd.type) {
        case CommandType::Read:
          policy->read(cmd.job_id);
          break;
        case CommandType::Write:
          policy->write(cmd.job_id);
          break;
        default:
          std::cerr << "Unhandled command type" << std::endl;
          exit(EXIT_FAILURE);
      }
    }
  }

  exit(EXIT_SUCCESS);
}
