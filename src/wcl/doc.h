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

#pragma once

#include <cassert>
#include <memory>
#include <sstream>
#include <vector>

#include "doc_state.h"
#include "utf8proc/utf8proc.h"

namespace wcl {
class doc_impl_base {
 protected:
  doc_state state_;

  doc_impl_base(doc_state state) : state_(state) {}

 public:
  virtual ~doc_impl_base() = default;

  // methods that all RopeImpl share
  virtual void write(std::ostream&) const = 0;

  const doc_state& operator->() const { return state_; }
  const doc_state& operator*() const { return state_; }
};

class doc_impl_string : public doc_impl_base {
 private:
  std::string str;

 public:
  explicit doc_impl_string(std::string str)
      : doc_impl_base(from_string<doc_state>(str)), str(str) {}
  void write(std::ostream& ostream) const override { ostream << str; }
};

class doc_impl_pair : public doc_impl_base {
  // this concats two docs together
 private:
  std::pair<std::shared_ptr<doc_impl_base>, std::shared_ptr<doc_impl_base>> pair;

 public:
  doc_impl_pair(std::shared_ptr<doc_impl_base> left, std::shared_ptr<doc_impl_base> right)
      : doc_impl_base(**left + **right), pair(std::make_pair(std::move(left), std::move(right))) {}

  void write(std::ostream& ostream) const override {
    pair.first->write(ostream);
    pair.second->write(ostream);
  }
};

// Forward declare doc_builder so it can be named as a friend of doc
class doc_builder;

// `doc` is a very efficient data structure for constructing strings. It is ideal to use when
// repeatedly building very long strings. Converting doc->string and string->doc is expensive
// thus the majority of work should be done within the doc structure.
//
// It supports O(1) concatination of two docs and O(1) geometry lookup.
// Rope->string and string->doc are O(n) operations.
//
// Examples:
// ```
// doc d1 = doc::lit("first");
// doc d2 = doc::lit("-second");
// doc d3 = d1.concat(d2);
// d3.byte_count() -> 12
// d3.as_string() -> "first-second"
// ```
class doc {
 private:
  std::shared_ptr<doc_impl_base> impl;

  explicit doc(std::unique_ptr<doc_impl_base> impl) : impl(std::move(impl)) {}

 public:
  doc(const doc& other) = default;

  // O(n) (n = character count)
  static doc lit(std::string str) { return doc(std::make_unique<doc_impl_string>(std::move(str))); }

  // O(1)
  doc concat(doc r) const { return doc(std::make_unique<doc_impl_pair>(impl, r.impl)); }

  // O(n)
  std::string as_string() const {
    std::stringstream ss;
    impl->write(ss);
    return ss.str();
  }

  // O(n)
  void write(std::ostream& ostream) const { impl->write(ostream); }

  const doc_state& operator->() const { return **impl; }
  const doc_state& operator*() const { return **impl; }

  friend doc_builder;
};

// `doc_builder` is a convenient wrapper around `doc`. It simplifies the API for building up
// a doc from several parts.
//
// Examples:
// ```
// doc_builder b1;
// b1.append("Hello");
// b1.append(" ");
//
// doc_builder b2;
// b2.append("World");
// b2.append("!");
//
// b1.append(std::move(b2).build());
// doc d = std::move(b1).build();
// d.as_string() -> "Hello World!"
// ```
class doc_builder {
 private:
  std::vector<doc> docs;
  doc_state state = doc_state::identity();

  doc merge(int start, int end) {
    if (start > end) {
      return doc::lit("");
    }

    if (start == end) {
      return docs[start];
    }

    int middle = (start + end) / 2;

    doc left = merge(start, middle);
    doc right = merge(middle + 1, end);

    return left.concat(right);
  }

 public:
  void append(std::string str) { append(doc::lit(std::move(str))); }

  void append(doc other) {
    state = state + *other;
    docs.push_back(std::move(other));
  }

  void undo() {
    docs.pop_back();
    state = {};
    for (const auto& r : docs) {
      state = state + *r;
    }
  }

  const doc_state& operator->() const { return state; }
  const doc_state& operator*() const { return state; }

  doc build() && {
    doc copy = merge(0, docs.size() - 1);
    docs = {};
    state = doc_state::identity();
    return copy;
  }
};

}  // namespace wcl
