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

#include "util/shell.h"

#include "unit.h"

TEST(shell_escape_nominal) {
  std::string esc = shell_escape("echo");
  EXPECT_EQUAL(esc, "echo");
  esc = shell_escape("test");
  EXPECT_EQUAL(esc, "test");
  esc = shell_escape("here");
  EXPECT_EQUAL(esc, "here");
}

TEST(shell_escape_spaces) {
  std::string esc = shell_escape("echo test here");
  EXPECT_EQUAL(esc, "'echo test here'");
  esc = shell_escape("a b c");
  EXPECT_EQUAL(esc, "'a b c'");
  esc = shell_escape("zz ss yy aa bb");
  EXPECT_EQUAL(esc, "'zz ss yy aa bb'");

  esc = shell_escape(" echo");
  EXPECT_EQUAL(esc, "' echo'");

  esc = shell_escape("echo ");
  EXPECT_EQUAL(esc, "'echo '");
}

TEST(shell_escape_empty_string) {
  std::string esc = shell_escape("");
  EXPECT_EQUAL(esc, "''");
}

TEST(shell_escape_special) {
  std::string esc = shell_escape("\n");
  EXPECT_EQUAL(esc, "'\n'");

  esc = shell_escape("'");
  EXPECT_EQUAL(esc, "''\\'''");

  esc = shell_escape("'test'");
  EXPECT_EQUAL(esc, "''\\''test'\\'''");
}
