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
  // critical path stats:
  double remain;
  double total;
  double current;
  typedef std::list<Status>::iterator Handle;

  StatusState() : remain(0), total(0), current(0), jobs() { }
  Handle add(const std::string &cmdline, double budget, const struct timeval &launch);
  void remove(Handle sh);

  const std::list<Status> &getJobs() const { return jobs; }

private:
  std::list<Status> jobs;
};

extern StatusState status_state;

void status_init();
void status_write(int fd, const char *data, int len);
void status_refresh(bool idle);
void status_finish();

void term_init(bool tty, bool bsp);
const char *term_red();
const char *term_normal();

#endif
