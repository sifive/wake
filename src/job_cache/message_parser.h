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

enum class MessageParserState { Continue, StopSuccess, StopFail, Timeout };

// Message parser acts sort of like a Rust future, it holds the state
// needed to keep reading from a fd even if it would block. This can be
// be thought of as dual to MessageSender. MessageParser will read as many
// messages as it can until it receives EAGAIN/EWOULDBLOCK. This means that
// you *must* use non-blocking IO with it. It can be used with either edge
// triggered or level triggered events.
struct MessageParser {
  std::string message_buff = "";
  int fd;
  time_t deadline;

  MessageParser() = delete;
  MessageParser(int fd, uint64_t timeout) : fd(fd), deadline(time(nullptr) + timeout) {}

  MessageParserState read_messages(std::vector<std::string>& messages) {
    messages = {};

    time_t now = time(nullptr);
    if (now > deadline) {
      return MessageParserState::Timeout;
    }

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
        if (errno == ECONNRESET) {
          return MessageParserState::StopSuccess;
        }
        // On EINTR we should just retry until we get EAGAIN/EWOULDBLOCK
        if (errno == EINTR) {
          continue;
        }
        // If we hit EAGAIN/EWOULDBLOCK then we might have more work to do but
        // we can't do that work just yet and need to continue.
        if (errno == EAGAIN || errno == EWOULDBLOCK) return MessageParserState::Continue;

        return MessageParserState::StopFail;
      }

      // Now we split the data we've received up by null bytes
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
    }

    // not actually reachable
    return MessageParserState::StopFail;
  }
};

}  // namespace job_cache
