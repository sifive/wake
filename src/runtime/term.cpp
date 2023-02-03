/*
 * Copyright 2022 SiFive, Inc.
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

#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include "util/term.h"

#ifndef __EMSCRIPTEN__

#include <curses.h>
#include <sys/ioctl.h>
#include <term.h>
#include <termios.h>
#include <unistd.h>

#include <cstring>
#include <set>
#include <sstream>

#define XTERM256_FOREGROUND_ESCAPE 38
#define XTERM256_BACKGROUND_ESCAPE 48
#define XTERM256_8BIT_ESCAPE 5

void TermInfoBuf::clear_codes() {
  codes.clear();
  cur_code = -1;
}

// Puts a character to `buf`
void TermInfoBuf::put(char c) { buf.sputc(c); }

// Puts a string to `buf`
void TermInfoBuf::putstr(const char *str) { buf.sputn(str, strlen(str)); }

// Updates `cur_code` with the next read digit
void TermInfoBuf::update_code(char digit) {
  if (cur_code < 0) cur_code = 0;
  cur_code = cur_code * 10 + digit;
}

// Pushes `cur_code` into `codes`, sets `cur_num` to 0
void TermInfoBuf::next_code() {
  codes.push_back(cur_code);
  cur_code = -1;
}

// Uses terminfo to output strings for each code
void TermInfoBuf::output_codes() {
  if (cur_code >= 0) codes.push_back(cur_code);

  // By moving `codes` out we ensure that all exit paths
  // clear the current accumulated codes.
  std::vector<int> output_codes = std::move(codes);
  cur_code = -1;

  // When dumb is set to true, we want to avoid all
  // console manipulation.
  if (dumb) return;

  // No code is interpreted as a reset
  if (output_codes.size() == 0) {
    putstr(term_normal());
    return;
  }

  // Sometimes we don't want
  // Handle singular codes (italics and crossed-out not-portable)
  // We also do not handle blinking despite xterm just translating that
  // to bold.
  if (output_codes.size() == 1) {
    switch (output_codes[0]) {
      case 0:  // Normal
        putstr(term_normal());
        return;
      case 1:  // Bold
        putstr(term_intensity(2));
        return;
      case 2:  // Faint
        putstr(term_intensity(1));
        return;
      case 4:  // Underline
        putstr(term_set_underline(true));
        return;
      case 7:  // Inverse (display as standout)
        putstr(term_set_standout(true));
        return;
      case 21:  // Double-Underline (treat as single underline)
        putstr(term_set_underline(true));
        return;
      case 24:  // Not Underline (or Double underline)
        putstr(term_set_underline(false));
        return;
      case 27:  // Not standout
        putstr(term_set_standout(false));
        return;
      case 30:
      case 31:
      case 32:
      case 33:
      case 34:
      case 35:
      case 36:
      case 37: {
        putstr(term_colour(output_codes[0] - 30));
        return;
      }
      case 40:
      case 41:
      case 42:
      case 43:
      case 44:
      case 45:
      case 46:
      case 47:
        putstr(term_colour_background(output_codes[0] - 40));
        return;
      case 90:
      case 91:
      case 92:
      case 93:
      case 94:
      case 95:
      case 96:
      case 97:
        putstr(term_colour(output_codes[0] - 90 + 8));
        return;
      case 100:
      case 101:
      case 102:
      case 103:
      case 104:
      case 105:
      case 106:
      case 107:
        putstr(term_colour_background(output_codes[0] - 100 + 8));
        return;
    }
  }

  if (output_codes.size() == 2 && output_codes[0] == 1) {
    int color_code = output_codes[1];
    switch (color_code) {
      case 30:
      case 31:
      case 32:
      case 33:
      case 34:
      case 35:
      case 36:
      case 37:
        putstr(term_intensity(2));
        putstr(term_colour(color_code - 30));
        return;
      case 40:
      case 41:
      case 42:
      case 43:
      case 44:
      case 45:
      case 46:
      case 47:
        putstr(term_intensity(2));
        putstr(term_colour_background(color_code - 40));
        return;
      case 90:
      case 91:
      case 92:
      case 93:
      case 94:
      case 95:
      case 96:
      case 97:
        putstr(term_intensity(2));
        putstr(term_colour(color_code - 90 + 8));
        return;
      case 100:
      case 101:
      case 102:
      case 103:
      case 104:
      case 105:
      case 106:
      case 107:
        putstr(term_intensity(2));
        putstr(term_colour(color_code - 100 + 8));
        return;
    }
  }

  // Handle 8-bit foreground colors
  if (output_codes[0] == XTERM256_FOREGROUND_ESCAPE) {
    if (output_codes[1] != XTERM256_8BIT_ESCAPE) return;
    if (output_codes.size() != 3) return;
    // Nicely, terminfo and xterm-256color both use ANSI-256 colors
    putstr(term_colour(output_codes[2]));
    return;
  }

  // Handle 8-bit foreground colors
  if (output_codes[0] == XTERM256_BACKGROUND_ESCAPE) {
    if (output_codes[1] != XTERM256_8BIT_ESCAPE) return;
    if (output_codes.size() != 3) return;
    // Nicely, terminfo and xterm-256color both use ANSI-256 colors
    putstr(term_colour_background(output_codes[2]));
    return;
  }
}

static void write_num(std::streambuf &buf, int x) {
  std::string s = std::to_string(x);
  buf.sputn(s.c_str(), s.size());
}

// In case we have to back up from the num_state to default_state,
// we put all previous characters. This happens if at some point
// we have to flush all thus-far collected numbers because the
// escape code is invalid. There is some special edge case handeling
// of `cur_code == 0` that has to be accounted for.
//
// If the lexer encounters a leading 0 on a number, it should
// handle that just like any other invalid character. But if the
// zero is *not* leading then it should be treated as part of a
// number.
//
// So if cur_code == 0 we assume the last parsed character was ';'
// we should be correct.
void TermInfoBuf::flush_nums() {
  buf.sputc('\033');
  buf.sputc('[');
  for (size_t i = 0; i < codes.size(); ++i) {
    write_num(buf, codes[i]);
    if (i + 1 != codes.size()) buf.sputc(';');
    if (i + 1 == codes.size() && cur_code != 0) buf.sputc(';');
  }
  if (cur_code != 0) write_num(buf, cur_code);
  cur_code = 0;
}

int TermInfoBuf::sync() { return buf.pubsync(); }

int TermInfoBuf::overflow(int c) {
  static const char tcis[] = " #%()*+-./";
  static std::set<char> two_character_ignore_starters(tcis, tcis + sizeof(tcis));

  if (c == EOF) return EOF;
  switch (state) {
    case State::default_state:
      if (c == '\033') {
        state = State::esc_state;
      } else {
        put(c);
      }
      break;
    case State::esc_state:
      // TODO: Add other things to this state,
      // specifically all the escape sequences we
      // we want to ignore. There should be two
      // cases, one character cases and two character
      // cases. For the two character case we'll need
      // an extra case.
      if (c == '[') {
        state = State::num_state;
        break;
      }
      // We ignore anything unrecognized, but
      // we may need to ignore two characters as well.
      // TODO: There are a few longer xterm escape sequences
      // that have longer leading characters. This
      // implementation will currently incorrectly render
      // those. They don't seem to see any wide spread use
      // however.
      if (two_character_ignore_starters.count(c)) {
        state = State::ignore_state;
        break;
      } else {
        state = State::default_state;
      }
      break;
    case State::ignore_state:
      state = State::default_state;
      break;
    case State::num_state:
      // Push the code if a ';' is seen
      if (c == ';') {
        next_code();
        break;
      }

      // Handle end of codes
      if (c == 'm') {
        output_codes();
        state = State::default_state;
        break;
      }

      // Sometimes an escape sequence will end in a way we don't
      // recognize, in that case, ignore the escape sequence.
      if (!isdigit(c)) {
        clear_codes();
        state = State::default_state;
        break;
      }

      // Handle digits
      if (isdigit(c)) {
        update_code(c - '0');
        break;
      }
  }

  return c;
}

int FdBuf::overflow(int c) {
  if (c == EOF) {
    return EOF;
  }

  char ch = c;
  ssize_t r = write(fd, &ch, sizeof(ch));
  if (r == -1) {
    // TODO(jake): what should be done here?
  }

  return c;
}

int FdBuf::sync() { return fsync(fd); }

static bool tty = false;
static const char *cuu1;
static const char *cr;
static const char *ed;
static const char *sgr0;

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

const char *term_colour_background(int code) {
  static char setaf_lit[] = "setab";
  if (!sgr0) return "";
  char *format = tigetstr(setaf_lit);
  if (missing_termfn(format)) return "";
  return wrap_termfn(tparm(format, code));
}

const char *term_set_underline(bool should_underline) {
  static char underline[] = "smul";
  static char no_underline[] = "rmul";
  if (!sgr0) return "";
  if (should_underline)
    return wrap_termfn(tigetstr(underline));
  else
    return wrap_termfn(tigetstr(no_underline));
}

const char *term_set_standout(bool should_standout) {
  static char standout[] = "smso";
  static char no_standout[] = "rmso";
  if (!sgr0) return "";
  if (should_standout)
    return wrap_termfn(tigetstr(standout));
  else
    return wrap_termfn(tigetstr(no_standout));
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

bool term_init(bool tty_, bool skip_atty) {
  tty = tty_;

  if (!tty) {
    return false;
  }

  if (!skip_atty && (isatty(1) != 1 || isatty(2) != 1)) {
    return false;
  }

  int eret;
  int ret = setupterm(0, 2, &eret);
  if (ret != OK) {
    return false;
  }

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
  int rows = tigetnum(lines_lit);
  int cols = tigetnum(cols_lit);
  sgr0 = tigetstr(sgr0_lit);  // optional
  if (missing_termfn(sgr0)) sgr0 = nullptr;

  if (missing_termfn(cuu1) || missing_termfn(cr) || missing_termfn(ed)) {
    return false;
  }

  if (cols < 0 || rows < 0) {
    return false;
  }

  return true;
}

const char *term_cuu1() { return cuu1; }

const char *term_cr() { return cr; }

const char *term_ed() { return ed; }

bool term_tty() { return tty; }

#else

const char *term_colour(int code) { return ""; }
const char *term_normal() { return ""; }
const char *term_normal(int code) { return ""; }
bool term_init(bool tty, bool skip_atty) { return true; }

const char *term_cuu1() { return ""; }

const char *term_cr() { return ""; }

const char *term_ed() { return ""; }

bool term_tty() { return false; }

#endif
