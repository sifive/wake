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

#ifndef STATUS_H
#define STATUS_H

#include <list>
#include <string>
#include <sys/time.h>

struct Status {
  std::string cmdline;
  double budget;
  bool merged, wait_stdout, wait_stderr;
  struct timespec launch;
  Status(const std::string &cmdline_, double budget_, const struct timespec &launch_)
   : cmdline(cmdline_), budget(budget_),
     merged(false), wait_stdout(true), wait_stderr(true),
     launch(launch_) { }
};

struct StatusState {
  std::list<Status> jobs;
  // critical path stats:
  double remain;
  double total;
  double current;

  StatusState() : jobs(), remain(0), total(0), current(0) { }
};

extern StatusState status_state;

#define STREAM_LOG	"debug"
#define STREAM_INFO	"info"
#define STREAM_REPORT	"report"
#define STREAM_ECHO	"echo"
#define STREAM_WARNING	"warning"
#define STREAM_ERROR	"error"

void status_init();
void status_write(const char *name, const char *data, int len);
inline void status_write(const char *name, const std::string &str) { status_write(name, str.data(), str.size()); }
void status_refresh(bool idle);
void status_finish();

void status_set_colour(const char *name, int colour);
void status_set_fd(const char *name, int fd);
void status_set_bulk_fd(int fd, const char *streams);

#endif
