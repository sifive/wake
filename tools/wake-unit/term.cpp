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

#include "util/term.h"

#include "unit.h"

std::string safe_print(std::string str) {
  std::cerr << "'";
  for (size_t i = 0; i < str.size(); i++) {
    int c = str[i];
    std::cerr << std::hex << std::setfill('0') << std::setw(2) << c << " ";
  }
  std::cerr << "'" << std::endl;
  return str;
}

static std::string convert_color(std::string str) {
  std::stringstream ss;
  TermInfoBuf tinfo(ss.rdbuf());
  std::ostream out(&tinfo);
  out << str;
  return ss.str();
}

static std::string convert_color_dumb(std::string str) {
  std::stringstream ss;
  TermInfoBuf tinfo(ss.rdbuf(), /*dumb*/ true);
  std::ostream out(&tinfo);
  out << str;
  return ss.str();
}

#define CONVERTS_TO(x, y) EXPECT_EQUAL(safe_print(y), safe_print(convert_color(x)))

#define CONVERTS_TO_DUMB(x, y) EXPECT_EQUAL(y, convert_color_dumb(x))

TEST(term_basic_foreground_colors) {
  // Black
  CONVERTS_TO("\033[30m", "\033[30m");

  // Red
  CONVERTS_TO("\033[31m", "\033[31m");

  // Green
  CONVERTS_TO("\033[32m", "\033[32m");

  // Yellow
  CONVERTS_TO("\033[33m", "\033[33m");

  // Blue
  CONVERTS_TO("\033[34m", "\033[34m");

  // Magenta
  CONVERTS_TO("\033[35m", "\033[35m");

  // Cyan
  CONVERTS_TO("\033[36m", "\033[36m");

  // White
  CONVERTS_TO("\033[37m", "\033[37m");
}
