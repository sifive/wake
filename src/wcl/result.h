/*
 * Copyright 2023 SiFive, Inc.
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
#include <utility>

#include "optional.h"

namespace wcl {

using posix_error_t = int;

class in_place_error_t {};

static constexpr uint64_t max(uint64_t a, uint64_t b) { return (a < b) ? b : a; }

template <class T, class E, bool copy>
class result_base {};

// result demands that the error be copyable
// and demands the the value be moveable but
// the value need not be copyable.
template <class T, class E>
class alignas(max(alignof(T), alignof(E))) result_base<T, E, true> {
 protected:
  union {
    T value;
    E error_;
  };
  bool is_error = true;

 public:
  ~result_base() {
    if (is_error) {
      error_.~E();
    } else {
      value.~T();
    }
  }
  result_base() = delete;
  result_base(const result_base& other) {
    is_error = other.is_error;
    if (is_error) {
      new (&error_) E(other.error_);
    } else {
      new (&value) T(other.value);
    }
  }
  result_base(result_base&& other) {
    is_error = other.is_error;
    // on move we just use the move constructors
    // of each value and we don't call their destructors yet.
    // so if you move a result, it will keeps its errorness
    // but the underlying value may be changed.
    if (is_error) {
      new (&error_) E(std::move(other.error_));
    } else {
      new (&value) T(std::move(other.value));
    }
  }

  result_base& operator=(const result_base& other) {
    if (is_error) {
      error_.~E();
    } else {
      value.~T();
    }

    is_error = other.is_error;
    if (other.is_error) {
      new (&error_) E(other.error_);
    } else {
      new (&value) T(other.value);
    }
  }

  result_base& operator=(result_base&& other) {
    if (is_error) {
      error_.~E();
    } else {
      value.~T();
    }

    is_error = other.is_error;
    if (other.is_error) {
      new (&error_) E(std::move(other.error_));
    } else {
      new (&value) T(std::move(other.value));
    }
  }

  template <class... Args>
  result_base(in_place_t, Args&&... args) : value(std::forward<Args>(args)...), is_error(false) {}

  template <class... Args>
  result_base(in_place_error_t, Args&&... args)
      : error_(std::forward<Args>(args)...), is_error(true) {}
};

// result demands that the error be copyable
// and demands the the value be moveable but
// the value need not be copyable.
template <class T, class E>
class alignas(max(alignof(T), alignof(E))) result_base<T, E, false> {
 protected:
  union {
    T value;
    E error_;
  };
  bool is_error = true;

 public:
  ~result_base() {
    if (is_error) {
      error_.~E();
    } else {
      value.~T();
    }
  }
  result_base() = delete;
  result_base(const result_base& other) = delete;
  result_base(result_base&& other) {
    is_error = other.is_error;
    // on move we just use the move constructors
    // of each value and we don't call their destructors yet.
    // so if you move a result, it will keeps its errorness
    // but the underlying value may be changed.
    if (is_error) {
      new (&error_) E(std::move(other.error_));
    } else {
      new (&value) T(std::move(other.value));
    }
  }

  result_base& operator=(const result_base& other) = delete;

  result_base& operator=(result_base&& other) {
    if (is_error) {
      error_.~E();
    } else {
      value.~T();
    }

    is_error = other.is_error;
    if (other.is_error) {
      new (&error_) E(std::move(other.error_));
    } else {
      new (&value) T(std::move(other.value));
    }
  }

  template <class... Args>
  result_base(in_place_t, Args&&... args) : value(std::forward<Args>(args)...), is_error(false) {}

  template <class... Args>
  result_base(in_place_error_t, Args&&... args)
      : error_(std::forward<Args>(args)...), is_error(true) {}
};

template <class T, class E>
using result_base_t =
    result_base<T, E, std::is_copy_constructible<T>::value && std::is_copy_constructible<E>::value>;

template <class T, class E>
class result : public result_base_t<T, E> {
 public:
  template <class... Args>
  result(in_place_t x, Args&&... args) : result_base_t<T, E>(x, std::forward<Args>(args)...) {}
  template <class... Args>
  result(in_place_error_t x, Args&&... args)
      : result_base_t<T, E>(x, std::forward<Args>(args)...) {}

  result() = default;
  result(const result&) = default;
  result(result&&) = default;
  ~result() = default;

  result& operator=(const result&) = default;
  result& operator=(result&&) = default;

  explicit operator bool() const { return !this->is_error; }

  T& operator*() { return this->value; }

  const T& operator*() const { return this->value; }

  T* operator->() { return &this->value; }

  const T* operator->() const { return &this->value; }

  E& error() { return this->error_; }

  const E& error() const { return this->error_; }
};

template <class T, class E, class... Args>
result<T, E> make_result(Args&&... args) {
  return result<T, E>{in_place_t{}, std::forward<Args>(args)...};
}

template <class T, class E, class... Args>
result<T, E> make_error(Args&&... args) {
  return result<T, E>{in_place_error_t{}, std::forward<Args>(args)...};
}

template <class T>
result<T, posix_error_t> make_errno() {
  return result<T, posix_error_t>{in_place_error_t{}, errno};
}

}  // namespace wcl
