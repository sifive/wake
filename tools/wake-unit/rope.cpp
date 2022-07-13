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

#include <wcl/rope.h>

#include "unit.h"

TEST(rope_basic) {
  wcl::rope_builder builder;
  builder.append("Hello");
  builder.append(" ");
  builder.append("World");
  builder.append("!");

  {
    wcl::rope_builder other;
    other.append("My name is");
    other.append(" John");
    wcl::rope r = other.build();
    builder.append(" ");
    builder.append(r);
  }

  wcl::rope r = builder.build();
  std::string expected = "Hello World! My name is John";
  ASSERT_EQUAL(expected.size(), r.size());
  ASSERT_EQUAL(expected, r.as_string());
}

TEST(rope_builder_build_once) {
  wcl::rope_builder builder;
  builder.append("Hello");
  builder.append(" ");
  builder.append("World");
  builder.append("!");
  wcl::rope r = builder.build();

  std::string expected = "Hello World!";
  ASSERT_EQUAL(expected.size(), r.size());
  ASSERT_EQUAL(expected, r.as_string());

  r = builder.build();

  expected = "";
  ASSERT_EQUAL(expected.size(), r.size());
  ASSERT_EQUAL(expected, r.as_string());
}

TEST(rope_large) {
  wcl::rope_builder builder;
  for (int i = 0; i < 1000; i++) {
    wcl::rope_builder a;
    for (int j = 0; j < 1000; j++) {
      a.append("a");
    }

    wcl::rope_builder b;
    for (int j = 0; j < 1000; j++) {
      b.append("b");
    }

    wcl::rope_builder c;
    for (int j = 0; j < 1000; j++) {
      c.append("c");
    }

    a.append(b.build());
    a.append(c.build());
    builder.append(a.build());
  }

  wcl::rope r = builder.build();
  ASSERT_EQUAL(3000000u, r.size());
}
