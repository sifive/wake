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

#include "tracing.h"

#include <ctime>
#include <vector>

#include "defer.h"

namespace wcl {
namespace log {

static std::vector<std::unique_ptr<Subscriber>> subscribers;

void subscribe(std::unique_ptr<Subscriber> subscriber) {
  subscribers.emplace_back(std::move(subscriber));
}

void clear_subscribers() { subscribers.clear(); }

Event event() {
  Event e({});
  return e;
}

Event Event::message(const char* fmt, va_list args) && {
  va_list copy;
  va_copy(copy, args);

  size_t size = vsnprintf(NULL, 0, fmt, copy);

  std::vector<char> buffer(size + 1);
  vsnprintf(buffer.data(), buffer.size(), fmt, args);

  std::string out = buffer.data();
  out += '\n';

  items[LOG_MESSAGE] = std::move(out);
  return std::move(*this);
}

Event Event::message(const char* fmt, ...) && {
  va_list args;
  va_start(args, fmt);
  auto defer = make_defer([&]() { va_end(args); });
  return std::move(*this).message(fmt, args);
}

Event Event::urgent() && {
  items[URGENT] = "1";
  return std::move(*this);
}

Event Event::time() && {
  time_t t = ::time(NULL);
  struct tm tm = *localtime(&t);
  char time_buffer[20 + 1];
  strftime(time_buffer, sizeof(time_buffer), "%F %T", &tm);
  items[LOG_TIME] = time_buffer;

  return std::move(*this);
}

Event Event::pid() && {
  items[LOG_PID] = std::to_string(getpid());
  return std::move(*this);
}

Event Event::info() && {
  items[LOG_LEVEL] = LOG_LEVEL_INFO;
  return std::move(*this);
}

Event Event::warning() && {
  items[LOG_LEVEL] = LOG_LEVEL_WARNING;
  return std::move(*this);
}

Event Event::error() && {
  items[LOG_LEVEL] = LOG_LEVEL_ERROR;
  return std::move(*this);
}

void Event::operator()() && {
  for (const auto& subscriber : subscribers) {
    subscriber->receive(*this);
  }
}

Event info(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  auto defer = make_defer([&]() { va_end(args); });

  return event().info().pid().time().message(fmt, args);
}

Event warning(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  auto defer = make_defer([&]() { va_end(args); });

  return event().warning().pid().time().message(fmt, args);
}

Event error(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  auto defer = make_defer([&]() { va_end(args); });

  return event().error().pid().time().message(fmt, args);
}

void FormatSubscriber::receive(const Event& e) {
  s << "[";

  bool insert_sep = false;
  for (const auto& item : e.items) {
    if (item.first == LOG_MESSAGE) {
      continue;
    }

    if (insert_sep) {
      s << ", ";
    }
    insert_sep = true;

    s << item.first << "=" << item.second;
  }

  s << "]";

  if (auto* value = e.get(LOG_MESSAGE)) {
    s << " " << *value;
  }
}

}  // namespace log
}  // namespace wcl
