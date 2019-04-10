#include "status.h"
#include "job.h"
#include <sstream>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <signal.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <math.h>
#include <curses.h>
#include <term.h>
#include <string.h>
#include <stdio.h>

std::list<Status> status_state;

static volatile bool refresh_needed = false;
static volatile bool resize_detected = false;

static bool tty = false;
static int rows = 0, cols = 0;
static const char *cuu1;
static const char *cr;
static const char *ed;
static const char *sgr0;
static int used = 0;

const char *term_red()
{
  static char setaf_lit[] = "setaf";
  char *out;
  if (!sgr0) return "";
  out = tigetstr(setaf_lit);
  if (!out || out == (char*)-1) return "";
  out = tparm(out, 1); // red
  if (!out || out == (char*)-1) return "";
  return out;
}

const char *term_normal()
{
  return sgr0?sgr0:"";
}

static void write_all(int fd, const char *data, size_t len)
{
  ssize_t got;
  size_t done;
  for (done = 0; !JobTable::exit_now() && done < len; done += got) {
    got = write(fd, data+done, len-done);
    if (done < 0 && errno != EINTR) break;
  }
}

static void status_clear()
{
  if (tty) {
    std::stringstream os;
    for (; used; --used) os << cuu1;
    os << cr;
    os << ed;
    std::string s = os.str();
    write_all(2, s.data(), s.size());
  }
}

static int ilog10(int x)
{
  int out = 1;
  for (; x >= 10; x /= 10) ++out;
  return out;
}

static void status_redraw()
{
  std::stringstream os;
  struct timeval now;
  gettimeofday(&now, 0);

  refresh_needed = false;
  if (resize_detected) {
    resize_detected = false;
    struct winsize size;
    if (ioctl(2, TIOCGWINSZ, &size) == 0) {
      rows = size.ws_row;
      cols = size.ws_col;
    }
  }

  int total = status_state.size();
  if (tty && rows > 4 && cols > 16) for (auto &x : status_state) {
    double runtime =
      (now.tv_sec  - x.launch.tv_sec) +
      (now.tv_usec - x.launch.tv_usec) / 1000000.0;

    int rest = cols - 10;
    std::string cut;
    if ((int)x.cmdline.size() < rest) {
      cut = x.cmdline;
    } else {
      cut = x.cmdline.substr(0, (rest-5)/2) + " ... " +
            x.cmdline.substr(x.cmdline.size()-(rest-4)/2);
    }

    char progress[] = "[      ] ";
    if (x.merged) {
      if (!x.stdout) {
        strcpy(progress, "[stdout] ");
      } else if (!x.stderr) {
        strcpy(progress, "[stderr] ");
      } else {
        strcpy(progress, "[merged] ");
      }
    } else if (x.budget == 0) {
      long offset = lround(floor(fmod(2*runtime, 6.0)));
      progress[offset+1] = '#';
    } else if (runtime < x.budget) {
      for (long offset = lround(floor(7*runtime/x.budget)); offset; --offset)
        progress[offset] = '#';
    } else {
      long over = lround(100.0 * runtime/x.budget);
      if (over > 99999) over = 99999;
      int len = ilog10(over);
      int wide = 5;
      snprintf(progress, sizeof(progress), "[%*d%%%*s] ", (wide+len)/2, (int)over, (wide-len+1)/2, "");
    }

    os << progress << cut << std::endl;
    ++used;
    if (used != total && used == rows - 3) {
      os << "... +" << (total-used) << " more" << std::endl;
      ++used;
      break;
    }
  }

  std::string s = os.str();
  write_all(2, s.data(), s.size());
}

static void handle_SIGALRM(int sig)
{
  (void)sig;
  refresh_needed = true;
}

static void handle_SIGWINCH(int sig)
{
  (void)sig;
  refresh_needed = true;
  resize_detected = true;
}

void term_init(bool tty_)
{
  tty = tty_;

  if (tty) {
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
    cuu1 = tigetstr(cuu1_lit);   // cursor up one row
    cr   = tigetstr(cr_lit);     // return to first column
    ed   = tigetstr(ed_lit);     // erase to bottom of display
    rows = tigetnum(lines_lit);
    cols = tigetnum(cols_lit);
    if (!cuu1 || cuu1 == (char*)-1) tty = false;
    if (!cr || cr == (char*)-1) tty = false;
    if (!ed || ed == (char*)-1) tty = false;
    if (cols < 0 || rows < 0) tty = false;
    sgr0 = tigetstr(sgr0_lit); // optional
    if (sgr0 == (char*)-1) sgr0 = 0;
  }
}

void status_init()
{
  if (tty) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));

    // watch for resize events
    sa.sa_handler = handle_SIGWINCH;
    sa.sa_flags = SA_RESTART; // interrupting pselect() is not critical for this
    sigaction(SIGWINCH, &sa, 0);
    handle_SIGWINCH(SIGWINCH);

    // Setup a SIGALRM timer to trigger status redraw
    struct itimerval timer;
    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = 1000000/6; // refresh at 6Hz
    timer.it_interval = timer.it_value;

    sa.sa_handler = handle_SIGALRM;
    sa.sa_flags = SA_RESTART; // we will truncate the pselect timeout to compensate
    sigaction(SIGALRM, &sa, 0);
    setitimer(ITIMER_REAL, &timer, 0);
  }
}

void status_write(int fd, const char *data, int len)
{
  status_clear();
  write_all(fd, data, len);
  refresh_needed = true;
}

void status_refresh()
{
  if (refresh_needed) {
    status_clear();
    status_redraw();
  }
}

void status_finish()
{
  status_clear();
  if (tty) {
    struct itimerval timer;
    memset(&timer, 0, sizeof(timer));
    setitimer(ITIMER_REAL, &timer, 0);
  }
}
