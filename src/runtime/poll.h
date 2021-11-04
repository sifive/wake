/*
 * Copyright 2021 SiFive, Inc.
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

#ifndef POLL_H
#define POLL_H

#include <sys/select.h>

#include <vector>
#include <memory>

struct Poll {
  struct detail;
  std::unique_ptr<detail> imp;

  Poll();
  ~Poll();

  void add(int fd);
  void remove(int fd);
  void clear();

  std::vector<int> wait(struct timespec *timeout, sigset_t *saved);

  int max_fds() const;
};

#endif
