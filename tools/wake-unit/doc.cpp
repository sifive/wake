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

#include <wcl/doc.h>

#include "unit.h"

TEST(doc_basic) {
  wcl::doc_builder builder;
  builder.append("Hello");
  builder.append(" ");
  builder.append("World");
  builder.append("!");

  {
    wcl::doc_builder other;
    other.append("My name is");
    other.append(" Ashley");
    wcl::doc d = std::move(other).build();
    builder.append(" ");
    builder.append(d);
  }

  wcl::doc d = std::move(builder).build();
  std::string expected = "Hello World! My name is Ashley";

  EXPECT_EQUAL(expected.size(), d.character_count());
  EXPECT_EQUAL(expected, d.as_string());
}

TEST(doc_large) {
  wcl::doc_builder builder;
  for (int i = 0; i < 1000; i++) {
    wcl::doc_builder a;
    for (int j = 0; j < 1000; j++) {
      a.append("a");
    }

    wcl::doc_builder b;
    for (int j = 0; j < 1000; j++) {
      b.append("b");
    }

    wcl::doc_builder c;
    for (int j = 0; j < 1000; j++) {
      c.append("c");
    }

    a.append(std::move(b).build());
    a.append(std::move(c).build());
    builder.append(std::move(a).build());
  }

  wcl::doc d = std::move(builder).build();
  ASSERT_EQUAL(3000000u, d.character_count());
}

TEST(doc_undo) {
  wcl::doc_builder builder;
  builder.append("Hello");
  builder.append(" ");
  builder.append("World");
  builder.append("!");

  builder.undo();
  builder.undo();

  wcl::doc d = std::move(builder).build();
  std::string expected = "Hello ";

  EXPECT_EQUAL(expected.size(), d.character_count());
  EXPECT_EQUAL(expected, d.as_string());
}

TEST(doc_geometry) {
  {
    wcl::doc_builder builder;
    builder.append("Hello");

    EXPECT_EQUAL(5u, builder.first_width());
    EXPECT_EQUAL(5u, builder.last_width());
    EXPECT_EQUAL(5u, builder.max_width());
    EXPECT_EQUAL(0u, builder.newline_count());
    EXPECT_EQUAL(1u, builder.height());

    wcl::doc d = std::move(builder).build();

    EXPECT_EQUAL(5u, d.first_width());
    EXPECT_EQUAL(5u, d.last_width());
    EXPECT_EQUAL(5u, d.max_width());
    EXPECT_EQUAL(0u, d.newline_count());
    EXPECT_EQUAL(1u, d.height());
  }

  {
    wcl::doc_builder builder;
    builder.append("Hello\n");

    EXPECT_EQUAL(5u, builder.first_width());
    EXPECT_EQUAL(0u, builder.last_width());
    EXPECT_EQUAL(5u, builder.max_width());
    EXPECT_EQUAL(1u, builder.newline_count());
    EXPECT_EQUAL(2u, builder.height());

    wcl::doc d = std::move(builder).build();

    EXPECT_EQUAL(5u, d.first_width());
    EXPECT_EQUAL(0u, d.last_width());
    EXPECT_EQUAL(5u, d.max_width());
    EXPECT_EQUAL(1u, d.newline_count());
    EXPECT_EQUAL(2u, d.height());
  }

  {
    wcl::doc_builder builder;
    builder.append("Hello\n");
    builder.append("World!");

    EXPECT_EQUAL(5u, builder.first_width());
    EXPECT_EQUAL(6u, builder.last_width());
    EXPECT_EQUAL(6u, builder.max_width());
    EXPECT_EQUAL(1u, builder.newline_count());
    EXPECT_EQUAL(2u, builder.height());

    wcl::doc d = std::move(builder).build();

    EXPECT_EQUAL(5u, d.first_width());
    EXPECT_EQUAL(6u, d.last_width());
    EXPECT_EQUAL(6u, d.max_width());
    EXPECT_EQUAL(1u, d.newline_count());
    EXPECT_EQUAL(2u, d.height());
  }

  {
    wcl::doc_builder builder;
    builder.append("Hello");
    builder.append("\nWorld!");

    EXPECT_EQUAL(5u, builder.first_width());
    EXPECT_EQUAL(6u, builder.last_width());
    EXPECT_EQUAL(6u, builder.max_width());
    EXPECT_EQUAL(1u, builder.newline_count());
    EXPECT_EQUAL(2u, builder.height());

    wcl::doc d = std::move(builder).build();

    EXPECT_EQUAL(5u, d.first_width());
    EXPECT_EQUAL(6u, d.last_width());
    EXPECT_EQUAL(6u, d.max_width());
    EXPECT_EQUAL(1u, d.newline_count());
    EXPECT_EQUAL(2u, d.height());
  }

  {
    wcl::doc_builder builder;
    builder.append("He\nllo");
    builder.append("Worl\nd!");

    EXPECT_EQUAL(2u, builder.first_width());
    EXPECT_EQUAL(2u, builder.last_width());
    EXPECT_EQUAL(7u, builder.max_width());
    EXPECT_EQUAL(2u, builder.newline_count());
    EXPECT_EQUAL(3u, builder.height());

    wcl::doc d = std::move(builder).build();

    EXPECT_EQUAL(2u, d.first_width());
    EXPECT_EQUAL(2u, d.last_width());
    EXPECT_EQUAL(7u, d.max_width());
    EXPECT_EQUAL(2u, d.newline_count());
    EXPECT_EQUAL(3u, d.height());
  }

  {
    wcl::doc_builder builder;
    builder.append("Hello");
    builder.append("\nHello");
    builder.append("Hello");
    builder.append("Hello\n");
    builder.append("Hello");
    builder.append("\nWorld!");
    builder.append("\nWorld!");
    builder.append("\nWorld!");
    builder.append("World!");
    builder.append("World!");
    builder.append("World!");
    builder.append("World!");
    builder.append("\n");
    builder.append("Hello");
    builder.append("\n");
    builder.append("123");

    EXPECT_EQUAL(5u, builder.first_width());
    EXPECT_EQUAL(3u, builder.last_width());
    EXPECT_EQUAL(30u, builder.max_width());
    EXPECT_EQUAL(7u, builder.newline_count());
    EXPECT_EQUAL(8u, builder.height());

    wcl::doc d = std::move(builder).build();

    EXPECT_EQUAL(5u, d.first_width());
    EXPECT_EQUAL(3u, d.last_width());
    EXPECT_EQUAL(30u, d.max_width());
    EXPECT_EQUAL(7u, d.newline_count());
    EXPECT_EQUAL(8u, d.height());
  }

  {
    wcl::doc_builder builder;
    builder.append("Hello");
    EXPECT_EQUAL(5u, builder.first_width());
    EXPECT_EQUAL(5u, builder.last_width());
    EXPECT_EQUAL(5u, builder.max_width());
    EXPECT_EQUAL(0u, builder.newline_count());
    EXPECT_EQUAL(1u, builder.height());

    builder.append("\nHello");
    EXPECT_EQUAL(5u, builder.first_width());
    EXPECT_EQUAL(5u, builder.last_width());
    EXPECT_EQUAL(5u, builder.max_width());
    EXPECT_EQUAL(1u, builder.newline_count());
    EXPECT_EQUAL(2u, builder.height());

    builder.append("Hello");
    EXPECT_EQUAL(5u, builder.first_width());
    EXPECT_EQUAL(10u, builder.last_width());
    EXPECT_EQUAL(10u, builder.max_width());
    EXPECT_EQUAL(1u, builder.newline_count());
    EXPECT_EQUAL(2u, builder.height());

    builder.append("Hello\n");
    EXPECT_EQUAL(5u, builder.first_width());
    EXPECT_EQUAL(0u, builder.last_width());
    EXPECT_EQUAL(15u, builder.max_width());
    EXPECT_EQUAL(2u, builder.newline_count());
    EXPECT_EQUAL(3u, builder.height());

    builder.append("Hello");
    EXPECT_EQUAL(5u, builder.first_width());
    EXPECT_EQUAL(5u, builder.last_width());
    EXPECT_EQUAL(15u, builder.max_width());
    EXPECT_EQUAL(2u, builder.newline_count());
    EXPECT_EQUAL(3u, builder.height());

    builder.append("\nWorld!");
    EXPECT_EQUAL(5u, builder.first_width());
    EXPECT_EQUAL(6u, builder.last_width());
    EXPECT_EQUAL(15u, builder.max_width());
    EXPECT_EQUAL(3u, builder.newline_count());
    EXPECT_EQUAL(4u, builder.height());

    builder.append("\nWorld!");
    EXPECT_EQUAL(5u, builder.first_width());
    EXPECT_EQUAL(6u, builder.last_width());
    EXPECT_EQUAL(15u, builder.max_width());
    EXPECT_EQUAL(4u, builder.newline_count());
    EXPECT_EQUAL(5u, builder.height());

    builder.append("\nWorld!");
    EXPECT_EQUAL(5u, builder.first_width());
    EXPECT_EQUAL(6u, builder.last_width());
    EXPECT_EQUAL(15u, builder.max_width());
    EXPECT_EQUAL(5u, builder.newline_count());
    EXPECT_EQUAL(6u, builder.height());

    builder.append("World!");
    EXPECT_EQUAL(5u, builder.first_width());
    EXPECT_EQUAL(12u, builder.last_width());
    EXPECT_EQUAL(15u, builder.max_width());
    EXPECT_EQUAL(5u, builder.newline_count());
    EXPECT_EQUAL(6u, builder.height());

    builder.append("World!");
    EXPECT_EQUAL(5u, builder.first_width());
    EXPECT_EQUAL(18u, builder.last_width());
    EXPECT_EQUAL(18u, builder.max_width());
    EXPECT_EQUAL(5u, builder.newline_count());
    EXPECT_EQUAL(6u, builder.height());

    builder.append("World!");
    EXPECT_EQUAL(5u, builder.first_width());
    EXPECT_EQUAL(24u, builder.last_width());
    EXPECT_EQUAL(24u, builder.max_width());
    EXPECT_EQUAL(5u, builder.newline_count());
    EXPECT_EQUAL(6u, builder.height());

    builder.append("World!");
    EXPECT_EQUAL(5u, builder.first_width());
    EXPECT_EQUAL(30u, builder.last_width());
    EXPECT_EQUAL(30u, builder.max_width());
    EXPECT_EQUAL(5u, builder.newline_count());
    EXPECT_EQUAL(6u, builder.height());

    builder.append("\n");
    EXPECT_EQUAL(5u, builder.first_width());
    EXPECT_EQUAL(0u, builder.last_width());
    EXPECT_EQUAL(30u, builder.max_width());
    EXPECT_EQUAL(6u, builder.newline_count());
    EXPECT_EQUAL(7u, builder.height());

    builder.append("Hello");
    EXPECT_EQUAL(5u, builder.first_width());
    EXPECT_EQUAL(5u, builder.last_width());
    EXPECT_EQUAL(30u, builder.max_width());
    EXPECT_EQUAL(6u, builder.newline_count());
    EXPECT_EQUAL(7u, builder.height());

    builder.append("\n");
    EXPECT_EQUAL(5u, builder.first_width());
    EXPECT_EQUAL(0u, builder.last_width());
    EXPECT_EQUAL(30u, builder.max_width());
    EXPECT_EQUAL(7u, builder.newline_count());
    EXPECT_EQUAL(8u, builder.height());

    builder.append("123");
    EXPECT_EQUAL(5u, builder.first_width());
    EXPECT_EQUAL(3u, builder.last_width());
    EXPECT_EQUAL(30u, builder.max_width());
    EXPECT_EQUAL(7u, builder.newline_count());
    EXPECT_EQUAL(8u, builder.height());

    builder.undo();
    EXPECT_EQUAL(5u, builder.first_width());
    EXPECT_EQUAL(0u, builder.last_width());
    EXPECT_EQUAL(30u, builder.max_width());
    EXPECT_EQUAL(7u, builder.newline_count());
    EXPECT_EQUAL(8u, builder.height());

    builder.undo();
    EXPECT_EQUAL(5u, builder.first_width());
    EXPECT_EQUAL(5u, builder.last_width());
    EXPECT_EQUAL(30u, builder.max_width());
    EXPECT_EQUAL(6u, builder.newline_count());
    EXPECT_EQUAL(7u, builder.height());

    builder.undo();
    EXPECT_EQUAL(5u, builder.first_width());
    EXPECT_EQUAL(0u, builder.last_width());
    EXPECT_EQUAL(30u, builder.max_width());
    EXPECT_EQUAL(6u, builder.newline_count());
    EXPECT_EQUAL(7u, builder.height());

    builder.undo();
    EXPECT_EQUAL(5u, builder.first_width());
    EXPECT_EQUAL(30u, builder.last_width());
    EXPECT_EQUAL(30u, builder.max_width());
    EXPECT_EQUAL(5u, builder.newline_count());
    EXPECT_EQUAL(6u, builder.height());

    builder.undo();
    EXPECT_EQUAL(5u, builder.first_width());
    EXPECT_EQUAL(24u, builder.last_width());
    EXPECT_EQUAL(24u, builder.max_width());
    EXPECT_EQUAL(5u, builder.newline_count());
    EXPECT_EQUAL(6u, builder.height());

    wcl::doc d = std::move(builder).build();
    EXPECT_EQUAL(5u, d.first_width());
    EXPECT_EQUAL(24u, d.last_width());
    EXPECT_EQUAL(24u, d.max_width());
    EXPECT_EQUAL(5u, d.newline_count());
    EXPECT_EQUAL(6u, d.height());
  }
}
