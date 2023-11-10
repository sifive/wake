/*
 * Copyright 2019 SiFive, Inc.
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

// vfork is a deprecated API in POSIX, but is defined in BSD
#define _BSD_SOURCE
#define _DEFAULT_SOURCE

#include "spawn.h"

#include <iostream>
#include <sys/types.h>
#include <unistd.h>

pid_t wake_spawn(const std::string cmd, std::vector<std::string> cmdline, std::vector<std::string> environ) {
  pid_t pid = vfork();
  if (pid == 0) {
    std::vector<char*> cmdline_c = std::vector<char*>();
    for (auto str : cmdline) {
      cmdline_c.push_back(&str.front());
    }
    cmdline_c.push_back(nullptr);
    std::vector<char*> environ_c = std::vector<char*>();
    for (auto str : environ) {
      environ_c.push_back(&str.front());
    }
    environ_c.push_back(nullptr);
    execve(cmd.c_str(), cmdline_c.data(), environ_c.data());
    _exit(127);
  }
  return pid;
}
