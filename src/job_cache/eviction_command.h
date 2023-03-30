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

#pragma once

// Open Group Base Specifications Issue 7
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <iostream>
#include <sstream>
#include <string>

#include "json/json5.h"

enum class EvictionCommandType { Read, Write };

struct EvictionCommand {
  EvictionCommandType type;
  int job_id;

  EvictionCommand() {}
  EvictionCommand(EvictionCommandType type, int job_id) : type(type), job_id(job_id) {}

  static wcl::optional<EvictionCommand> parse(const std::string& str) {
    JAST json;
    std::stringstream parse_errors;
    if (!JAST::parse(str, parse_errors, json)) {
      std::cerr << "Failed to parse json command: " << parse_errors.str() << std::endl;
      return {};
    }

    const JAST& command = json.get("command");
    if (command.kind != JSON_STR) {
      std::cerr << "Expected string for 'command' key" << std::endl;
      return {};
    }

    const std::string& command_str = command.value;

    EvictionCommandType type;
    if (command_str == "read") {
      type = EvictionCommandType::Read;
    } else if (command_str == "write") {
      type = EvictionCommandType::Write;
    } else {
      std::cerr << "Invalid value for 'command' key. Expected: 'read' | 'write', saw "
                << command_str << std::endl;
      return {};
    }

    const JAST& job_id = json.get("job_id");
    if (job_id.kind != JSON_INTEGER) {
      std::cerr << "Expected integer for 'job_id' key" << std::endl;
      return {};
    }

    return wcl::some(EvictionCommand{type, std::stoi(job_id.value)});
  }

  std::string serialize() {
    JAST json(JSON_OBJECT);

    std::string command;
    switch (type) {
      case EvictionCommandType::Read:
        command = "read";
        break;
      case EvictionCommandType::Write:
        command = "write";
        break;
      default:
        std::cerr << "Unhandled type in EvictionCommand::serialize()" << std::endl;
        exit(EXIT_FAILURE);
    }

    json.add("command", command);
    json.add("job_id", job_id);

    std::stringstream s;
    s << json;
    return s.str();
  }
};
