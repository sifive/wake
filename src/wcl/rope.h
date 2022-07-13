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

#include <iostream>
#include <memory>

namespace wcl {

class rope_impl_base {
 protected:
  // On construction this has to be set
  size_t length;

  rope_impl_base(size_t len) : length(len) {}

 public:
  // methods that all RopeImpl share
  virtual void write(std::ostream&) const = 0;
  virtual std::string as_string() const = 0;
  size_t size() const { return length; }
};

class rope_impl_string : public rope_impl_base {
 private:
  std::string str;

 public:
  explicit rope_impl_string(std::string str) : rope_impl_base(str.size()), str(str) {}
  void write(std::ostream& ostream) const override { ostream << str; }

  std::string as_string() const override { return str; }
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
  std::string as_string() const override {
    return pair.first->as_string() + pair.second->as_string();
  }
};

class rope {
 private:
  std::shared_ptr<rope_impl_base> impl;

  explicit rope(std::unique_ptr<rope_impl_base> impl) : impl(std::move(impl)) {}

 public:
  // O(1) but constructing the string takes O(n)
  static rope lit(std::string&& str) {
    return rope(std::make_unique<rope_impl_string>(std::move(str)));
  }
  // O(1)
  rope concat(rope r) { return rope(std::make_unique<rope_impl_pair>(impl, r.impl)); }

  // O(n)
  std::string as_string() { return impl->as_string(); }

  // O(n)
  void write(std::ostream& ostream) const { impl->write(ostream); }

  // O(1)
  size_t size() { return impl->size(); }
};

class rope_builder {
 private:
  rope r = rope::lit("");

 public:
  void append(const char* str) { r = r.concat(rope::lit(str)); }
  void append(rope other) { r = r.concat(other); }

  rope build() {
    rope copy = r;
    r = rope::lit("");
    return copy;
  }
};

}  // namespace wcl
