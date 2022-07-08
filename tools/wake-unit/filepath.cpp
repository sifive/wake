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

#include <wcl/filepath.h>
#include <wcl/xoshiro_256.h>

#include <random>
#include <string>
#include <vector>

#include "unit.h"

static std::vector<std::string> to_vec(std::string str) {
  std::vector<std::string> actual;

  for (std::string&& node : wcl::make_filepath_range(str)) {
    actual.emplace_back(std::move(node));
  }

  return actual;
}

TEST(filepath_range_basic) {
  std::vector<std::string> expected = {"this", "is", "a", "test"};

  {
    std::vector<std::string> actual = to_vec("this/is/a/test");
    EXPECT_EQUAL(expected, actual);
  }

  {
    std::vector<std::string> actual = to_vec("/this/is/a/test");
    EXPECT_EQUAL(expected, actual);
  }

  {
    std::vector<std::string> actual = to_vec("this/is/a/test/");
    EXPECT_EQUAL(expected, actual);
  }

  {
    std::vector<std::string> actual = to_vec("/this/is/a/test/");
    EXPECT_EQUAL(expected, actual);
  }
}

TEST(filepath_range_empty_node) {
  {
    std::vector<std::string> expected = {"this", "", "a", "test"};
    std::vector<std::string> actual = to_vec("/this//a/test");

    EXPECT_EQUAL(expected, actual);
  }

  {
    std::vector<std::string> expected = {"", "is", "a", "test"};
    std::vector<std::string> actual = to_vec("//is/a/test");

    EXPECT_EQUAL(expected, actual);
  }

  {
    std::vector<std::string> expected = {"this", "is", "a", ""};
    std::vector<std::string> actual = to_vec("/this/is/a//");

    EXPECT_EQUAL(expected, actual);
  }
}

TEST(filepath_range_no_node) {
  std::vector<std::string> expected = {};
  std::vector<std::string> actual = to_vec("");

  EXPECT_EQUAL(expected, actual);
}

TEST(filepath_range_only_slash) {
  std::vector<std::string> expected = {};
  std::vector<std::string> actual = to_vec("/");

  EXPECT_EQUAL(expected, actual);
}

TEST(filepath_range_two_slash) {
  std::vector<std::string> expected = {""};
  std::vector<std::string> actual = to_vec("//");

  EXPECT_EQUAL(expected, actual);
}

TEST(filepath_range_one_node) {
  std::vector<std::string> expected = {"test"};
  {
    std::vector<std::string> actual = to_vec("test");
    EXPECT_EQUAL(expected, actual);
  }

  {
    std::vector<std::string> actual = to_vec("/test");
    EXPECT_EQUAL(expected, actual);
  }

  {
    std::vector<std::string> actual = to_vec("test/");
    EXPECT_EQUAL(expected, actual);
  }

  {
    std::vector<std::string> actual = to_vec("/test/");
    EXPECT_EQUAL(expected, actual);
  }
}

template <class Gen>
static std::string posix_portable_name(int min_length, int max_length, Gen& gen) {
  static const std::string chars =
      ".-_abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  std::uniform_int_distribution<int> length_dist(min_length, max_length);
  int size = length_dist(gen);

  std::uniform_int_distribution<int> node_length(0, chars.size() - 1);
  std::string out;
  for (int i = 0; i < size; ++i) {
    out += chars[node_length(gen)];
  }

  return out;
}

template <class Gen>
static std::vector<std::string> posix_portable_path(int min_length, int max_length, Gen& gen) {
  std::uniform_int_distribution<int> length_dist(min_length, max_length);
  int size = length_dist(gen);

  std::vector<std::string> out;
  for (int i = 0; i < size; ++i) {
    out.push_back(posix_portable_name(1, 5, gen));  // empty nodes not actully allowed
  }

  return out;
}

template <class Gen>
static std::string to_path(const std::vector<std::string>& vec, Gen& gen) {
  std::uniform_int_distribution<int> coin(0, 1);
  std::string out;

  // Do we want a leading slash?
  if (coin(gen)) {
    out += "/";
  }

  // Always trail and we'll remove it later randomly
  for (const auto& node : vec) {
    out += node;
    out += "/";
  }

  // Now randomly remove the trailing slash
  if (out.size() && coin(gen)) {
    out.pop_back();
  }

  return out;
}

TEST(filepath_range_fuzz_nice) {
  // seed the rng for fuzzing
  uint64_t seedV = 0xdeadbeefdeadbeef;
  std::tuple<uint64_t, uint64_t, uint64_t, uint64_t> seed = {seedV, seedV, seedV, seedV};
  wcl::xoshiro_256 rng(seed);

  for (int i = 0; i < 3; ++i) {
    auto expected = posix_portable_path(0, 5, rng);
    auto path = to_path(expected, rng);
    auto actual = to_vec(path);
    EXPECT_EQUAL(expected, actual) << "NOTE: path = " << path << "\n";
  }
}

static std::string gen_garbage(int min_length, int max_length, wcl::xoshiro_256& gen) {
  std::string out;

  std::uniform_int_distribution<int> length_dist(min_length, max_length);
  int size = length_dist(gen);

  for (int i = 0; i < size; i++) {
    out += static_cast<char>(gen());
  }

  return out;
}

TEST(filepath_range_fuzz_garbage) {
  // seed the rng for fuzzing
  uint64_t seedV = 0xdeadbeefdeadbeef;
  std::tuple<uint64_t, uint64_t, uint64_t, uint64_t> seed = {seedV, seedV, seedV, seedV};
  wcl::xoshiro_256 rng(seed);

  for (int i = 0; i < 10000; ++i) {
    auto path = gen_garbage(0, 128, rng);
    auto actual = to_vec(path);

    // We can't say much about the resulting path but there's a few santity checks we can
    // do. Mostly this test is just to make sure we don't segfault or throw an exception
    // somewhere.
    EXPECT_TRUE(actual.size() >= 0 && actual.size() <= 128)
        << "Actual size: " << actual.size() << ", path size = " << path.size();

    size_t total_size = 0;
    for (const auto& node : actual) {
      total_size += node.size();
    }

    EXPECT_TRUE(total_size <= path.size());
  }
}
