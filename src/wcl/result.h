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

#include <algorithm>
#include <cstdint>

namespace wcl {

template <class Value, class Error>
class alignas(std::max(alignof(Value), alignof(Error))) result {
 private:
  uint8_t data[std::max(sizeof(Value), sizeof(Error))];
  bool is_error = false;

  // we need a default constructor but only because
  // we need static functions to act as our primary
  // constructors and those need to be able to construct.
  // something to a default state. From a user's perspective
  // however this function is *very* dangerous.
  result() = default;

  // We commonly need to revert to an unconstructed state
  // while implementing these functions but it would
  // be dangerous for the user to do this. So we make it
  // private. This is really just a convient name for the
  // destructor.
  void clear() {
    if (is_error) {
      reinterpret_cast<Error*>(data)->~Error();
    } else {
      reinterpret_cast<Value*>(data)->~Value();
    }
  }

 public:
  result(const result& other) : is_error(other.is_error) {
    if (other.is_error) {
      Error* other_ptr = reinterpret_cast<const Error*>(other.data);
      new (reinterpret_cast<Error*>(data)) Error(*other_ptr);
    } else {
      Value* other_ptr = reinterpret_cast<const Value*>(other.data);
      new (reinterpret_cast<Value*>(data)) Value(*other_ptr);
    }
  }

  result(result&& other) : is_error(other.is_error) {
    if (other.is_error) {
      Error* other_ptr = reinterpret_cast<const Error*>(other.data);
      new (reinterpret_cast<Error*>(data)) Error(std::move(*other_ptr));
    } else {
      Value* other_ptr = reinterpret_cast<const Value*>(other.data);
      new (reinterpret_cast<Value*>(data)) Value(std::move(*other_ptr));
    }
  }

  template <class... Args>
  static result make_result(Args&&... args) {
    result out;
    out.is_error = false;
    new (reinterpret_cast<Value*>(out.data)) Value(std::forward<Args>(args)...);
    return out;
  }

  template <class... Args>
  static result make_error(Args&&... args) {
    result out;
    out.is_error = true;
    new (reinterpret_cast<Error*>(out.data)) Error(std::forward<Args>(args)...);
    return out;
  }

  ~result() { clear(); }

  result& operator=(const result& other) {
    clear();
    new (this) result(other);
    return *this;
  }

  result& operator=(result&& other) {
    clear();
    new (this) result(std::move(other));
    return *this;
  }

  explicit operator bool() const { return !is_error; }

  Value& operator*() {
    assert(!is_error);
    return *reinterpret_cast<Value*>(data);
  }

  const Value& operator*() const {
    assert(!is_error);
    return *reinterpret_cast<const Value*>(data);
  }

  Value* operator->() {
    assert(!is_error);
    return reinterpret_cast<const Value*>(data);
  }

  const Value* operator->() const {
    assert(!is_error);
    return reinterpret_cast<const Value*>(data);
  }

  Error& error() {
    assert(is_error);
    return *reinterpret_cast<Error*>(data);
  }

  const Error& error() const {
    assert(is_error);
    return *reinterpret_cast<const Error*>(data);
  }
};

template <class Value, class Error, class... Args>
static inline result<Value, Error> make_result(Args&&... args) {
  return result<Value, Error>::make_result(std::forward<Args>(args)...);
}

template <class Value, class Error, class... Args>
static inline result<Value, Error> make_error(Args&&... args) {
  return result<Value, Error>::make_error(std::forward<Args>(args)...);
}

}  // namespace wcl
