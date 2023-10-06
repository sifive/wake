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
  int64_t size;

  EvictionCommand() {}
  EvictionCommand(EvictionCommandType type, int64_t datum) : type(type) {
    if (type == EvictionCommandType::Read) {
      job_id = datum;
    } else {
      size = datum;
    }
  }

  static EvictionCommand make_read(int job_id) {
    return EvictionCommand(EvictionCommandType::Read, job_id);
  }

  static EvictionCommand make_write(int64_t size) {
    return EvictionCommand(EvictionCommandType::Write, size);
  }

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

    if (type == EvictionCommandType::Read) {
      const JAST& job_id = json.get("job_id");
      if (job_id.kind != JSON_INTEGER) {
        std::cerr << "Expected integer for 'job_id' key" << std::endl;
        return {};
      }
      return wcl::some(make_read(std::stoi(job_id.value)));
    } else {
      const JAST& size = json.get("size");
      if (size.kind != JSON_INTEGER) {
        std::cerr << "Expected integer for 'size' key" << std::endl;
        return {};
      }
      return wcl::some(make_write(std::stoll(size.value)));
    }
  }

  std::string serialize() {
    JAST json(JSON_OBJECT);

    switch (type) {
      case EvictionCommandType::Read:
        json.add("command", "read");
        json.add("job_id", job_id);
        break;
      case EvictionCommandType::Write:
        json.add("command", "write");
        json.add("size", size);
        break;
      default:
        std::cerr << "Unhandled type in EvictionCommand::serialize()" << std::endl;
        exit(EXIT_FAILURE);
    }

    std::stringstream s;
    s << json;
    return s.str();
  }
};
