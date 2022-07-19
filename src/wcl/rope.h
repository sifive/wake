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

#include <cassert>
#include <memory>
#include <sstream>
#include <vector>

namespace wcl {

class rope_impl_base {
 protected:
  // On construction this has to be set
  size_t length;

  rope_impl_base(size_t len) : length(len) {}

 public:
  virtual ~rope_impl_base() = default;

  // methods that all RopeImpl share
  virtual void write(std::ostream&) const = 0;
  size_t size() const { return length; }
};

class rope_impl_string : public rope_impl_base {
 private:
  std::string str;

 public:
  explicit rope_impl_string(std::string str) : rope_impl_base(str.size()), str(str) {}
  void write(std::ostream& ostream) const override { ostream << str; }
};

class rope_impl_pair : public rope_impl_base {
  // this concats two ropes togethor
 private:
  std::pair<std::shared_ptr<rope_impl_base>, std::shared_ptr<rope_impl_base>> pair;

 public:
  rope_impl_pair(std::shared_ptr<rope_impl_base> left, std::shared_ptr<rope_impl_base> right)
      : rope_impl_base(left->size() + right->size()),
        pair(std::make_pair(std::move(left), std::move(right))) {}
  void write(std::ostream& ostream) const override {
    pair.first->write(ostream);
    pair.second->write(ostream);
  }
};

// `rope` is a very efficient data structure for editing strings. It is ideal to use when
// repeatedly editing very long strings. Converting rope->string and string->rope is expensive
// thus the majority of work should be done within the rope structure.
//
// It supports O(1) concatination of two ropes and O(1) size lookup.
// Rope->string and string->rope are O(n) operations.
//
// Examples:
// ```
// rope r1 = rope::lit("first");
// rope r2 = rope::lit("-second");
// rope r3 = r1.concat(r2);
// r3.size() -> 13
// r3.as_string() -> "first-second"
// ```
class rope {
 private:
  std::shared_ptr<rope_impl_base> impl;

  explicit rope(std::unique_ptr<rope_impl_base> impl) : impl(std::move(impl)) {}

 public:
  // O(1) but constructing the string takes O(n)
  static rope lit(std::string str) {
    return rope(std::make_unique<rope_impl_string>(std::move(str)));
  }
  // O(1)
  rope concat(rope r) const { return rope(std::make_unique<rope_impl_pair>(impl, r.impl)); }

  // O(n)
  std::string as_string() const {
    std::stringstream ss;
    impl->write(ss);
    return ss.str();
  }

  // O(n)
  void write(std::ostream& ostream) const { impl->write(ostream); }

  // O(1)
  size_t size() const { return impl->size(); }
};

// `rope_builder` is a convenient wrapper around `rope`. It simplifies the API for building up
// a rope from several parts.
//
// Examples:
// ```
// rope_builder b1;
// b1.append("Hello");
// b1.append(" ");
//
// rope_builder b2;
// b2.append("World");
// b2.append("!");
//
// b1.append(std::move(b2).build());
// rope r = std::move(b1).build();
// r.as_string() -> "Hello World!"
// ```
class rope_builder {
 private:
  std::vector<rope> ropes;

  rope merge(int start, int end) {
    if (start >= end) {
      return ropes[start];
    }

    int middle = (start + end) / 2;

    rope left = merge(start, middle);
    rope right = merge(middle + 1, end);

    return left.concat(right);
  }

 public:
  void append(std::string str) { ropes.push_back(rope::lit(std::move(str))); }
  void append(rope other) { ropes.push_back(std::move(other)); }
  void undo() { ropes.pop_back(); }

  rope build() && {
    assert(ropes.size() > 0);
    rope copy = merge(0, ropes.size() - 1);
    ropes = {};
    return copy;
  }
};

}  // namespace wcl
