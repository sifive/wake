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

#include <wcl/xoshiro_256.h>

#include <random>

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

TEST(term_git_smoke_test) {
  CONVERTS_TO("\033[?1h\033=\r2023-02-08 12:31:48 -0600\033[m\r\n\r\033[K\033[?1l\033>",
              "2023-02-08 12:31:48 -0600\033(B\033[m\n");
}

TEST(term_dumb_git_smoke_test) {
  CONVERTS_TO_DUMB("\033[?1h\033=\r2023-02-08 12:31:48 -0600\033[m\r\n\r\033[K\033[?1l\033>",
                   "2023-02-08 12:31:48 -0600\n");
}

TEST(term_unicode_smoke_test) {
  // Unicode should be ignored even if there's an escape sequence
  // or other special character embeded inside
  // CONVERTS_TO("\xC0\r", "\xC0\r");
  CONVERTS_TO("\xC0\033", "\xC0\033");

  CONVERTS_TO("\xE0\x80\r", "\xE0\x80\r");
  CONVERTS_TO("\xE0\x80\033", "\xE0\x80\033");

  CONVERTS_TO("\xF0\x80\x80\r", "\xF0\x80\x80\r");
  CONVERTS_TO("\xF0\x80\x80\033", "\xF0\x80\x80\033");

  // However if we go just one more we'll fall out of the unicode states
  // and special characters should be removed
  CONVERTS_TO("\xF0\x80\x80\x80\r", "\xF0\x80\x80\x80");
}

TEST(term_dumb_unicode_smoke_test) {
  // Unicode should be ignored even if there's an escape sequence
  // or other special character embeded inside
  CONVERTS_TO_DUMB("\xC0\r", "\xC0\r");
  CONVERTS_TO_DUMB("\xC0\033", "\xC0\033");

  CONVERTS_TO_DUMB("\xE0\x80\r", "\xE0\x80\r");
  CONVERTS_TO_DUMB("\xE0\x80\033", "\xE0\x80\033");

  CONVERTS_TO_DUMB("\xF0\x80\x80\r", "\xF0\x80\x80\r");
  CONVERTS_TO_DUMB("\xF0\x80\x80\033", "\xF0\x80\x80\033");

  // However if we go just one more we'll fall out of the unicode states
  // and special characters should be removed
  CONVERTS_TO_DUMB("\xF0\xFF\xFF\xFF\r", "\xF0\xFF\xFF\xFF");
}

std::pair<std::string, std::string> random_ignored_control_seq(wcl::xoshiro_256& rng) {
  std::string out = "\033[";
  std::uniform_int_distribution<int> param_byte_count(0, 10);
  std::uniform_int_distribution<int> intermediate_byte_count(0, 50);
  std::uniform_int_distribution<int> final_byte_dist(0x40, 0x7E);
  std::uniform_int_distribution<int> param_byte_dist(0x30, 0x3F);
  std::uniform_int_distribution<int> intermediate_byte_dist(0x20, 0x2F);

  int pb_count = param_byte_count(rng);
  for (int i = 0; i < pb_count; ++i) {
    out += char(param_byte_dist(rng));
  }

  int ib_count = intermediate_byte_count(rng);
  for (int i = 0; i < ib_count; ++i) {
    out += char(intermediate_byte_dist(rng));
  }

  // pick the final byte but avoid `m` just in case it becomes valid
  char final_byte = final_byte_dist(rng);
  if (final_byte == 'm') final_byte++;
  out += final_byte;

  return {std::move(out), ""};
}

std::pair<std::string, std::string> random_valued_control_seq(wcl::xoshiro_256& rng) {
  static std::vector<int> single_codes = {
      0,  1,  2,  4,  7,  21, 24, 27, 30, 31, 32, 33, 34,  35,  36,  37,  40,  41,  42,  43,
      44, 45, 46, 47, 90, 91, 92, 93, 94, 95, 96, 97, 100, 101, 102, 103, 104, 105, 106, 107};
  static std::vector<int> double_codes = {30, 31, 32,  33,  34,  35,  36,  37,  40,  41, 42,
                                          43, 44, 45,  46,  47,  90,  91,  92,  93,  94, 95,
                                          96, 97, 100, 101, 102, 103, 104, 105, 106, 107};

  std::uniform_int_distribution<int> case_picker(0, 4);
  std::uniform_int_distribution<int> single_code_picker(0, single_codes.size() - 1);
  std::uniform_int_distribution<int> double_code_picker(0, double_codes.size() - 1);
  std::uniform_int_distribution<int> color_picker(0, 255);
  switch (case_picker(rng)) {
    case 0:
      return {"\033[m", ""};
    case 1:
      return {"\033[" + std::to_string(single_codes[single_code_picker(rng)]) + "m", ""};
    case 2:
      return {"\033[1;" + std::to_string(double_codes[double_code_picker(rng)]) + "m", ""};
    case 3:
      return {"\033[38;5;" + std::to_string(color_picker(rng)) + "m", ""};
    case 4:
      return {"\033[48;5;" + std::to_string(color_picker(rng)) + "m", ""};
  }

  return {"", ""};
}

std::pair<std::string, std::string> random_ignored_command(wcl::xoshiro_256& rng) {
  static std::string starts = "]_P^";

  std::uniform_int_distribution<int> start_picker(0, starts.size() - 1);
  std::uniform_int_distribution<int> rand_byte(0, 255);
  int length = rand_byte(rng);
  std::string out = "\033";
  out += starts[start_picker(rng)];
  for (int i = 0; i < length; ++i) {
    char c = char(rand_byte(rng));
    if (c == 0x7 || c == '\033' || c == '\\') continue;
    out += c;
  }
  if (rand_byte(rng) & 1) {
    out += char(0x7);
  } else {
    out += "\033\\";
  }

  return {std::move(out), ""};
}

std::pair<std::string, std::string> random_single_character_command(wcl::xoshiro_256& rng) {
  std::vector<char> cmds = {'\r', '\n', 0x7, 0x8, 0x5, 0xF, 0xE, 0xC, 0xB};
  std::uniform_int_distribution<int> cmd_picker(0, cmds.size() - 1);
  char cmd = cmds[cmd_picker(rng)];
  std::string out;
  out += cmd;
  // We return newlines for VT and FF
  if (cmd == '\n' || cmd == 0xC || cmd == 0xB) {
    return {std::move(out), "\n"};
  }
  return {std::move(out), ""};
}

std::pair<std::string, std::string> random_two_character_command(wcl::xoshiro_256& rng) {
  std::string cmds = "6789=>Fclmno|}~";
  std::uniform_int_distribution<int> cmd_picker(0, cmds.size() - 1);
  std::string out = "\033";
  out += cmds[cmd_picker(rng)];
  return {std::move(out), ""};
}

std::pair<std::string, std::string> random_three_character_command(wcl::xoshiro_256& rng) {
  std::string cmds = " #%()*+-./";
  std::uniform_int_distribution<int> cmd_picker(0, cmds.size() - 1);
  std::uniform_int_distribution<int> rand_byte(0, 255);
  std::string out = "\033";
  out += cmds[cmd_picker(rng)];
  out += char(rand_byte(rng));
  return {std::move(out), ""};
}

inline char fix(uint8_t byte, uint8_t set, uint8_t unset) {
  byte &= ~unset;
  byte |= set;
  return char(byte);
}

inline char ascii_mods(char c) {
  // Avoid sending single character commands
  if (c <= 0xF) return ' ';
  if (c == '\033') return ' ';
  return c;
}

void append_random_utf8_char(std::string& out, wcl::xoshiro_256& rng) {
  std::uniform_int_distribution<int> length_dist(1, 4);
  std::uniform_int_distribution<uint8_t> rand_byte(0, 255);
  int length = length_dist(rng);
  uint8_t unset0 = 0x80;
  uint8_t unset1 = 0x40;
  uint8_t set1 = 0x80;
  uint8_t unset2 = 0x20;
  uint8_t set2 = 0xC0;
  uint8_t unset3 = 0x10;
  uint8_t set3 = 0xE0;
  uint8_t unset4 = 0x8;
  uint8_t set4 = 0xF0;
  switch (length) {
    case 1:
      // if we get a line feed or a vertical tab, we want
      // to convert it to a newline so we avoid generating
      // those here and let another function generate them
      // instead.
      out += ascii_mods(fix(rand_byte(rng), 0, unset0));
      return;
    case 2:
      out += fix(rand_byte(rng), set2, unset2);
      out += fix(rand_byte(rng), set1, unset1);
      return;
    case 3:
      out += fix(rand_byte(rng), set3, unset3);
      out += fix(rand_byte(rng), set1, unset1);
      out += fix(rand_byte(rng), set1, unset1);
      return;
    case 4:
      out += fix(rand_byte(rng), set4, unset4);
      out += fix(rand_byte(rng), set1, unset1);
      out += fix(rand_byte(rng), set1, unset1);
      out += fix(rand_byte(rng), set1, unset1);
      return;
  }
}

std::pair<std::string, std::string> random_unicode_string(wcl::xoshiro_256& rng) {
  std::uniform_int_distribution<int> length_dist(0, 50);
  int length = length_dist(rng);
  std::string out;
  for (int i = 0; i < length; ++i) {
    append_random_utf8_char(out, rng);
  }

  // copy output now so that it is only copied once
  std::string copy = out;
  return {std::move(out), std::move(copy)};
}

TEST(term_dumb_fuzz) {
  std::vector<std::pair<std::string, std::string> (*)(wcl::xoshiro_256 & rng)> funcs = {
      random_ignored_control_seq,   random_valued_control_seq,
      random_ignored_command,       random_single_character_command,
      random_two_character_command, random_three_character_command,
      random_unicode_string};
  uint64_t seedV = 0xdeadbeefdeadbeef;
  std::tuple<uint64_t, uint64_t, uint64_t, uint64_t> seed = {seedV, seedV, seedV, seedV};
  wcl::xoshiro_256 rng(seed);
  std::uniform_int_distribution<int> func_picker(0, funcs.size() - 1);
  std::uniform_int_distribution<int> length_dist(0, 50);

  for (int i = 0; i < 10000; ++i) {
    int length = length_dist(rng);
    std::string input, expected;
    std::string functions;
    for (int j = 0; j < length; ++j) {
      int idx = func_picker(rng);
      functions += std::to_string(idx);
      auto func = funcs[idx];
      auto p = func(rng);
      input += p.first;
      expected += p.second;
    }

    // Now we just hope they're equal!
    CONVERTS_TO_DUMB(input, expected)
        << "\ninput = " << json_escape(input) << "\nfunctions = " << functions << "\n";
  }
}
