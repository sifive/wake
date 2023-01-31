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
#include <term.h>
#include <unistd.h>

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

const char *term_intensity(int code) {
  static char dim_lit[] = "dim";
  static char bold_lit[] = "bold";
  if (!sgr0) return "";
  if (code == 1) return wrap_termfn(tigetstr(dim_lit));
  if (code == 2) return wrap_termfn(tigetstr(bold_lit));
  return "";
}

const char *term_normal() { return sgr0 ? sgr0 : ""; }

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
    int rows = tigetnum(lines_lit);
    int cols = tigetnum(cols_lit);
    if (missing_termfn(cuu1)) tty = false;
    if (missing_termfn(cr)) tty = false;
    if (missing_termfn(ed)) tty = false;
    if (cols < 0 || rows < 0) tty = false;
    sgr0 = tigetstr(sgr0_lit);  // optional
    if (missing_termfn(sgr0)) sgr0 = nullptr;
  }

  return tty;
}

const char *term_cuu1() { return cuu1; }

const char *term_cr() { return cr; }

const char *term_ed() { return ed; }

bool term_tty() { return tty; }

#else

const char *term_colour(int code) { return ""; }
const char *term_normal() { return ""; }
const char *term_normal(int code) { return ""; }
bool term_init(bool tty) { return true; }

const char *term_cuu1() { return ""; }

const char *term_cr() { return ""; }

const char *term_ed() { return ""; }

bool term_tty() { return false; }

#endif
