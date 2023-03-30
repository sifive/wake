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

#include "eviction_policy.h"

#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <algorithm>
#include <iostream>
#include <memory>
#include <vector>

#include "eviction_command.h"

enum class CommandParserState { Continue, StopSuccess, StopFail };

struct CommandParser {
  std::string command_buff = "";

  CommandParser() {}

  CommandParserState read_commands(std::vector<std::string>& commands) {
    commands = {};

    while (true) {
      uint8_t buffer[4096] = {};

      ssize_t count = read(STDIN_FILENO, static_cast<void*>(buffer), 4096);

      // Pipe has been closed. Stop processing
      if (count == 0) {
        return CommandParserState::StopSuccess;
      }

      // An error occured during read
      if (count < 0) {
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

      if (count < 4096) {
        return CommandParserState::Continue;
      }
    }

    // not actually reachable
    return CommandParserState::Continue;
  }
};

int eviction_loop(std::unique_ptr<EvictionPolicy> policy) {
  policy->init();

  CommandParser cmd_parser;
  CommandParserState state;
  do {
    std::vector<std::string> cmds;
    state = cmd_parser.read_commands(cmds);

    for (const auto& c : cmds) {
      auto cmd = EvictionCommand::parse(c);
      if (!cmd) {
        exit(EXIT_FAILURE);
      }

      switch (cmd->type) {
        case EvictionCommandType::Read:
          policy->read(cmd->job_id);
          break;
        case EvictionCommandType::Write:
          policy->write(cmd->job_id);
          break;
        default:
          std::cerr << "Unhandled command type" << std::endl;
          exit(EXIT_FAILURE);
      }
    }
  } while (state == CommandParserState::Continue);

  exit(state == CommandParserState::StopSuccess ? EXIT_SUCCESS : EXIT_FAILURE);
}
