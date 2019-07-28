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

#include "tuple.h"
#include "location.h"

void Promise::fulfill(Runtime &runtime, HeapObject *obj) {
  if (value) {
#ifdef DEBUG_GC
    assert(value->is_work());
#endif
    Continuation *c = static_cast<Continuation*>(value.get());
    while (c->next) {
      c->value = obj;
      c = static_cast<Continuation*>(c->next.get());
    }
    c->value = obj;
    c->next = runtime.stack;
    runtime.stack = value;
  }
#ifdef DEBUG_GC
  assert(obj);
  assert(!obj->is_work());
#endif
  value = obj;
}

struct FulFiller final : public GCObject<FulFiller, Continuation> {
  HeapPointer<Tuple> tuple;
  size_t i;

  FulFiller(Tuple *tuple_, size_t i_) : tuple(tuple_), i(i_) { }

  template <typename T, T (HeapPointerBase::*memberfn)(T x)>
  T recurse(T arg) {
    arg = Continuation::recurse<T, memberfn>(arg);
    arg = (tuple.*memberfn)(arg);
    return arg;
  }

  void execute(Runtime &runtime) {
    tuple->at(i)->fulfill(runtime, value.get());
  }
};

struct BigTuple final : public GCObject<BigTuple, Tuple> {
  typedef GCObject<BigTuple, Tuple> Parent;

  size_t tsize;

  size_t size() const override;
  Promise *at(size_t i) override;
  const Promise *at(size_t i) const override;

  BigTuple(Meta *meta, size_t size_);
  BigTuple(const BigTuple &b);

  template <typename T, T (HeapPointerBase::*memberfn)(T x)>
  T recurse(T arg);
  PadObject *next();

  Continuation *claim_fulfiller(Runtime &r, size_t i) override;
};

size_t BigTuple::size() const {
  return tsize;
}

Promise *BigTuple::at(size_t i) {
  return static_cast<Promise*>(data()) + i;
}

const Promise *BigTuple::at(size_t i) const {
  return static_cast<const Promise*>(data()) + i;
}

BigTuple::BigTuple(Meta *meta, size_t size_) : GCObject<BigTuple, Tuple>(meta), tsize(size_) {
  for (size_t i = 0; i < size_; ++i)
    new (at(i)) Promise();
}

BigTuple::BigTuple(const BigTuple &b) : GCObject<BigTuple, Tuple>(b), tsize(b.tsize) {
  for (size_t i = 0; i < tsize; ++i)
    new (at(i)) Promise(*b.at(i));
}

PadObject *BigTuple::next() {
  return Parent::next() + tsize * (sizeof(Promise)/sizeof(PadObject));
}

template <typename T, T (HeapPointerBase::*memberfn)(T x)>
T BigTuple::recurse(T arg) {
  arg = Parent::recurse<T, memberfn>(arg);
  for (size_t i = 0; i < tsize; ++i)
    arg = at(i)->recurse<T, memberfn>(arg);
  return arg;
}

Continuation *BigTuple::claim_fulfiller(Runtime &r, size_t i) {
  return new (r.heap.claim(fulfiller_pads)) FulFiller(this, i);
}

template <size_t tsize>
struct SmallTuple final : public GCObject<SmallTuple<tsize>, Tuple> {
  typedef GCObject<SmallTuple<tsize>, Tuple> Parent;

  size_t size() const override;
  Promise *at(size_t i) override;
  const Promise *at(size_t i) const override;

  SmallTuple(Meta *meta);
  SmallTuple(const SmallTuple &b);

  template <typename T, T (HeapPointerBase::*memberfn)(T x)>
  T recurse(T arg);
  PadObject *next();

  Continuation *claim_fulfiller(Runtime &r, size_t i) override;
};

template <size_t tsize>
size_t SmallTuple<tsize>::size() const {
  return tsize;
}

template <size_t tsize>
Promise *SmallTuple<tsize>::at(size_t i) {
  return static_cast<Promise*>(Parent::data()) + i;
}

template <size_t tsize>
const Promise *SmallTuple<tsize>::at(size_t i) const {
  return static_cast<const Promise*>(Parent::data()) + i;
}

template <size_t tsize>
SmallTuple<tsize>::SmallTuple(Meta *meta) : GCObject<SmallTuple<tsize>, Tuple>(meta) {
  for (size_t i = 0; i < tsize; ++i)
    new (at(i)) Promise();
}

template <size_t tsize>
SmallTuple<tsize>::SmallTuple(const SmallTuple &b) : GCObject<SmallTuple<tsize>, Tuple>(b) {
  for (size_t i = 0; i < tsize; ++i)
    new (at(i)) Promise(*b.at(i));
}

template <size_t tsize>
PadObject *SmallTuple<tsize>::next() {
  return Parent::next() + tsize * (sizeof(Promise)/sizeof(PadObject));
}

template <size_t tsize>
template <typename T, T (HeapPointerBase::*memberfn)(T x)>
T SmallTuple<tsize>::recurse(T arg) {
  arg = Parent::template recurse<T, memberfn>(arg);
  for (size_t i = 0; i < tsize; ++i)
    arg = at(i)->template recurse<T, memberfn>(arg);
  return arg;
}

template <size_t tsize>
Continuation *SmallTuple<tsize>::claim_fulfiller(Runtime &r, size_t i) {
  return new (r.heap.claim(Tuple::fulfiller_pads)) FulFiller(this, i);
}

const size_t Tuple::fulfiller_pads = sizeof(FulFiller)/sizeof(PadObject);

size_t Tuple::reserve(size_t size) {
  bool big = size > 4;
  if (big) {
    return sizeof(BigTuple)/sizeof(PadObject) + size * (sizeof(Promise)/sizeof(PadObject));
  } else {
    return sizeof(SmallTuple<0>)/sizeof(PadObject) + size * (sizeof(Promise)/sizeof(PadObject));
  }
}

Tuple *Tuple::claim(Heap &h, Meta *meta, size_t size) {
  bool big = size > 4;
  if (big) {
    return new (h.claim(reserve(size))) BigTuple(meta, size);
  } else {
    PadObject *dest = h.claim(reserve(size));
    switch (size) {
      case 0:  return new (dest) SmallTuple<0>(meta);
      case 1:  return new (dest) SmallTuple<1>(meta);
      case 2:  return new (dest) SmallTuple<2>(meta);
      case 3:  return new (dest) SmallTuple<3>(meta);
      default: return new (dest) SmallTuple<4>(meta);
    }
  }
}

Tuple *Tuple::alloc(Heap &h, Meta *meta, size_t size) {
  h.reserve(reserve(size));
  return claim(h, meta, size);
}

std::vector<Location> Tuple::stack_trace() const {
  std::vector<Location> out;
/* !!! stack tracing missing
  for (const Binding *i = this; i; i = i->invoker.get())
    if (i->expr->type != &DefBinding::type)
      out.emplace_back(i->expr->location);
*/
  return out;
}
