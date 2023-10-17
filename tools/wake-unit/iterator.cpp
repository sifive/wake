/*
 * Copyright 2023 SiFive, Inc.
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

#include <wcl/iterator.h>

#include "unit.h"

TEST(iterator_split_by_nominal) {
  std::string str = "an,input,text";

  std::vector<std::string> parts = wcl::split_by_fn(
      ',', str.begin(), str.end(), [](auto a, auto b) { return std::string(a, b); });

  EXPECT_EQUAL(parts.size(), 3u);
  EXPECT_EQUAL(parts[0], "an");
  EXPECT_EQUAL(parts[1], "input");
  EXPECT_EQUAL(parts[2], "text");
}

TEST(iterator_split_by_no_delim) {
  std::string str = "an input text";

  std::vector<std::string> parts = wcl::split_by_fn(
      ',', str.begin(), str.end(), [](auto a, auto b) { return std::string(a, b); });

  EXPECT_EQUAL(parts.size(), 1u);
  EXPECT_EQUAL(parts[0], "an input text");
}

TEST(iterator_split_by_only_delim) {
  std::string str = ",,,,,";

  std::vector<std::string> parts = wcl::split_by_fn(
      ',', str.begin(), str.end(), [](auto a, auto b) { return std::string(a, b); });

  EXPECT_EQUAL(parts.size(), 6u);
  EXPECT_EQUAL(parts[0], "");
  EXPECT_EQUAL(parts[1], "");
  EXPECT_EQUAL(parts[2], "");
  EXPECT_EQUAL(parts[3], "");
  EXPECT_EQUAL(parts[4], "");
  EXPECT_EQUAL(parts[5], "");
}

TEST(iterator_split_by_empty) {
  std::string str = "";

  std::vector<std::string> parts = wcl::split_by_fn(
      ',', str.begin(), str.end(), [](auto a, auto b) { return std::string(a, b); });

  EXPECT_EQUAL(parts.size(), 1u);
  EXPECT_EQUAL(parts[0], "");
}
