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

// Open Group Base Specifications Issue 7
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <cstdarg>
#include <memory>
#include <ostream>
#include <string>
#include <unordered_map>

#include "optional.h"

namespace wcl {
namespace log {

static constexpr const char* LOG_LEVEL = "level";
static constexpr const char* LOG_TIME = "time";
static constexpr const char* LOG_PID = "pid";
static constexpr const char* LOG_LEVEL_INFO = "info";
static constexpr const char* LOG_LEVEL_WARNING = "warning";
static constexpr const char* LOG_LEVEL_ERROR = "error";
static constexpr const char* LOG_LEVEL_FATAL = "fatal";
static constexpr const char* LOG_LEVEL_EXIT = "exit";
static constexpr const char* LOG_MESSAGE = "message";

struct Event {
  std::unordered_map<std::string, std::string> items;

  Event(std::initializer_list<std::pair<const std::string, std::string>> list) : items(list) {}

  const std::string* get(const std::string& key) const {
    auto it = items.find(key);
    if (it == items.end()) {
      return nullptr;
    }

    return &it->second;
  }
};

// Abstract
class Subscriber {
 public:
  virtual void receive(const Event& e) = 0;
  virtual ~Subscriber(){};
};

class FormatSubscriber : public Subscriber {
 private:
  std::ostream s;

 public:
  FormatSubscriber(std::streambuf* rdbuf) : s(rdbuf) {}
  void receive(const Event& e) override;
  ~FormatSubscriber() override{};
};

class FatalEventSubscriber : public Subscriber {
 private:
  std::ostream s;

 public:
  FatalEventSubscriber(std::streambuf* rdbuf) : s(rdbuf) {}
  void receive(const Event& e) override;
  ~FatalEventSubscriber() override{};
};

void subscribe(std::unique_ptr<Subscriber>);
void clear_subscribers();

void publish(Event e);

void info(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void info(std::initializer_list<std::pair<const std::string, std::string>> list, const char* fmt,
          ...) __attribute__((format(printf, 2, 3)));

void warning(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void warning(std::initializer_list<std::pair<const std::string, std::string>> list, const char* fmt,
             ...) __attribute__((format(printf, 2, 3)));

void error(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void error(std::initializer_list<std::pair<const std::string, std::string>> list, const char* fmt,
           ...) __attribute__((format(printf, 2, 3)));

void fatal(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void fatal(std::initializer_list<std::pair<const std::string, std::string>> list, const char* fmt,
           ...) __attribute__((format(printf, 2, 3)));

void exit(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void exit(std::initializer_list<std::pair<const std::string, std::string>> list, const char* fmt,
          ...) __attribute__((format(printf, 2, 3)));

}  // namespace log
}  // namespace wcl
