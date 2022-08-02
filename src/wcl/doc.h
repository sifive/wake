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

namespace wcl {

// Internal state for a doc object. It is intentionally not exposed in the
// public API and should not be used directly.
struct doc_state {
  size_t character_count = 0;
  size_t newline_count = 0;
  size_t first_width = 0;
  size_t last_width = 0;
  size_t max_width = 0;

  // Returns the merged state of this and other.
  doc_state merged(doc_state other) {
    doc_state merged;

    merged.character_count = character_count + other.character_count;
    merged.newline_count = newline_count + other.newline_count;

    merged.last_width = other.last_width;
    // If other doesn't have any newlines, then the left side needs to be accounted for
    if (other.newline_count == 0) {
      merged.last_width += last_width;
    }

    merged.first_width = first_width;
    // If the left side doesn't have any newlines, then other side needs to be accounted for
    if (newline_count == 0) {
      merged.first_width += other.first_width;
    }

    merged.max_width = std::max(max_width, other.max_width);
    // It is possible that the join point is a longer line than any other in either doc
    merged.max_width = std::max(merged.max_width, last_width + other.first_width);

    return merged;
  }

  static doc_state from_string(const std::string& str) {
    doc_state state;

    state.character_count = str.size();

    for (auto& c : str) {
      if (c == '\n') {
        // once we see a newline, check if the line was larger than any seen before
        state.max_width = std::max(state.max_width, state.last_width);

        state.last_width = 0;
        state.newline_count++;
        continue;
      }

      if (state.newline_count == 0) {
        state.first_width++;
      }

      state.last_width++;
    }

    // Account for the case when the last line is the longest w/o a nl.
    // ex: "a <newline> b <newline> cdef"
    state.max_width = std::max(state.max_width, state.last_width);
    return state;
  }
};

class doc_impl_base {
 protected:
  doc_state state_;

  doc_impl_base(doc_state state) : state_(state) {}

 public:
  virtual ~doc_impl_base() = default;

  // methods that all RopeImpl share
  virtual void write(std::ostream&) const = 0;

  size_t character_count() const { return state_.character_count; }
  size_t newline_count() const { return state_.newline_count; }
  size_t first_width() const { return state_.first_width; }
  size_t last_width() const { return state_.last_width; }
  size_t max_width() const { return state_.max_width; }

  doc_state state() const { return state_; }
  bool has_newline() const { return state_.newline_count > 0; }
};

class doc_impl_string : public doc_impl_base {
 private:
  std::string str;

 public:
  explicit doc_impl_string(std::string str)
      : doc_impl_base(doc_state::from_string(str)), str(str) {}
  void write(std::ostream& ostream) const override { ostream << str; }
};

class doc_impl_pair : public doc_impl_base {
  // this concats two docs together
 private:
  std::pair<std::shared_ptr<doc_impl_base>, std::shared_ptr<doc_impl_base>> pair;

 public:
  doc_impl_pair(std::shared_ptr<doc_impl_base> left, std::shared_ptr<doc_impl_base> right)
      : doc_impl_base(left->state().merged(right->state())),
        pair(std::make_pair(std::move(left), std::move(right))) {}

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
// d3.character_count() -> 13
// d3.as_string() -> "first-second"
// ```
class doc {
 private:
  std::shared_ptr<doc_impl_base> impl;

  doc_state state() const { return impl->state(); }

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

  // O(1)
  size_t first_width() const { return impl->first_width(); }

  // O(1)
  size_t last_width() const { return impl->last_width(); }

  // O(1)
  size_t max_width() const { return impl->max_width(); }

  // O(1)
  size_t character_count() const { return impl->character_count(); }

  // O(1)
  size_t newline_count() const { return impl->newline_count(); }

  // O(1)
  bool has_newline() const { return impl->has_newline(); }

  // O(1)
  size_t height() const { return newline_count() + 1; }

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
  doc_state state;

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
    state = state.merged(other.state());
    docs.push_back(std::move(other));
  }

  void undo() {
    docs.pop_back();
    state = {};
    for (const auto& r : docs) {
      state = state.merged(r.state());
    }
  }

  size_t first_width() const { return state.first_width; }
  size_t last_width() const { return state.last_width; }
  size_t max_width() const { return state.max_width; }
  size_t character_count() const { return state.character_count; }
  size_t newline_count() const { return state.newline_count; }
  size_t height() const { return state.newline_count + 1; }
  bool has_newline() const { return state.newline_count > 0; }

  doc build() && {
    doc copy = merge(0, docs.size() - 1);
    docs = {};
    state = {};
    return copy;
  }
};

}  // namespace wcl
