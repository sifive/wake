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

#include <wcl/hash.h>

#include <cassert>
#include <string>

#include "utf8proc/utf8proc.h"

namespace wcl {
template <class State>
State from_string(const std::string& str) {
  State out = State::identity();

  const utf8proc_uint8_t* iter = reinterpret_cast<const utf8proc_uint8_t*>(str.c_str());
  const utf8proc_uint8_t* iter_end = iter + str.size();
  while (iter < iter_end) {
    utf8proc_int32_t codepoint;
    size_t size = utf8proc_iterate(iter, -1, &codepoint);
    iter += size;

    assert(size > 0);
    assert(codepoint > 0);  // < 0 means error parsing utf8

    out = out + State::inject(size, codepoint);
  }
  return out;
}

struct empty_state {
  empty_state operator+(empty_state other) const { return empty_state{}; }

  bool operator==(const empty_state& other) const { return true; }

  static empty_state identity() { return empty_state{}; }

  static empty_state inject(size_t size, utf8proc_int32_t codepoint) { return empty_state{}; }
};

struct byte_count_state {
  size_t count = 0;

  byte_count_state() = default;
  byte_count_state(size_t count) : count(count) {}

  byte_count_state operator+(byte_count_state other) const {
    return byte_count_state{count + other.count};
  }

  bool operator==(const byte_count_state& other) const { return count == other.count; }

  static byte_count_state identity() { return byte_count_state{}; }

  static byte_count_state inject(size_t size, utf8proc_int32_t codepoint) {
    return byte_count_state{size};
  }
};

struct newline_count_state {
  size_t count = 0;

  newline_count_state() = default;
  newline_count_state(size_t count) : count(count) {}

  newline_count_state operator+(newline_count_state other) const {
    return newline_count_state{count + other.count};
  }

  bool operator==(const newline_count_state& other) const { return count == other.count; }

  static newline_count_state identity() { return newline_count_state{}; }

  static newline_count_state inject(size_t size, utf8proc_int32_t codepoint) {
    return newline_count_state{codepoint == '\n'};
  }
};

class first_width_state {
 private:
  first_width_state(bool wrapped, size_t width) : wrapped(wrapped), width(width) {}

  bool wrapped = false;

 public:
  size_t width = 0;

  first_width_state() = default;

  first_width_state operator+(first_width_state other) const {
    if (wrapped) {
      return *this;
    }
    return first_width_state{other.wrapped, width + other.width};
  }

  bool operator==(const first_width_state& other) const {
    return wrapped == other.wrapped && width == other.width;
  }

  static first_width_state identity() { return first_width_state{}; }

  static first_width_state inject(size_t size, utf8proc_int32_t codepoint) {
    if (codepoint == '\n') return first_width_state{true, 0};
    return first_width_state{false, static_cast<size_t>(utf8proc_charwidth(codepoint))};
  }

  friend std::hash<first_width_state>;
};

class last_width_state {
 private:
  last_width_state(bool wrapped, size_t width) : wrapped(wrapped), width(width) {}

  bool wrapped = false;

 public:
  size_t width = 0;

  last_width_state() = default;

  last_width_state operator+(last_width_state other) const {
    if (other.wrapped) {
      return other;
    }
    return last_width_state{wrapped, width + other.width};
  }

  bool operator==(const last_width_state& other) const {
    return wrapped == other.wrapped && width == other.width;
  }

  static last_width_state identity() { return last_width_state{}; }

  static last_width_state inject(size_t size, utf8proc_int32_t codepoint) {
    if (codepoint == '\n') return last_width_state{true, 0};
    return last_width_state{false, static_cast<size_t>(utf8proc_charwidth(codepoint))};
  }

  friend std::hash<last_width_state>;
};

class doc_state {
 private:
  doc_state(byte_count_state byte_count, newline_count_state newline_count,
            first_width_state first_width, last_width_state last_width)
      : byte_count_(byte_count),
        newline_count_(newline_count),
        first_width_(first_width),
        last_width_(last_width) {}

  // Total number of bytes in the doc
  byte_count_state byte_count_;

  // Total number of newlines in the doc
  newline_count_state newline_count_;

  // Human visible "width" of the first line of the doc
  first_width_state first_width_;

  // Human visible "width" of the last line of the doc
  last_width_state last_width_;

 public:
  doc_state() = default;

  doc_state operator+(doc_state other) const {
    return doc_state{byte_count_ + other.byte_count_, newline_count_ + other.newline_count_,
                     first_width_ + other.first_width_, last_width_ + other.last_width_};
  }

  bool operator==(const doc_state& other) const {
    return byte_count_ == other.byte_count_ && newline_count_ == other.newline_count_ &&
           first_width_ == other.first_width_ && last_width_ == other.last_width_;
  }

  const doc_state* operator->() const { return this; }

  static doc_state identity() {
    return doc_state{byte_count_state::identity(), newline_count_state::identity(),
                     first_width_state::identity(), last_width_state::identity()};
  }

  static doc_state inject(size_t size, utf8proc_int32_t codepoint) {
    return doc_state{
        byte_count_state::inject(size, codepoint), newline_count_state::inject(size, codepoint),
        first_width_state::inject(size, codepoint), last_width_state::inject(size, codepoint)};
  }

  size_t byte_count() const { return byte_count_.count; }
  size_t newline_count() const { return newline_count_.count; }
  size_t first_width() const { return first_width_.width; }
  size_t last_width() const { return last_width_.width; }
  bool has_newline() const { return newline_count() > 0; }
  size_t height() const { return newline_count() + 1; }

  friend std::hash<doc_state>;
};

}  // namespace wcl

template <>
struct std::hash<wcl::empty_state> {
  size_t operator()(wcl::empty_state const& state) const noexcept { return 0; }
};

template <>
struct std::hash<wcl::byte_count_state> {
  size_t operator()(wcl::byte_count_state const& state) const noexcept {
    return std::hash<size_t>{}(state.count);
  }
};

template <>
struct std::hash<wcl::newline_count_state> {
  size_t operator()(wcl::newline_count_state const& state) const noexcept {
    return std::hash<size_t>{}(state.count);
  }
};

template <>
struct std::hash<wcl::first_width_state> {
  size_t operator()(wcl::first_width_state const& state) const noexcept {
    return wcl::hash_combine(std::hash<bool>{}(state.wrapped), std::hash<size_t>{}(state.width));
  }
};

template <>
struct std::hash<wcl::last_width_state> {
  size_t operator()(wcl::last_width_state const& state) const noexcept {
    return wcl::hash_combine(std::hash<bool>{}(state.wrapped), std::hash<size_t>{}(state.width));
  }
};

template <>
struct std::hash<wcl::doc_state> {
  size_t operator()(wcl::doc_state const& state) const noexcept {
    size_t hash = wcl::hash_combine(std::hash<wcl::byte_count_state>{}(state.byte_count_),
                                    std::hash<wcl::newline_count_state>{}(state.newline_count_));
    hash = wcl::hash_combine(hash, std::hash<wcl::first_width_state>{}(state.first_width_));
    hash = wcl::hash_combine(hash, std::hash<wcl::last_width_state>{}(state.last_width_));
    return hash;
  }
};
