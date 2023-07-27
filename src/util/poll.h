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
#include <sys/epoll.h>

#include <memory>
#include <vector>

// Poll is an wrapper around epoll on linux but also works on
// other operating systems. Today we only support linux but
// in the past we supported other kernels. This is still the
// the polling struct used throughout wake. It only allows
// for read level-triggered polling.
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

// EPoll is a more advanced wrapper around the epoll interface.
// It explicitly uses types from the linux epoll interface.
// It allows for read and write polling as well as level
// or edge triggered polling. It's just a nice wrapper around
// epoll.
struct EPoll {
  int epfd;

  EPoll();
  ~EPoll();

  void add(int fd, uint32_t events);
  void remove(int fd);
  std::vector<epoll_event> wait(struct timespec *timeout, sigset_t *saved);
};

#endif
