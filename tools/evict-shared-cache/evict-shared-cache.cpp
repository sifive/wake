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
#include "util/poll.h"

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

enum class CommandParserState { Continue, StopSuccess, StopFail };

struct CommandParser {
  std::string command_buff = "";
  Poll poll;

  CommandParser() { poll.add(STDIN_FILENO); }

  CommandParserState read_commands(std::vector<std::string>& commands) {
    commands = {};

    sigset_t saved;
    struct timespec timeout;
    timeout.tv_sec = 1;

    // Sleep until timeout or a signal arrives
    std::vector<int> ready_fds = poll.wait(&timeout, &saved);

    // Nothing is ready within timeout, yield control
    if (ready_fds.size() == 0) {
      return CommandParserState::Continue;
    }

    while (true) {
      uint8_t buffer[4096] = {};

      ssize_t count = read(STDIN_FILENO, static_cast<void*>(buffer), 4096);

      // Nothing new to process, yield control
      if (count == 0) {
        return CommandParserState::Continue;
      }

      // An error occured during read
      if (count < 0) {
        // EBADF means that stdin was closed. This is the signal to stop
        // processing commands.
        if (errno == EBADF) {
          return CommandParserState::StopSuccess;
        }

        std::cerr << "Failed to read from stdin: " << strerror(errno) << std::endl;
        return CommandParserState::StopFail;
      }

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
        return CommandParserState::Continue;
      }

      // yield if 100 commands have been buffered up without yielding
      if (commands.size() > 100) {
        return CommandParserState::Continue;
      }
    }

    // not actually reachable
    return CommandParserState::Continue;
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
  CommandParserState state;
  do {
    std::vector<std::string> cmds;
    state = cmd_parser.read_commands(cmds);

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
  } while (state == CommandParserState::Continue);

  exit(state == CommandParserState::StopSuccess ? EXIT_SUCCESS : EXIT_FAILURE);
}
