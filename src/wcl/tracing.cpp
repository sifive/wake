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

void publish(Event e) {
  for (const auto& subscriber : subscribers) {
    subscriber->receive(e);
  }
}

static void log_message(const char* level,
                        std::initializer_list<std::pair<const std::string, std::string>> list,
                        const char* fmt, va_list args) {
  va_list copy;
  va_copy(copy, args);

  size_t size = vsnprintf(NULL, 0, fmt, copy);

  std::vector<char> buffer(size + 1);
  vsnprintf(buffer.data(), buffer.size(), fmt, args);

  std::string out = buffer.data();
  out += '\n';

  time_t t = time(NULL);
  struct tm tm = *localtime(&t);
  char time_buffer[20 + 1];
  strftime(time_buffer, sizeof(time_buffer), "%F %T", &tm);

  Event e(list);
  e.items[LOG_LEVEL] = level;
  e.items[LOG_MESSAGE] = std::move(out);
  e.items[LOG_PID] = std::to_string(getpid());
  e.items[LOG_TIME] = time_buffer;

  publish(e);
}

void info(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  auto defer = make_defer([&]() { va_end(args); });
  log_message(LOG_LEVEL_INFO, {}, fmt, args);
}

void info(std::initializer_list<std::pair<const std::string, std::string>> list, const char* fmt,
          ...) {
  va_list args;
  va_start(args, fmt);
  auto defer = make_defer([&]() { va_end(args); });
  log_message(LOG_LEVEL_INFO, list, fmt, args);
}

void warning(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  auto defer = make_defer([&]() { va_end(args); });
  log_message(LOG_LEVEL_WARNING, {}, fmt, args);
}

void warning(std::initializer_list<std::pair<const std::string, std::string>> list, const char* fmt,
             ...) {
  va_list args;
  va_start(args, fmt);
  auto defer = make_defer([&]() { va_end(args); });
  log_message(LOG_LEVEL_WARNING, list, fmt, args);
}

void error(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  auto defer = make_defer([&]() { va_end(args); });
  log_message(LOG_LEVEL_ERROR, {}, fmt, args);
}

void error(std::initializer_list<std::pair<const std::string, std::string>> list, const char* fmt,
           ...) {
  va_list args;
  va_start(args, fmt);
  auto defer = make_defer([&]() { va_end(args); });
  log_message(LOG_LEVEL_ERROR, list, fmt, args);
}

void fatal(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  auto defer = make_defer([&]() { va_end(args); });
  log_message(LOG_LEVEL_FATAL, {}, fmt, args);
  ::exit(1);
}

void fatal(std::initializer_list<std::pair<const std::string, std::string>> list, const char* fmt,
           ...) {
  va_list args;
  va_start(args, fmt);
  auto defer = make_defer([&]() { va_end(args); });
  log_message(LOG_LEVEL_FATAL, list, fmt, args);
  ::exit(1);
}

void exit(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  auto defer = make_defer([&]() { va_end(args); });
  log_message(LOG_LEVEL_EXIT, {}, fmt, args);
  ::exit(0);
}

void exit(std::initializer_list<std::pair<const std::string, std::string>> list, const char* fmt,
          ...) {
  va_list args;
  va_start(args, fmt);
  auto defer = make_defer([&]() { va_end(args); });
  log_message(LOG_LEVEL_EXIT, list, fmt, args);
  ::exit(0);
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
