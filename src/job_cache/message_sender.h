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

enum class MessageSenderState {
  Continue,
  StopSuccess,
  StopFail,
  Timeout,
};

// Message sender acts sort of like a Rust future, it holds the state needed
// to keep performing a write even if it would block. This can be thought of as
// dual to MessageParser. MessageSender will continue writing until it receives
// EAGAIN/EWOULDBLOCK, or an error occurs. Thus it is a *must* that non-blocking
// io be used. It is recomended that you use this with edge triggered polling so
// that you only reattempt the write when you have to. Otherwise you will receive
// writes that you did not want to receive.
class MessageSender {
 public:
  std::string data;
  std::string::iterator start;
  time_t deadline;
  int fd;
  MessageSenderState state = MessageSenderState::Continue;

 public:
  MessageSender() = delete;
  MessageSender(const MessageSender&) = delete;
  MessageSender(MessageSender&& sender) {
    data = std::move(sender.data);
    start = data.begin();
    deadline = sender.deadline;
    fd = sender.fd;
    state = sender.state;

    sender.start = sender.data.begin();
    sender.fd = -1;
    sender.state = MessageSenderState::StopFail;
  }

  MessageSender(std::string data_, int fd, uint64_t timeout_seconds)
      : data(std::move(data_)), fd(fd) {
    start = data.begin();
    deadline = time(nullptr) + timeout_seconds;
    state = MessageSenderState::Continue;
  }

  // errno from the call to `write` remains set if StopFail is returned
  MessageSenderState send() {
    wcl::log::info("MessageSender::send(): %d bytes left to send", int(data.end() - start))();
    if (state != MessageSenderState::Continue) {
      wcl::log::info("MessageSender::send(): state was already not continue, returning")();
      return state;
    }

    if (time(nullptr) > deadline) {
      wcl::log::info("MessageSender::send(): Timeout")();
      state = MessageSenderState::Timeout;
      return state;
    }

    while (start < data.end()) {
      int res = write(fd, &*start, data.end() - start);
      if (res == -1) {
        // signal interuptions will be rare and should just be re-tried until
        // we get EAGAIN/EWOULDBLOCK
        if (errno == EINTR) {
          continue;
        }

        // We still have more work to do but we can't do it right now.
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          wcl::log::info("MessageSender::send(): Stopping send because it would block")();
          return MessageSenderState::Continue;
        }

        wcl::log::info("MessageSender::send(): write(%d): %s", fd, strerror(errno)).urgent()();
        state = MessageSenderState::StopFail;
        return state;
      }

      wcl::log::info("MessageSender::send(): Wrote %d bytes", res)();
                     std::string(start, start + res).c_str())();
                     start += res;
    }

    state = MessageSenderState::StopSuccess;
    return state;
  }
};

}  // namespace job_cache
