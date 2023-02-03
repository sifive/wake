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

#ifndef TERM_H
#define TERM_H

#include <streambuf>
#include <vector>

#define TERM_DEFAULT 0

#define TERM_BLACK (8 + 0)
#define TERM_RED (8 + 1)
#define TERM_GREEN (8 + 2)
#define TERM_YELLOW (8 + 3)
#define TERM_BLUE (8 + 4)
#define TERM_MAGENTA (8 + 5)
#define TERM_CYAN (8 + 6)
#define TERM_WHITE (8 + 7)

#define TERM_DIM (16 * 1)
#define TERM_BRIGHT (16 * 2)

// A stringbuf that writes to a file descriptor.
//
class FdBuf : public std::streambuf {
 private:
  // static constexpr size_t buf_size = 512;
  // char* end = nullptr;
  int fd;
  // We use a static thread_local,
  // char buf[buf_size];

 public:
  // FdBuf(int fd_) : end(buf), fd(fd_) {}
  FdBuf(int fd_) : fd(fd_) {}
  virtual ~FdBuf() override { sync(); }
  virtual int sync() override;
  virtual int overflow(int c) override;
};

// A stringbuf that translates xterm-256color into
// whatever the current terminal is. All 8-bit colors
// for both foreground and background are handled.
// In reality, it is imperfect in its current state and
// is going to get some of these escape codes wrong.
// Many cases are currently known to be wrong but
// implementing the full state machine needed for
// parsing is a longer process. This is just a
// first pass that covers 99% of use cases and
// should be viable in practice. We can add on
// to it as we go.
class TermInfoBuf : public std::streambuf {
 private:
  enum class State { default_state, esc_state, ignore_state, num_state };

  State state = State::default_state;
  int cur_code = -1;
  std::vector<int> codes;
  std::streambuf &buf;
  bool dumb = false;

  // Puts a character to `buf`
  void put(char c);
  void putstr(const char *s);

  // Updates `cur_code` with the next read digit
  void update_code(char digit);

  // Pushes `cur_code` into `codes`, sets `cur_num` to 0
  void next_code();

  // Uses terminfo to output strings for each code
  void output_codes();

  // Ignore all thus far aumulated codes
  void clear_codes();

  // In case we have to back up from the num_state to default_state,
  // we put all previous characters
  void flush_nums();

 public:
  TermInfoBuf(std::streambuf *buf_, bool dumb_ = false) : buf(*buf_), dumb(dumb_) {}
  virtual ~TermInfoBuf() override { sync(); }
  virtual int sync() override;
  virtual int overflow(int c) override;
};

bool term_init(bool tty_, bool skip_atty = false);

const char *term_normal();
const char *term_colour(int code);
const char *term_colour_background(int code);
const char *term_intensity(int code);
const char *term_set_standout(bool should_standout);
const char *term_set_underline(bool should_underline);

const char *term_cuu1();
const char *term_cr();
const char *term_ed();
bool term_tty();

#endif
