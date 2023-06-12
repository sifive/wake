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

#include <unistd.h>

#include <algorithm>
#include <string>
#include <vector>

namespace job_cache {

enum class MessageParserState { Continue, StopSuccess, StopFail };

struct MessageParser {
  std::string message_buff = "";
  int fd;

  MessageParser() = delete;
  MessageParser(int fd) : fd(fd) {}

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
        // Under some circumstances a connection can be closed by the client
        // in such a way that ECONNRESET is returned. This should be treated
        // as equivlient to a close which would normally appear as the count == 0
        // case above.
        if (errno == ECONNRESET) return MessageParserState::StopSuccess;
        // There are some failures that could occur that just require us to retry.
        // EINTR could occur on any fd type but EAGAIN and EWOULDBLOCK should
        // only occur if the user gave us a non-blocking socket.
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
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

      // If we get less than 4096 bytes we want to assume that the messages
      // are done and exit. If however we still haven't received a full message
      // yet, we want to keep going until the fd is closed. If we have a message
      // to return however lets return and process that instead.
      if (count < 4096 && messages.size() != 0) {
        return MessageParserState::Continue;
      }
    }

    // not actually reachable
    return MessageParserState::Continue;
  }
};

}  // namespace job_cache
