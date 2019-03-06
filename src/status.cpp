#include "status.h"
#include <iostream>
#include <sys/ioctl.h>
#include <signal.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <curses.h>
#include <term.h>

static bool tty = false;
static int rows = 0, cols = 0;
static const char *cuu1;
static const char *ed;

static int eputc(int c)
{
  std::cerr << (char)c;
  return 0;
}

static void update_rows(int)
{
  struct winsize size;
  if (ioctl(2, TIOCGWINSZ, &size) == 0) {
    rows = size.ws_row;
    cols = size.ws_col;
  }
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
  if (fd == 1) {
    std::cout.write(data, len);
    std::cout << std::flush;
  } else {
    std::cerr.write(data, len);
    std::cerr << std::flush;
  }
}

void status_refresh()
{
  tputs(cuu1, 1, eputc);
}

void status_finish()
{
  reset_shell_mode();
}
