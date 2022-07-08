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

#include <vector>
#include <map>
#include <algorithm>
#include <memory>
#include <random>

#include "unit.h"

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


template <class Gen>
static std::pair<std::vector<int>, int> gen_seq_pair(int max_length, Gen& gen) {

  std::uniform_int_distribution<int> length_dist(0, max_length);
  int size = length_dist(gen);

  std::uniform_int_distribution<int> value_dist(-1000, 1000);
  std::vector<int> out(size, 0);
  for (auto& out_value : out) {
    out_value = value_dist(gen);
  }

  return std::make_pair(std::move(out), value_dist);
}

TEST(trie_fuzz_recall) {
  std::map<std::vector<int>, int> recall;
  wcl::trie<int, int> test;

  for (int i = 0; i < 1000; ++i) {
    //auto pair = gen_seq_pair(20, )
  }
}
