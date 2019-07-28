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
#include <vector>
struct Location;
struct Meta;

struct alignas(PadObject) Promise {
  explicit operator bool() const {
    HeapObject *obj = value.get();
    return obj && !obj->is_work();
  }

  void await(Runtime &runtime, Continuation *c) const {
    if (*this) {
      c->resume(runtime, value.get());
    } else {
      c->next = static_cast<Continuation*>(value.get());
      value = c;
    }
  }

  // Use only if the value is known to already be available 
  template <typename T>
  T *coerce() { return static_cast<T*>(value.get()); }
  template <typename T>
  const T *coerce() const { return static_cast<const T*>(value.get()); }

  // Call once only!
  void fulfill(Runtime &runtime, HeapObject *obj);
  // Call only if the containing tuple was just constructed (no Continuations)
  void instant_fulfill(HeapObject *obj) {
#ifdef DEBUG_GC
     assert(!obj || !value);
     assert(!obj || !obj->is_work());
#endif
     value = obj;
  }

  template <typename T, T (HeapPointerBase::*memberfn)(T x)>
  T recurse(T arg) { return (value.*memberfn)(arg); }

private:
  mutable HeapPointer<HeapObject> value;
friend struct Tuple;
};

struct Tuple : public HeapObject {
  // Either an Expr or a Constructor
  Meta *meta;

  Tuple(Meta *meta_) : meta(meta_) { }

  virtual size_t size() const = 0;
  virtual Promise *at(size_t i) = 0;
  const virtual Promise *at(size_t i) const = 0;
  void format(std::ostream &os, FormatState &state) const override;
  Hash hash() const override;

  bool empty() const { return size() == 0; }

  static const size_t fulfiller_pads;
  virtual Continuation *claim_fulfiller(Runtime &r, size_t i) = 0;

  void claim_instant_fulfiller(Runtime &r, size_t i, Promise *p) {
    if (*p) {
      at(i)->instant_fulfill(p->coerce<HeapObject>());
    } else {
      Continuation *cont = claim_fulfiller(r, i);
      cont->next = p->value;
      p->value = cont;
    }
  }

  static size_t reserve(size_t size);
  static Tuple *claim(Heap &h, Meta *meta, size_t size); // requires prior h.reserve
  static Tuple *alloc(Heap &h, Meta *meta, size_t size);

  std::vector<Location> stack_trace() const;
};

#endif
