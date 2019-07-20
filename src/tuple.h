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

#ifndef TUPLE_H
#define TUPLE_H

#include "runtime.h"

struct alignas(PadObject) Promise {
  explicit operator bool() const {
    HeapObject *obj = value.get();
    return obj && typeid(*obj) != typeid(Continuation);
  }

  void await(Runtime &runtime, Continuation *c) const {
    if (*this) {
      c->resume(runtime, value.get());
    } else {
      c->next = static_cast<Work*>(value.get());
      value = c;
    }
  }

  // Use only if the value is known to always be available
  template <typename T>
  T *coerce() { return static_cast<T*>(value.get()); }

  // Call once only!
  void fulfill(Runtime &runtime, HeapObject *obj);

  PadObject *moveto(PadObject *free) { return value.moveto(free); }

private:
  mutable HeapPointer<HeapObject> value;
};

struct Tuple : public HeapObject {
  // Either an Expr or a Constructor
  void *meta;

  Tuple(void *meta_) : meta(meta_) { }

  virtual size_t size() const = 0;
  virtual Promise & operator [] (size_t i) = 0;
  const virtual Promise & operator [] (size_t i) const = 0;

  static const size_t fulfiller_pads;
  virtual Continuation *claim_fulfiller(Runtime &r, size_t i) = 0;

  static size_t reserve(size_t size);
  static Tuple *claim(Heap &h, void *meta, size_t size); // requires prior h.reserve
  static Tuple *alloc(Heap &h, void *meta, size_t size);
};

#endif
