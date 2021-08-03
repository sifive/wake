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
  struct timeval launch;
  Status(const std::string &cmdline_, double budget_, const struct timeval &launch_)
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

void term_init(bool tty);

#define TERM_DEFAULT	0

#define TERM_BLACK	(8+0)
#define TERM_RED	(8+1)
#define TERM_GREEN	(8+2)
#define TERM_YELLOW	(8+3)
#define TERM_BLUE	(8+4)
#define TERM_MAGENTA	(8+5)
#define TERM_CYAN	(8+6)
#define TERM_WHITE	(8+7)

#define TERM_DIM	(16*1)
#define TERM_BRIGHT	(16*2)

const char *term_colour(int code);
const char *term_normal();

#endif
