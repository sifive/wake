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

#include <wcl/optional.h>

#include <algorithm>
#include <memory>
#include <random>

#include "unit.h"

TEST(option_none_is_none) {
  wcl::optional<int> none;
  EXPECT_FALSE((bool)none);
}

TEST(option_some) {
  wcl::optional<int> some(wcl::in_place_t{}, 10);
  ASSERT_TRUE((bool)some);
  EXPECT_EQUAL(10, *some);
}

TEST(option_inplace) {
  wcl::optional<int> some(wcl::in_place_t{}, 10);
  ASSERT_TRUE((bool)some);
  EXPECT_EQUAL(10, *some);
}

TEST(option_copy) {
  wcl::optional<int> none1;
  wcl::optional<int> none2(none1);
  EXPECT_FALSE((bool)none1);
  EXPECT_FALSE((bool)none2);

  wcl::optional<int> some1(wcl::in_place_t{}, 10);
  wcl::optional<int> some2(some1);
  ASSERT_TRUE((bool)some1);
  EXPECT_EQUAL(10, *some1);
  ASSERT_TRUE((bool)some2);
  EXPECT_EQUAL(10, *some2);
}

TEST(option_move) {
  wcl::optional<int> none1;
  wcl::optional<int> none2(std::move(none1));
  EXPECT_FALSE((bool)none1);
  EXPECT_FALSE((bool)none2);

  wcl::optional<int> some1(wcl::in_place_t{}, 10);
  wcl::optional<int> some2(std::move(some1));
  EXPECT_FALSE((bool)some1);
  ASSERT_TRUE((bool)some2);
  EXPECT_EQUAL(10, *some2);

  // We want to make sure we can make optional move only things.
  wcl::optional<std::unique_ptr<int>> move_only1(wcl::in_place_t{}, std::make_unique<int>(10));
  wcl::optional<std::unique_ptr<int>> move_only2(std::move(move_only1));
  // move_only1 = move_only2;
  EXPECT_FALSE((bool)move_only1);
  ASSERT_TRUE((bool)move_only2);
  EXPECT_FALSE(move_only2->get() == nullptr);
  EXPECT_FALSE((*move_only2).get() == nullptr);
  const auto& ref = move_only2;
  EXPECT_FALSE(move_only2->get() == nullptr);
  EXPECT_FALSE((*move_only2).get() == nullptr);
  (void)ref;
}

TEST(option_forward) {
  wcl::optional<std::pair<int, int>> opair(wcl::in_place_t{}, 10, 10);
  EXPECT_TRUE((bool)opair);
  EXPECT_EQUAL(std::make_pair(10, 10), *opair);
}

struct NoConstruct {
  NoConstruct() = delete;
  NoConstruct(const NoConstruct&) = delete;
  NoConstruct(NoConstruct&&) = delete;
  ~NoConstruct() {
    std::cerr << "You weren't ever supposed to be able to construct such a thing!!!" << std::endl;
    exit(1);
  }
};

TEST(option_no_construct) {
  wcl::optional<NoConstruct> none1;
  wcl::optional<NoConstruct> none2;
  EXPECT_FALSE((bool)none1);
  EXPECT_FALSE((bool)none2);
}

struct SetOnDestruct {
  const char*& msg;
  const char* on_destruct = nullptr;

  SetOnDestruct() = delete;
  SetOnDestruct(const char*& msg, const char* on_destruct) : msg(msg), on_destruct(on_destruct) {}

  ~SetOnDestruct() { msg = on_destruct; }
};

class ConstructDestructCount {
 private:
  int* count = nullptr;

 public:
  ConstructDestructCount() = default;
  ConstructDestructCount(int& count) : count(&count) { ++*this->count; }
  ConstructDestructCount(const ConstructDestructCount& other) : count(other.count) {
    ++*this->count;
  }
  ConstructDestructCount(ConstructDestructCount&& other) : count(other.count) {
    other.count = nullptr;
  }
  ConstructDestructCount& operator=(const ConstructDestructCount& other) {
    if (count) --*count;
    count = other.count;
    ++*count;
    return *this;
  }
  ConstructDestructCount& operator=(ConstructDestructCount&& other) {
    if (count) --*count;
    count = other.count;
    other.count = nullptr;
    return *this;
  }
  ~ConstructDestructCount() {
    if (count) --*count;
  }
  void swap(ConstructDestructCount& other) { std::swap(count, other.count); }
};

TEST(option_destructs) {
  const char* msg;
  const char* expected = "this is the expected string";
  int counter = 0;

  // First some really basic tests
  {
    wcl::optional<SetOnDestruct> msg_setter(wcl::in_place_t{}, msg, expected);
    wcl::optional<ConstructDestructCount> ocounter(wcl::in_place_t{}, counter);
  }
  EXPECT_EQUAL(msg, expected);
  ASSERT_EQUAL(0, counter);

  // Now some basic assignment tests.
  {
    wcl::optional<ConstructDestructCount> counter1(wcl::in_place_t{}, counter);
    EXPECT_EQUAL(1, counter);
    wcl::optional<ConstructDestructCount> counter2;
    counter2 = counter1;
    EXPECT_EQUAL(2, counter);
  }
  ASSERT_EQUAL(0, counter);
  {
    wcl::optional<ConstructDestructCount> counter1(wcl::in_place_t{}, counter);
    EXPECT_EQUAL(1, counter);
    wcl::optional<ConstructDestructCount> counter2;
    counter2 = std::move(counter1);
    EXPECT_EQUAL(1, counter);
  }
  ASSERT_EQUAL(0, counter);

  // Now do something complicated to really stress the counter.
  {
    std::vector<wcl::optional<ConstructDestructCount>> counters;
    for (int i = 0; i < 1000; ++i) {
      counters.emplace_back(wcl::in_place_t{}, counter);
    }
    EXPECT_EQUAL(1000, counter);
    std::mt19937 gen;
    for (int i = 0; i < 10; ++i) std::shuffle(counters.begin(), counters.end(), gen);
    EXPECT_EQUAL(1000, counter);
  }
  ASSERT_EQUAL(0, counter);
}

TEST(option_assign1) {
  // Basic copy
  wcl::optional<int> some1(wcl::in_place_t{}, 10);
  wcl::optional<int> some2;
  EXPECT_FALSE((bool)some2);
  some2 = some1;
  ASSERT_TRUE((bool)some1);
  ASSERT_TRUE((bool)some2);
  EXPECT_EQUAL(*some1, *some2);
}

TEST(option_assign2) {
  // Basic copy
  int counter = 0;
  {
    wcl::optional<ConstructDestructCount> some1(wcl::in_place_t{}, counter);
    wcl::optional<ConstructDestructCount> some2;

    EXPECT_TRUE((bool)some1);
    EXPECT_FALSE((bool)some2);
    EXPECT_EQUAL(1, counter);

    some2 = some1;
    EXPECT_TRUE((bool)some1);
    EXPECT_TRUE((bool)some2);
    EXPECT_EQUAL(2, counter);

    some1 = some2;
    EXPECT_TRUE((bool)some1);
    EXPECT_TRUE((bool)some2);
    EXPECT_EQUAL(2, counter);

    some1 = wcl::optional<ConstructDestructCount>();
    EXPECT_TRUE((bool)some2);
    EXPECT_FALSE((bool)some1);
    EXPECT_EQUAL(1, counter);
  }
  EXPECT_EQUAL(0, counter);
}
