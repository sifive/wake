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

#include <cstdint>
#include <iostream>
#include <utility>

namespace wcl {

struct in_place_t {
  explicit in_place_t() = default;
};

template <class T, bool copy, bool move>
class optional_base {};

template <class T>
class alignas(T) optional_base<T, true, true> {
 protected:
  union {
    in_place_t null_value;
    T value;
  };
  bool none = true;

 public:
  ~optional_base() {
    if (!none) value.~T();
  }
  optional_base() : null_value(), none(true) {}
  optional_base(const optional_base& other) : null_value(), none(other.none) {
    if (!none) new (&value) T(other.value);
  }
  optional_base(optional_base&& other) : null_value(), none(other.none) {
    if (!none) {
      new (&value) T(std::move(other.value));
      other.value.~T();
      other.none = true;
    }
  }

  optional_base& operator=(const optional_base& other) {
    if (!none) value.~T();
    none = other.none;
    if (none) return *this;
    new (&value) T(other.value);
    return *this;
  }

  optional_base& operator=(optional_base&& other) {
    if (!none) value.~T();
    none = other.none;
    if (none) return *this;
    new (&value) T(std::move(other.value));
    other.value.~T();
    other.none = true;
    return *this;
  }

  template <class... Args>
  optional_base(in_place_t, Args&&... args) : value(std::forward<Args>(args)...), none(false) {}
};

template <class T>
class alignas(T) optional_base<T, true, false> {
 protected:
  union {
    uint8_t null_value;
    T value;
  };
  bool none = true;

 public:
  ~optional_base() {
    if (!none) value.~T();
  }
  optional_base() : null_value(), none(true) {}
  optional_base(const optional_base& other) : null_value(), none(other.none) {
    if (!none) new (&value) T(other.value);
  }
  optional_base(optional_base&&) = delete;

  optional_base& operator=(const optional_base& other) {
    if (!none) value.~T();
    none = other.none;
    if (none) return *this;
    new (&value) T(other.value);
    return *this;
  }

  optional_base& operator=(optional_base&& other) = delete;

  template <class... Args>
  optional_base(in_place_t, Args&&... args) : value(std::forward<Args>(args)...), none(false) {}
};

template <class T>
class alignas(T) optional_base<T, false, true> {
 protected:
  union {
    in_place_t null_value;
    T value;
  };
  bool none = true;

 public:
  ~optional_base() {
    if (!none) value.~T();
  }
  optional_base() : null_value(), none(true) {}

  optional_base(optional_base&& other) : null_value(), none(other.none) {
    if (!none) {
      new (&value) T(std::move(other.value));
      other.value.~T();
      other.none = true;
    }
  }
  optional_base(const optional_base&) = delete;

  optional_base& operator=(optional_base&& other) {
    if (!none) value.~T();
    none = other.none;
    if (none) return *this;
    new (&value) T(std::move(other.value));
    other.value.~T();
    other.none = true;
    return *this;
  }

  template <class... Args>
  optional_base(in_place_t, Args&&... args) : value(std::forward<Args>(args)...), none(false) {}
};

template <class T>
class alignas(T) optional_base<T, false, false> {
 protected:
  union {
    uint8_t null_value;
    T value;
  };
  bool none = true;

 public:
  ~optional_base() {
    if (!none) value.~T();
  }
  optional_base() : null_value(), none(true) {}
  optional_base(const optional_base&) = delete;
  optional_base(optional_base&&) = delete;

  template <class... Args>
  optional_base(in_place_t, Args&&... args) : value(std::forward<Args>(args)...), none(false) {}
};

template <class T>
using optional_base_t =
    optional_base<T, std::is_copy_constructible<T>::value, std::is_move_constructible<T>::value>;

template <class T>
class optional : public optional_base_t<T> {
 public:
  template <class... Args>
  optional(in_place_t x, Args&&... args) : optional_base_t<T>(x, std::forward<Args>(args)...) {}

  optional() = default;
  optional(const optional&) = default;
  optional(optional&&) = default;
  ~optional() = default;

  optional& operator=(const optional&) = default;
  optional& operator=(optional&&) = default;

  explicit operator bool() const { return !this->none; }

  T& operator*() { return this->value; }

  const T& operator*() const { return this->value; }

  T* operator->() { return &this->value; }

  const T* operator->() const { return &this->value; }
};

// `some` is used when you want to wrap a known value in an optinal
// and don't want to mess with template arguments.
// Example: some(10) would be of type wcl::optional<int>
template <class T>
inline optional<T> some(T&& x) {
  return optional<T>{in_place_t{}, std::forward<T>(x)};
}

// `make_some` is the more general older brother of `some` which
// demands that you tell it the output type you want to use but allows
// you to construct the value in place using an constructor of that type.
template <class T, class... Args>
inline optional<T> make_some(Args&&... args) {
  return optional<T>{in_place_t{}, std::forward<Args>(args)...};
}

template <class T>
std::ostream& operator<<(std::ostream& os, const optional<T>& value) {
  if (value) {
    os << "{" << *value << "}";
  } else {
    os << "{}";
  }
  return os;
}

}  // namespace wcl
