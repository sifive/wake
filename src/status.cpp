#include "status.h"
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

bool exit_now = false;
bool refresh_needed = false;
std::list<Status> status_state;

static bool tty = false;
static int rows = 0, cols = 0;
static const char *cuu1;
static const char *cr;
static const char *ed;
static int used = 0;

static void write_all(int fd, const char *data, size_t len)
{
  ssize_t got;
  size_t done;
  for (done = 0; done < len; done += got) {
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
  refresh_needed = false;
}

static void update_rows(int)
{
  struct winsize size;
  if (ioctl(2, TIOCGWINSZ, &size) == 0) {
    rows = size.ws_row;
    cols = size.ws_col;
  }
  status_clear();
  refresh_needed = true;
}

void status_init(bool tty_)
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
    cuu1 = tigetstr("cuu1"); // cursor up one row
    cr = tigetstr("cr");     // return to first column
    ed = tigetstr("ed");     // erase to bottom of display
    rows = tigetnum("lines");
    cols = tigetnum("cols");
    if (!cuu1 || cuu1 == (char*)-1) tty = false;
    if (!cr || cr == (char*)-1) tty = false;
    if (!ed || ed == (char*)-1) tty = false;
    if (cols < 0 || rows < 0) tty = false;
  }
  if (tty) {
    // watch for resize events
    signal(SIGWINCH, update_rows);
    update_rows(SIGWINCH);
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
  status_clear();
  status_redraw();
}

void status_finish()
{
  status_clear();
  if (tty) reset_shell_mode();
}
