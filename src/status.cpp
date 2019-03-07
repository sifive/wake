#include "status.h"
#include <iostream>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <signal.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <math.h>
#include <curses.h>
#include <term.h>

bool refresh_needed = false;
std::list<Status> status_state;

static bool tty = false;
static int rows = 0, cols = 0;
static const char *cuu1;
static const char *ed;
static int used = 0;

static int eputc(int c)
{
  std::cerr << (char)c;
  return 0;
}

static void status_clear()
{
  for (; used; --used) tputs(cuu1, 1, eputc);
  tputs(ed, 1, eputc);
}

static void status_redraw()
{
  struct timeval now;
  gettimeofday(&now, 0);

  int total = status_state.size();
  if (rows > 4 && cols > 16) for (auto &x : status_state) {
    double runtime =
      (now.tv_sec  - x.launch.tv_sec) +
      (now.tv_usec - x.launch.tv_usec) / 1000000.0;

    int rest = cols - 10;
    std::string cut;
    if (x.cmdline.size() < rest) {
      cut = x.cmdline;
    } else {
      cut = x.cmdline.substr(0, (rest-5)/2) + " ... " +
            x.cmdline.substr(x.cmdline.size()-(rest-4)/2);
    }

    char progress[] = "[      ] ";
    if (x.budget == 0) {
      long offset = lround(floor(fmod(2*runtime, 6.0)));
      progress[offset+1] = '#';
    } else if (runtime < x.budget) {
      for (long offset = lround(floor(6*runtime/x.budget)); offset; --offset)
        progress[offset+1] = '#';
    } else {
      double ratio = 100.0 * runtime/x.budget;
      // !!!
    }

    std::cerr << progress << cut << std::endl;
    ++used;
    if (used != total && used == rows - 3) {
      std::cerr << "... +" << (total-used) << " more" << std::endl;
      ++used;
      break;
    }
  }

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
  status_redraw();
}

void status_init()
{
  tty = isatty(2) == 1;
  if (tty) {
    int eret;
    int ret = setupterm(0, 2, &eret);
    if (ret != OK) tty = false;
  }
  if (tty) {
    cuu1 = tigetstr("cuu1"); // cursor up one row
    ed = tigetstr("ed");     // erase to bottom of display
    rows = tigetnum("lines");
    cols = tigetnum("cols");
    if (!cuu1 || cuu1 == (char*)-1) tty = false;
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
  if (fd == 1) {
    std::cout.write(data, len);
    std::cout << std::flush;
  } else {
    std::cerr.write(data, len);
    std::cerr << std::flush;
  }
  status_redraw();
}

void status_refresh()
{
  status_clear();
  status_redraw();
}

void status_finish()
{
  status_clear();
  reset_shell_mode();
}
