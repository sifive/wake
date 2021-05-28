/*
 * Copyright 2019 SiFive, Inc.
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

#ifndef OPTION_H
#define OPTION_H

template <typename T>
struct optional {
  ~optional() { reset(); }

  optional(T *x = nullptr) : ptr(x) { }
  optional(optional<T> &&o) : ptr(o.ptr) { o.ptr = nullptr; }
  optional(const optional<T> &o) : ptr(o.ptr ? new T(*o.ptr) : nullptr) { }

  void reset() { delete ptr; ptr = nullptr; }
  T *release() { T *out = ptr; ptr = nullptr; return out; }

  T& operator *  () const { return *ptr; }
  T* operator -> () const { return ptr; }
  operator bool  () const { return ptr; }

  optional<T>& operator = (const optional<T> &o) { optional<T> tmp(o);            swap(*this, tmp); return *this; }
  optional<T>& operator = (optional<T> &&o)      { optional<T> tmp(std::move(o)); swap(*this, tmp); return *this; }

  friend void swap(optional<T> &a, optional<T> &b) {
    std::swap(a.ptr, b.ptr);
  }

private:
  T* ptr;
};

#endif
