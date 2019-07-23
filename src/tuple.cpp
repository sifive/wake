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

void Promise::fulfill(Runtime &runtime, HeapObject *obj) {
  if (value) {
    Continuation *c = static_cast<Continuation*>(value.get());
    while (c->next) {
      c->value = obj;
      c = static_cast<Continuation*>(c->next.get());
    }
    c->value = obj;
    c->next = runtime.stack;
    runtime.stack = value;
  }
  value = obj;
}

struct FulFiller final : public GCObject<FulFiller, Continuation> {
  HeapPointer<Tuple> tuple;
  size_t i;

  FulFiller(Tuple *tuple_, size_t i_) : tuple(tuple_), i(i_) { }

  PadObject *recurse(PadObject *free) {
    free = Continuation::recurse(free);
    free = tuple.moveto(free);
    return free;
  }

  void execute(Runtime &runtime) {
    (*tuple.get())[i].fulfill(runtime, value.get());
  }
};

struct BigTuple final : public GCObject<BigTuple, Tuple> {
  typedef GCObject<BigTuple, Tuple> Parent;

  size_t tsize;

  Promise *at(size_t i);
  const Promise *at(size_t i) const;

  BigTuple(void *meta, size_t size_);
  BigTuple(const BigTuple &b);

  PadObject *next();
  PadObject *recurse(PadObject *free);

  size_t size() const override;
  Promise & operator [] (size_t i) override;
  const Promise & operator [] (size_t i) const override;

  Continuation *claim_fulfiller(Runtime &r, size_t i) override;
};

Promise *BigTuple::at(size_t i) {
  return static_cast<Promise*>(data()) + i;
}

const Promise *BigTuple::at(size_t i) const {
  return static_cast<const Promise*>(data()) + i;
}

BigTuple::BigTuple(void *meta, size_t size_) : GCObject<BigTuple, Tuple>(meta), tsize(size_) {
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

PadObject *BigTuple::recurse(PadObject *free) {
  free = Parent::recurse(free);
  for (size_t i = 0; i < tsize; ++i)
    free = (*this)[i].moveto(free);
  return free;
}

size_t BigTuple::size() const {
  return tsize;
}

Promise & BigTuple::operator [] (size_t i) {
  return *at(i);
}

const Promise & BigTuple::operator [] (size_t i) const {
  return *at(i);
}

Continuation *BigTuple::claim_fulfiller(Runtime &r, size_t i) {
  return new (r.heap.claim(fulfiller_pads)) FulFiller(this, i);
}

template <size_t tsize>
struct SmallTuple final : public GCObject<SmallTuple<tsize>, Tuple> {
  typedef GCObject<SmallTuple<tsize>, Tuple> Parent;

  Promise *at(size_t i);
  const Promise *at(size_t i) const;

  SmallTuple(void *meta);
  SmallTuple(const SmallTuple &b);

  PadObject *next();
  PadObject *recurse(PadObject *free);

  size_t size() const override;
  Promise & operator [] (size_t i) override;
  const Promise & operator [] (size_t i) const override;

  Continuation *claim_fulfiller(Runtime &r, size_t i) override;
};

template <size_t tsize>
Promise *SmallTuple<tsize>::at(size_t i) {
  return static_cast<Promise*>(Parent::data()) + i;
}

template <size_t tsize>
const Promise *SmallTuple<tsize>::at(size_t i) const {
  return static_cast<const Promise*>(Parent::data()) + i;
}

template <size_t tsize>
SmallTuple<tsize>::SmallTuple(void *meta) : GCObject<SmallTuple<tsize>, Tuple>(meta) {
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
PadObject *SmallTuple<tsize>::recurse(PadObject *free) {
  free = Parent::recurse(free);
  for (size_t i = 0; i < tsize; ++i)
    free = (*this)[i].moveto(free);
  return free;
}

template <size_t tsize>
size_t SmallTuple<tsize>::size() const {
  return tsize;
}

template <size_t tsize>
Promise & SmallTuple<tsize>::operator [] (size_t i) {
  return *at(i);
}

template <size_t tsize>
const Promise & SmallTuple<tsize>::operator [] (size_t i) const {
  return *at(i);
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

Tuple *Tuple::claim(Heap &h, void *meta, size_t size) {
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

Tuple *Tuple::alloc(Heap &h, void *meta, size_t size) {
  h.reserve(reserve(size));
  return claim(h, meta, size);
}
