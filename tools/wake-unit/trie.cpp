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

#include <wcl/trie.h>
#include <wcl/xoshiro_256.h>

#include <vector>
#include <string>
#include <map>
#include <algorithm>
#include <memory>
#include <random>

#include "unit.h"

TEST(trie_string) {
  wcl::trie<std::string, int> test;
  std::vector<std::string> seq = {"this", "is", "a", "test"};
  std::vector<std::string> to_move = seq;

  EXPECT_EQUAL(nullptr, test.find(seq.begin(), seq.end()));
  test.move_emplace(to_move.begin(), to_move.end(), 10);

  auto v1 = test.find(seq.begin(), seq.end());
  ASSERT_TRUE(v1 != nullptr);
  EXPECT_EQUAL(10, *v1);
}

TEST(trie_unique) {
  wcl::trie<int, std::unique_ptr<int>> test;
  int dummy;
  test.move_emplace(&dummy, &dummy, std::make_unique<int>(10));
  auto v1 = test.find(&dummy, &dummy);
  ASSERT_TRUE(v1 != nullptr);
  EXPECT_EQUAL(10, **v1);
}

TEST(trie_basic) {
  wcl::trie<int, int> test;
  int seq[] = {0, 1, 2, 3};
  test.move_emplace(seq, seq + 1, 10);
  test.move_emplace(seq, seq + 2, 20);
  // Add a skip in here
  test.move_emplace(seq, seq + 4, 40);

  auto v1 = test.find(seq, seq + 1);
  ASSERT_TRUE(v1 != nullptr);
  EXPECT_EQUAL(10, *v1);

  auto v2 = test.find(seq, seq + 2);
  ASSERT_TRUE(v2 != nullptr);
  EXPECT_EQUAL(20, *v2);

  auto v3 = test.find(seq, seq + 3);
  EXPECT_FALSE(v3 != nullptr);

  auto v4 = test.find(seq, seq + 4);
  ASSERT_TRUE(v4 != nullptr);
  EXPECT_EQUAL(40, *v4);
}

TEST(trie_basic_const) {
  wcl::trie<int, int> testStore;
  int seq[] = {0, 1, 2, 3};
  testStore.move_emplace(seq, seq + 1, 10);
  testStore.move_emplace(seq, seq + 2, 20);
  // Add a skip in here
  testStore.move_emplace(seq, seq + 4, 40);

  const wcl::trie<int, int>& test = testStore;
  auto v1 = test.find(seq, seq + 1);
  ASSERT_TRUE(v1 != nullptr);
  EXPECT_EQUAL(10, *v1);

  auto v2 = test.find(seq, seq + 2);
  ASSERT_TRUE(v2 != nullptr);
  EXPECT_EQUAL(20, *v2);

  auto v3 = test.find(seq, seq + 3);
  EXPECT_FALSE(v3 != nullptr);

  auto v4 = test.find(seq, seq + 4);
  ASSERT_TRUE(v4 != nullptr);
  EXPECT_EQUAL(40, *v4);
}

TEST(trie_empty_seq) {
  wcl::trie<int, int> test;
  int seq[0] = {};

  auto v1 = test.find(seq, seq);
  EXPECT_FALSE(v1 != nullptr);

  test.move_emplace(seq, seq, 10);

  v1 = test.find(seq, seq);
  ASSERT_TRUE(v1 != nullptr);
  EXPECT_EQUAL(10, *v1);
}

TEST(trie_empty_seq_const) {
  wcl::trie<int, int> testStore;
  int seq[0] = {};
  testStore.move_emplace(seq, seq, 10);

  const wcl::trie<int, int>& test = testStore;

  auto v1 = test.find(seq, seq);
  ASSERT_TRUE(v1 != nullptr);
  EXPECT_EQUAL(10, *v1);
}

TEST(trie_unit_seqs) {
  wcl::trie<int, int> test;

  // Insert some unit sequences
  for (int i = 0; i < 100; i += 3) {
    test.move_emplace(&i, &i + 1, i);
  }

  // Test that we get them all back
  for (int i = 0; i < 100; i += 3) {
    auto vi = test.find(&i, &i + 1);
    ASSERT_TRUE(vi != nullptr);
    EXPECT_EQUAL(i, *vi);
  }
}

TEST(trie_unit_seqs_const) {
  wcl::trie<int, int> testStore;

  // Insert some unit sequences
  for (int i = 0; i < 100; i += 3) {
    testStore.move_emplace(&i, &i + 1, i);
  }

  const wcl::trie<int, int>& test = testStore;

  // Test that we get them all back
  for (int i = 0; i < 100; i += 3) {
    auto vi = test.find(&i, &i + 1);
    ASSERT_TRUE(vi != nullptr);
    EXPECT_EQUAL(i, *vi);
  }
}

TEST(trie_long_seq) {
  wcl::trie<int, int> test;
  int seq1[] = {5, 8, 2, 8, 9, 4, 7, 0, 4, 3, 8};
  int seq2[] = {7, 4, 2, 9, 0, 5, 9, 6, 3};
  int seq3[] = {6, 0, 4, 2, 6, 9, 5, 3, 3, 8, 0, 4, 3, 7, 9, 6, 4, 2};

  test.move_emplace(seq3, seq3 + sizeof(seq3)/sizeof(*seq3), 30);
  test.move_emplace(seq2, seq2 + sizeof(seq2)/sizeof(*seq2), 20);
  test.move_emplace(seq1, seq1 + sizeof(seq1)/sizeof(*seq1), 10);

  auto v1 = test.find(seq1, seq1 + sizeof(seq1)/sizeof(*seq1));
  ASSERT_TRUE(v1 != nullptr);
  EXPECT_EQUAL(10, *v1);

  auto v2 = test.find(seq2, seq2 + sizeof(seq2)/sizeof(*seq2));
  ASSERT_TRUE(v2 != nullptr);
  EXPECT_EQUAL(20, *v2);

  auto v3 = test.find(seq3, seq3 + sizeof(seq3)/sizeof(*seq3));
  ASSERT_TRUE(v3 != nullptr);
  EXPECT_EQUAL(30, *v3);
}


template <class F, class Gen>
static std::pair<std::vector<int>, int> gen_seq_pair(int min_length, int max_length, Gen& gen, F f) {

  std::uniform_int_distribution<int> length_dist(min_length, max_length);
  int size = length_dist(gen);

  std::uniform_int_distribution<int> value_dist(0, 1000);
  std::vector<int> out(size, 0);
  for (auto& out_value : out) {
    out_value = f(value_dist(gen));
  }

  return std::make_pair(std::move(out), value_dist(gen));
}

TEST(trie_fuzz) {
  std::map<std::vector<int>, int> recall;
  wcl::trie<int, int> test;

  // seed the rng for fuzzing
  uint64_t seedV = 0xdeadbeefdeadbeef;
  std::tuple<uint64_t, uint64_t, uint64_t, uint64_t> seed = {seedV, seedV, seedV, seedV};
  wcl::xoshiro_256 rng(seed);

  // First insert many values
  for (int i = 0; i < 1000; ++i) {
    // Generate values that are even mod 7. Then we can generate values that are odd
    // mod 7 later to ensure we have a unique sequence. The benifit of being even mod 7
    // is that both even and odd numbers, with no trivial pattern to them, are even mod
    // 7. For instance 7 mod 7 is 0 which is even, 8 mod 7 is 1 which is odd, 14 mod 7 is
    // 0 which is even and 15 mod 7 is 1 which is odd. So you can so that every multiple
    // of 7, the even/odd pattenr flips. This ensures better coverage of your code.
    auto pair = gen_seq_pair(0, 20, rng, [](int x) {
      if ((x % 7) & 1) return x + 1;
      return x;
    });
    recall[pair.first] = pair.second;
    test.move_emplace(pair.first.begin(), pair.first.end(), pair.second);
  }

  // Next recall them all but in a different order than
  // they were inserted.
  for (auto pair : recall) {
    auto recall_value = test.find(pair.first.begin(), pair.first.end());
    ASSERT_TRUE(recall_value != nullptr);
    EXPECT_EQUAL(recall[pair.first], *recall_value);
  }

  auto to_str = [](const std::vector<int>& seq) {
    std::string out;
    for (int val : seq) {
      out += std::to_string(val);
      out += ", ";
    }
    return out;
  };

  // Now make sure none of these values are in the trie
  for (int i = 0; i < 1000; ++i) {
    // We don't allow empty sequences because they are likely to have
    // been added we can't unique them in anyway.
    auto pair = gen_seq_pair(1, 20, rng, [](int x) {
      if ((x % 7) & 1) return x;
      return x + 1;
    });
    auto null_value = test.find(pair.first.begin(), pair.first.end());
    EXPECT_EQUAL(nullptr, null_value) << "Checking seq: " << to_str(pair.first);
  }
}
