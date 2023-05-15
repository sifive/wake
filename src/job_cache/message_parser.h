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

// the `Cache` class provides the full interface
// the the underlying complete cache directory.
// This requires interplay between the file system and
// the database and must be carefully orchestrated. This
// class handles all those details and provides a simple
// interface.

#pragma once

#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <algorithm>
#include <string>
#include <vector>

#include <unistd.h>

namespace job_cache {

enum class MessageParserState { Continue, StopSuccess, StopFail };

struct MessageParser {
  std::string message_buff = "";
  int fd;

  MessageParser() = delete;
  MessageParser(int fd): fd(fd) {}

  MessageParserState read_messages(std::vector<std::string>& messages) {
    messages = {};

    while (true) {
      uint8_t buffer[4096] = {};
      ssize_t count = read(fd, static_cast<void*>(buffer), 4096);
      // Pipe has been closed. Stop processing
      if (count == 0) {
        return MessageParserState::StopSuccess;
      }

      // An error occured during read
      if (count < 0) {
        return MessageParserState::StopFail;
      }

      uint8_t* iter = buffer;
      uint8_t* buffer_end = buffer + count;
      while (iter < buffer_end) {
        auto end = std::find(iter, buffer_end, 0);
        message_buff.append(iter, end);
        if (end != buffer_end) {
          messages.emplace_back(std::move(message_buff));
          message_buff = "";
        }
        iter = end + 1;
      }

      if (count < 4096) {
        return MessageParserState::Continue;
      }
    }

    // not actually reachable
    return MessageParserState::Continue;
  }
};

}
