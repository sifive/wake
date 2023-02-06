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

#define CONVERTS_TO(x, y) EXPECT_EQUAL(y, convert_color(x))

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

TEST(term_basic_bold_foreground_colors) {
  // Black
  CONVERTS_TO("\033[1;30m", "\033[1m\033[30m");

  // Red
  CONVERTS_TO("\033[1;31m", "\033[1m\033[31m");

  // Green
  CONVERTS_TO("\033[1;32m", "\033[1m\033[32m");

  // Yellow
  CONVERTS_TO("\033[1;33m", "\033[1m\033[33m");

  // Blue
  CONVERTS_TO("\033[1;34m", "\033[1m\033[34m");

  // Magenta
  CONVERTS_TO("\033[1;35m", "\033[1m\033[35m");

  // Cyan
  CONVERTS_TO("\033[1;36m", "\033[1m\033[36m");

  // White
  CONVERTS_TO("\033[1;37m", "\033[1m\033[37m");
}

TEST(term_basic_bold_foreground_colors_dumb) {
  CONVERTS_TO_DUMB("\033[1;30m", "");
  CONVERTS_TO_DUMB("\033[1;31m", "");
  CONVERTS_TO_DUMB("\033[1;32m", "");
  CONVERTS_TO_DUMB("\033[1;33m", "");
  CONVERTS_TO_DUMB("\033[1;34m", "");
  CONVERTS_TO_DUMB("\033[1;35m", "");
  CONVERTS_TO_DUMB("\033[1;36m", "");
  CONVERTS_TO_DUMB("\033[1;37m", "");
}

TEST(term_basic_background_colors) {
  // Black
  CONVERTS_TO("\033[40m", "\033[40m");

  // Red
  CONVERTS_TO("\033[41m", "\033[41m");

  // Green
  CONVERTS_TO("\033[42m", "\033[42m");

  // Yellow
  CONVERTS_TO("\033[43m", "\033[43m");

  // Blue
  CONVERTS_TO("\033[44m", "\033[44m");

  // Magenta
  CONVERTS_TO("\033[45m", "\033[45m");

  // Cyan
  CONVERTS_TO("\033[46m", "\033[46m");

  // White
  CONVERTS_TO("\033[47m", "\033[47m");
}

TEST(term_basic_bold_background_colors) {
  // Black
  CONVERTS_TO("\033[1;40m", "\033[1m\033[40m");

  // Red
  CONVERTS_TO("\033[1;41m", "\033[1m\033[41m");

  // Green
  CONVERTS_TO("\033[1;42m", "\033[1m\033[42m");

  // Yellow
  CONVERTS_TO("\033[1;43m", "\033[1m\033[43m");

  // Blue
  CONVERTS_TO("\033[1;44m", "\033[1m\033[44m");

  // Magenta
  CONVERTS_TO("\033[1;45m", "\033[1m\033[45m");

  // Cyan
  CONVERTS_TO("\033[1;46m", "\033[1m\033[46m");

  // White
  CONVERTS_TO("\033[1;47m", "\033[1m\033[47m");
}

TEST(term_basic_bold_background_colors_dumb) {
  CONVERTS_TO_DUMB("\033[1;40m", "");
  CONVERTS_TO_DUMB("\033[1;41m", "");
  CONVERTS_TO_DUMB("\033[1;42m", "");
  CONVERTS_TO_DUMB("\033[1;43m", "");
  CONVERTS_TO_DUMB("\033[1;44m", "");
  CONVERTS_TO_DUMB("\033[1;45m", "");
  CONVERTS_TO_DUMB("\033[1;46m", "");
  CONVERTS_TO_DUMB("\033[1;47m", "");
}

TEST(term_basic_intense_foreground_colors) {
  // Black
  CONVERTS_TO("\033[90m", "\033[90m");

  // Red
  CONVERTS_TO("\033[91m", "\033[91m");

  // Green
  CONVERTS_TO("\033[92m", "\033[92m");

  // Yellow
  CONVERTS_TO("\033[93m", "\033[93m");

  // Blue
  CONVERTS_TO("\033[94m", "\033[94m");

  // Magenta
  CONVERTS_TO("\033[95m", "\033[95m");

  // Cyan
  CONVERTS_TO("\033[96m", "\033[96m");

  // White
  CONVERTS_TO("\033[97m", "\033[97m");
}

TEST(term_basic_foreground_colors_dumb) {
  CONVERTS_TO_DUMB("\033[30m", "");
  CONVERTS_TO_DUMB("\033[31m", "");
  CONVERTS_TO_DUMB("\033[32m", "");
  CONVERTS_TO_DUMB("\033[33m", "");
  CONVERTS_TO_DUMB("\033[34m", "");
  CONVERTS_TO_DUMB("\033[35m", "");
  CONVERTS_TO_DUMB("\033[36m", "");
  CONVERTS_TO_DUMB("\033[37m", "");
  CONVERTS_TO_DUMB("\033[90m", "");
  CONVERTS_TO_DUMB("\033[91m", "");
  CONVERTS_TO_DUMB("\033[92m", "");
  CONVERTS_TO_DUMB("\033[93m", "");
  CONVERTS_TO_DUMB("\033[94m", "");
  CONVERTS_TO_DUMB("\033[95m", "");
  CONVERTS_TO_DUMB("\033[96m", "");
  CONVERTS_TO_DUMB("\033[97m", "");
}

TEST(term_basic_intense_background_colors) {
  // Black
  CONVERTS_TO("\033[100m", "\033[100m");

  // Red
  CONVERTS_TO("\033[101m", "\033[101m");

  // Green
  CONVERTS_TO("\033[102m", "\033[102m");

  // Yellow
  CONVERTS_TO("\033[103m", "\033[103m");

  // Blue
  CONVERTS_TO("\033[104m", "\033[104m");

  // Magenta
  CONVERTS_TO("\033[105m", "\033[105m");

  // Cyan
  CONVERTS_TO("\033[106m", "\033[106m");

  // White
  CONVERTS_TO("\033[107m", "\033[107m");
}

TEST(term_basic_background_colors_dumb) {
  CONVERTS_TO_DUMB("\033[40m", "");
  CONVERTS_TO_DUMB("\033[41m", "");
  CONVERTS_TO_DUMB("\033[42m", "");
  CONVERTS_TO_DUMB("\033[43m", "");
  CONVERTS_TO_DUMB("\033[44m", "");
  CONVERTS_TO_DUMB("\033[45m", "");
  CONVERTS_TO_DUMB("\033[46m", "");
  CONVERTS_TO_DUMB("\033[47m", "");
  CONVERTS_TO_DUMB("\033[100m", "");
  CONVERTS_TO_DUMB("\033[101m", "");
  CONVERTS_TO_DUMB("\033[102m", "");
  CONVERTS_TO_DUMB("\033[103m", "");
  CONVERTS_TO_DUMB("\033[104m", "");
  CONVERTS_TO_DUMB("\033[105m", "");
  CONVERTS_TO_DUMB("\033[106m", "");
  CONVERTS_TO_DUMB("\033[107m", "");
}

TEST(term_rich_foreground_colors) {
  // Sweep test all 8-bit foreground colors
  for (int i = 0; i < 8; ++i) {
    CONVERTS_TO("\033[38;5;" + std::to_string(i) + "m", "\033[" + std::to_string(30 + i) + "m");
  }
  for (int i = 8; i < 16; ++i) {
    CONVERTS_TO("\033[38;5;" + std::to_string(i) + "m", "\033[" + std::to_string(90 + i - 8) + "m");
  }
  for (int i = 16; i < 256; ++i) {
    std::string esc_seq = "\033[38;5;" + std::to_string(i) + "m";
    CONVERTS_TO(esc_seq, esc_seq);
  }
}

TEST(term_rich_foreground_colors_dumb) {
  // Sweep test all 8-bit foreground colors
  for (int i = 0; i < 8; ++i) {
    CONVERTS_TO_DUMB("\033[38;5;" + std::to_string(i) + "m", "");
  }
  for (int i = 8; i < 16; ++i) {
    CONVERTS_TO_DUMB("\033[38;5;" + std::to_string(i) + "m", "");
  }
  for (int i = 16; i < 256; ++i) {
    CONVERTS_TO_DUMB("\033[38;5;" + std::to_string(i) + "m", "");
  }
}

TEST(term_rich_background_colors) {
  // Sweep test all 8-bit foreground colors
  for (int i = 0; i < 8; ++i) {
    CONVERTS_TO("\033[48;5;" + std::to_string(i) + "m", "\033[" + std::to_string(40 + i) + "m");
  }
  for (int i = 8; i < 16; ++i) {
    CONVERTS_TO("\033[48;5;" + std::to_string(i) + "m",
                "\033[" + std::to_string(100 + i - 8) + "m");
  }
  for (int i = 16; i < 256; ++i) {
    std::string esc_seq = "\033[48;5;" + std::to_string(i) + "m";
    CONVERTS_TO(esc_seq, esc_seq);
  }
}

TEST(term_rich_background_colors_dumb) {
  // Sweep test all 8-bit foreground colors
  for (int i = 0; i < 8; ++i) {
    CONVERTS_TO_DUMB("\033[48;5;" + std::to_string(i) + "m", "");
  }
  for (int i = 8; i < 16; ++i) {
    CONVERTS_TO_DUMB("\033[48;5;" + std::to_string(i) + "m", "");
  }
  for (int i = 16; i < 256; ++i) {
    CONVERTS_TO_DUMB("\033[48;5;" + std::to_string(i) + "m", "");
  }
}

TEST(term_basic_resets) {
  // Term info also resets the character set when you pass term_normal
  // for xterm which is kind of annoying IMO but we translate it that
  // way anyhow. Maybe there's a differnt term info command.
  // TODO: We should handle \033(B by ignoring it I think.
  CONVERTS_TO("\033[m", "\033(B\033[m");
  CONVERTS_TO("\033[0m", "\033(B\033[m");
  CONVERTS_TO("\033[24m", "\033[24m");
  CONVERTS_TO("\033[27m", "\033[27m");
}

TEST(term_basic_resets_dumb) {
  CONVERTS_TO_DUMB("\033[m", "");
  CONVERTS_TO_DUMB("\033[0m", "");
  CONVERTS_TO_DUMB("\033[24m", "");
  CONVERTS_TO_DUMB("\033[27m", "");
}

TEST(term_basic_controls) {
  CONVERTS_TO("\033[1m", "\033[1m");
  CONVERTS_TO("\033[2m", "\033[2m");
  CONVERTS_TO("\033[4m", "\033[4m");
}

TEST(term_basic_controls_dumb) {
  CONVERTS_TO_DUMB("\033[1m", "");
  CONVERTS_TO_DUMB("\033[2m", "");
  CONVERTS_TO_DUMB("\033[4m", "");
}

TEST(term_basic_ignores) {
  CONVERTS_TO("\033[2;1m", "");
  CONVERTS_TO("\033[3;1m", "");
  CONVERTS_TO("\033[;m", "");
  CONVERTS_TO("\033[54m", "");
  CONVERTS_TO("\033[K", "");
  CONVERTS_TO("\033[1K", "");
  CONVERTS_TO("\033[1@", "");
  CONVERTS_TO("\033[1A", "");
  CONVERTS_TO("\033[1B", "");
  CONVERTS_TO("\033[1C", "");
  CONVERTS_TO("\033[1D", "");
  CONVERTS_TO("\033[1E", "");
  CONVERTS_TO("\033[1F", "");
  CONVERTS_TO("\033[44;46H", "");
  CONVERTS_TO("\033[4958696857m", "");
  CONVERTS_TO("\033[457;4565;565;465;457", "");
  CONVERTS_TO("\033[~", "");
  CONVERTS_TO("\033P", "");
  CONVERTS_TO("\033#hj", "j");
  CONVERTS_TO("\033%5c", "c");
  CONVERTS_TO("\033(8b", "b");
  CONVERTS_TO("\033)7^", "^");
  CONVERTS_TO("\033*#f", "f");
  CONVERTS_TO("\033+94", "4");
  CONVERTS_TO("\033-68", "8");
  CONVERTS_TO("\033.f%", "%");
  CONVERTS_TO("\033/()", ")");
}

TEST(term_gcc_smoke_test) {
  CONVERTS_TO("In file included from \033[01m\033[Ktools/wake-unit/wake-unit.cpp:20\033[m:",
              "In file included from \033[1mtools/wake-unit/wake-unit.cpp:20\033(B\033[m:");
  CONVERTS_TO("\033[01m\033[Ktools/wake-unit/unit.h:146:95:\033[m\033[K \033[01;31m\033[Kerror:",
              "\033[1mtools/wake-unit/unit.h:146:95:\033(B\033[m \033[1m\033[31merror:");
}

TEST(term_dumb_gcc_smoke_test) {
  CONVERTS_TO_DUMB("In file included from \033[01m\033[Ktools/wake-unit/wake-unit.cpp:20\033[m:",
                   "In file included from tools/wake-unit/wake-unit.cpp:20:");
  CONVERTS_TO_DUMB(
      "\033[01m\033[Ktools/wake-unit/unit.h:146:95:\033[m\033[K \033[01;31m\033[Kerror:",
      "tools/wake-unit/unit.h:146:95: error:");
}
