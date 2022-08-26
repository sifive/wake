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

// Open Group Base Specifications Issue 7
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include "status.h"

#ifndef __EMSCRIPTEN__

#include <assert.h>
#include <curses.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <term.h>
#include <termios.h>
#include <unistd.h>

#include <iomanip>
#include <limits>
#include <sstream>
#include <unordered_map>

#include "compat/sigwinch.h"
#include "job.h"
#include "util/colour.h"

// How often is the status updated (should be a multiple of 2 for budget=0)
#define REFRESH_HZ 6
// Processes which last less than this time do not get displayed
#define MIN_DRAW_TIME 0.2

#define ALMOST_ONE (1.0 - 2 * std::numeric_limits<double>::epsilon())

StatusState status_state;

static volatile bool refresh_needed = false;
static volatile bool spinner_update = false;
static volatile bool resize_detected = false;

static bool tty = false;
static int rows = 0, cols = 0;
static const char *cuu1;
static const char *cr;
static const char *ed;
static const char *sgr0;
static int used = 0;
static int ticks = 0;

static bool missing_termfn(const char *s) { return !s || s == (const char *)-1; }

static const char *wrap_termfn(const char *s) {
  if (missing_termfn(s)) return "";
  return s;
}

const char *term_colour(int code) {
  static char setaf_lit[] = "setaf";
  if (!sgr0) return "";
  char *format = tigetstr(setaf_lit);
  if (missing_termfn(format)) return "";
  return wrap_termfn(tparm(format, code));
}

const char *term_intensity(int code) {
  static char dim_lit[] = "dim";
  static char bold_lit[] = "bold";
  if (!sgr0) return "";
  if (code == 1) return wrap_termfn(tigetstr(dim_lit));
  if (code == 2) return wrap_termfn(tigetstr(bold_lit));
  return "";
}

const char *term_normal() { return sgr0 ? sgr0 : ""; }

static void write_all(int fd, const char *data, size_t len) {
  ssize_t got;
  size_t done;
  for (done = 0; !JobTable::exit_now() && done < len; done += got) {
    got = write(fd, data + done, len - done);
    if (got < 0 && errno != EINTR) break;
  }
}

static void write_all_str(int fd, const char *data) { write_all(fd, data, strlen(data)); }

static void status_clear() {
  if (tty && used) {
    std::stringstream os;
    for (; used; --used) os << cuu1;
    os << cr;
    os << ed;
    std::string s = os.str();
    write_all(2, s.data(), s.size());
  }
}

static int ilog10(int x) {
  int out = 1;
  for (; x >= 10; x /= 10) ++out;
  return out;
}

static void status_redraw(bool idle) {
  std::stringstream os;
  struct timespec now;
  clock_gettime(CLOCK_REALTIME, &now);

  refresh_needed = false;
  if (resize_detected) {
    resize_detected = false;
    struct winsize size;
    if (ioctl(2, TIOCGWINSZ, &size) == 0) {
      rows = size.ws_row;
      cols = size.ws_col;
    }
  }

  int total = status_state.jobs.size();
  int rows3 = rows / 3;
  int overall = status_state.remain > 0 ? 1 : 0;
  if (tty && rows3 >= 2 + overall && cols > 16)
    for (auto &x : status_state.jobs) {
      // Silence wake-internal messages like '<hash>'
      std::string msg_prefix = "'<";
      if (x.cmdline.compare(0, msg_prefix.size(), msg_prefix) == 0) continue;

      double runtime =
          (now.tv_sec - x.launch.tv_sec) + (now.tv_nsec - x.launch.tv_nsec) / 1000000000.0;

      if (x.budget < MIN_DRAW_TIME && runtime < MIN_DRAW_TIME) {
        --total;
        continue;
      }

      int rest = cols - 10;
      std::string cut;
      if ((int)x.cmdline.size() < rest) {
        cut = x.cmdline;
      } else {
        cut = x.cmdline.substr(0, (rest - 5) / 2) + " ... " +
              x.cmdline.substr(x.cmdline.size() - (rest - 4) / 2);
      }

      char progress[] = "[      ] ";
      if (x.merged) {
        if (!x.wait_stdout) {
          strcpy(progress, "[stdout] ");
        } else if (!x.wait_stderr) {
          strcpy(progress, "[stderr] ");
        } else {
          strcpy(progress, "[merged] ");
        }
      } else if (x.budget == 0) {
        long offset = lround(floor(fmod(2 * runtime, 6.0)));
        progress[offset + 1] = '#';
      } else if (runtime < x.budget) {
        for (long offset = lround(floor(7 * runtime / x.budget)); offset; --offset)
          progress[offset] = '#';
      } else {
        long over = lround(100.0 * runtime / x.budget);
        if (over > 99999) over = 99999;
        int len = ilog10(over);
        int wide = 5;
        snprintf(progress, sizeof(progress), "[%*d%%%*s] ", (wide + len) / 2, (int)over,
                 (wide - len + 1) / 2, "");
      }

      os << progress << cut << std::endl;
      ++used;
      if (used != total && used == rows3 - 1 - overall) {  // use at most 1/3 of the space
        os << "... +" << (total - used) << " more" << std::endl;
        ++used;
        break;
      }
    }

  if (tty && rows3 > 0 && cols > 6 && status_state.remain > 0) {
    std::stringstream eta;
    long seconds = lround(status_state.remain);
    if (seconds > 3600) {
      eta << (seconds / 3600);
      eta << ":" << std::setfill('0') << std::setw(2);
    }
    eta << ((seconds % 3600) / 60);
    eta << ":" << std::setfill('0') << std::setw(2);
    eta << (seconds % 60);
    auto etas = eta.str();
    long width = etas.size();

    assert(status_state.total >= status_state.remain);
    assert(status_state.current >= 0);

    double progress = status_state.total - status_state.remain;
    long hashes = lround(floor((cols - 4) * progress * ALMOST_ONE / status_state.total));
    long current = lround(floor((cols - 4) * (progress + status_state.current) * ALMOST_ONE /
                                status_state.total)) -
                   hashes;
    long spaces = cols - 5 - hashes - current;
    assert(spaces >= 0);

    os << "[";
    if (spaces >= width + 3) {
      for (; hashes; --hashes) os << "#";
      for (; current; --current) os << ".";
      spaces -= width + 2;
      for (; spaces; --spaces) os << " ";
      os << etas << "  ";
    } else if (current >= width + 4) {
      current -= width + 3;
      for (; hashes; --hashes) os << "#";
      for (; current; --current) os << ".";
      os << " " << etas << " .";
      for (; spaces; --spaces) os << " ";
    } else if (hashes >= width + 4) {
      hashes -= width + 3;
      os << "# " << etas << " ";
      for (; hashes; --hashes) os << "#";
      for (; current; --current) os << ".";
      for (; spaces; --spaces) os << " ";
    } else {
      for (; hashes; --hashes) os << "#";
      for (; current; --current) os << ".";
      for (; spaces; --spaces) os << " ";
    }
    os << "]";
    if (idle) {
      os << " ." << std::endl;
    } else {
      os << " "
         << "/-\\|"[ticks] << std::endl;
      ticks = (ticks + spinner_update) & 3;
    }
    ++used;
  } else if (tty && !idle) {
    os << std::string(std::max(cols - 2, 0), ' ') << "/-\\|"[ticks] << std::endl;
    ticks = (ticks + spinner_update) & 3;
    ++used;
  }
  spinner_update = false;

  std::string s = os.str();
  write_all(2, s.data(), s.size());
}

static void handle_SIGALRM(int sig) {
  (void)sig;
  refresh_needed = true;
  spinner_update = true;
}

static void handle_SIGWINCH(int sig) {
  (void)sig;
  refresh_needed = true;
  resize_detected = true;
}

bool term_init(bool tty_) {
  tty = tty_;

  if (tty) {
    if (isatty(1) != 1) tty = false;
    if (isatty(2) != 1) tty = false;
  }

  if (tty) {
    int eret;
    int ret = setupterm(0, 2, &eret);
    if (ret != OK) tty = false;
  }

  if (tty) {
    // tigetstr function argument is (char*) on some platforms, so we need this hack:
    static char cuu1_lit[] = "cuu1";
    static char cr_lit[] = "cr";
    static char ed_lit[] = "ed";
    static char lines_lit[] = "lines";
    static char cols_lit[] = "cols";
    static char sgr0_lit[] = "sgr0";
    cuu1 = tigetstr(cuu1_lit);  // cursor up one row
    cr = tigetstr(cr_lit);      // return to first column
    ed = tigetstr(ed_lit);      // erase to bottom of display
    rows = tigetnum(lines_lit);
    cols = tigetnum(cols_lit);
    if (missing_termfn(cuu1)) tty = false;
    if (missing_termfn(cr)) tty = false;
    if (missing_termfn(ed)) tty = false;
    if (cols < 0 || rows < 0) tty = false;
    sgr0 = tigetstr(sgr0_lit);  // optional
    if (missing_termfn(sgr0)) sgr0 = nullptr;
  }

  return tty;
}

void status_init() {
  if (tty) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));

    // watch for resize events
    sa.sa_handler = handle_SIGWINCH;
    sa.sa_flags = SA_RESTART;  // we don't interrupt the main loop for this
    sigaction(wake_SIGWINCH, &sa, 0);
    handle_SIGWINCH(wake_SIGWINCH);

    // Setup a SIGALRM timer to trigger status redraw
    struct itimerval timer;
    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = 1000000 / REFRESH_HZ;
    timer.it_interval = timer.it_value;

    sa.sa_handler = handle_SIGALRM;
    sa.sa_flags = SA_RESTART;  // we will truncate the pselect timeout to compensate
    sigaction(SIGALRM, &sa, 0);
    setitimer(ITIMER_REAL, &timer, 0);
  }
}

struct StreamSettings {
  int fd;
  int colour;
  StreamSettings() : fd(-1), colour(TERM_DEFAULT) {}
};

static std::unordered_map<std::string, StreamSettings> settings;

void status_set_colour(const char *name, int colour) { settings[name].colour = colour; }

void status_set_fd(const char *name, int fd) { settings[name].fd = fd; }

void status_set_bulk_fd(int fd, const char *streams) {
  if (!streams) return;
  const char *start = streams;
  while (*start) {
    const char *end = start;
    while (*end && *end != ',') {
      ++end;
    }
    status_set_fd(std::string(start, end - start).c_str(), fd);
    start = *end ? (end + 1) : end;
  }
}

void status_write(const char *name, const char *data, int len) {
  StreamSettings s = settings[name];
  if (s.fd != -1) {
    status_clear();
    if (s.colour != TERM_DEFAULT) {
      int colour = s.colour % 8;
      int intensity = s.colour / 16;
      if (colour != TERM_DEFAULT) write_all_str(s.fd, term_colour(colour));
      if (intensity != TERM_DEFAULT) write_all_str(s.fd, term_intensity(intensity));
    }
    write_all(s.fd, data, len);
    if (s.colour != TERM_DEFAULT) write_all_str(s.fd, term_normal());
    refresh_needed = true;
  }
}

void status_refresh(bool idle) {
  if (refresh_needed) {
    status_clear();
    status_redraw(idle);
  }
}

void status_finish() {
  status_clear();
  if (tty) {
    struct itimerval timer;
    memset(&timer, 0, sizeof(timer));
    setitimer(ITIMER_REAL, &timer, 0);
  }
}

#else
#include <emscripten/emscripten.h>

StatusState status_state;

void status_init() {}

void status_write(const char *name, const char *data, int len) {
  // clang-format off
  EM_ASM_INT({
    console.log('[' + UTF8ToString($0) + '] ' + UTF8ToString($1));
    return 0;
  }, name, data);
  // clang-format on
}

void status_refresh(bool idle) {}
void status_finish() {}

void status_set_colour(const char *name, int colour) {}
void status_set_fd(const char *name, int fd) {}
void status_set_bulk_fd(int fd, const char *streams) {}

const char *term_colour(int code) { return ""; }
const char *term_normal() { return ""; }
const char *term_normal(int code) { return ""; }
bool term_init(bool tty) { return true; }

#endif
