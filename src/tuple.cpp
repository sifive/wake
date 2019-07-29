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

const size_t Tuple::fulfiller_pads = sizeof(FulFiller)/sizeof(PadObject);

Continuation *Tuple::claim_fulfiller(Runtime &r, size_t i) {
  return new (r.heap.claim(Tuple::fulfiller_pads)) FulFiller(this, i);
}

template <typename T, typename B>
struct TupleObject : public GCObject<T, B> {
  Promise *at(size_t i) final override;
  const Promise *at(size_t i) const final override;

  template <typename ... ARGS>
  TupleObject(size_t size, ARGS&&... args);
  TupleObject(const TupleObject &other);

  template <typename R, R (HeapPointerBase::*memberfn)(R x)>
  R recurse(R arg);
  PadObject *next();
};

template <typename T, typename B>
Promise *TupleObject<T,B>::at(size_t i) {
  return static_cast<Promise*>(GCObject<T,B>::data()) + i;
}

template <typename T, typename B>
const Promise *TupleObject<T,B>::at(size_t i) const {
  return static_cast<const Promise*>(GCObject<T,B>::data()) + i;
}

template <typename T, typename B>
template <typename ... ARGS>
TupleObject<T,B>::TupleObject(size_t size, ARGS&&... args) : GCObject<T,B>(std::forward<ARGS>(args) ... ) {
  for (size_t i = 0; i < size; ++i)
    new (at(i)) Promise();
}

template <typename T, typename B>
TupleObject<T,B>::TupleObject(const TupleObject &b) : GCObject<T, B>(b) {
  for (size_t i = 0; i < b.size(); ++i)
    new (at(i)) Promise(*b.at(i));
}

template <typename T, typename B>
PadObject *TupleObject<T,B>::next() {
  return GCObject<T,B>::next() + GCObject<T,B>::self()->size() * (sizeof(Promise)/sizeof(PadObject));
}

template <typename T, typename B>
template <typename R, R (HeapPointerBase::*memberfn)(R x)>
R TupleObject<T,B>::recurse(R arg) {
  arg = GCObject<T,B>::template recurse<R, memberfn>(arg);
  for (size_t i = 0; i < GCObject<T,B>::self()->size(); ++i)
    arg = at(i)->template recurse<R, memberfn>(arg);
  return arg;
}

struct BigRecord final : public TupleObject<BigRecord, Record> {
  size_t tsize;

  BigRecord(Constructor *cons, size_t tsize_)
   : TupleObject<BigRecord, Record>(tsize_, cons), tsize(tsize_) { }

  size_t size() const override;
};

size_t BigRecord::size() const {
  return tsize;
}

template <size_t tsize>
struct SmallRecord final : public TupleObject<SmallRecord<tsize>, Record> {
  SmallRecord(Constructor *cons)
   : TupleObject<SmallRecord<tsize>, Record>(tsize, cons) { }

  size_t size() const override;
};

template <size_t tsize>
size_t SmallRecord<tsize>::size() const {
  return tsize;
}

size_t Record::reserve(size_t size) {
  bool big = size > 4;
  if (big) {
    return sizeof(BigRecord)/sizeof(PadObject) + size * (sizeof(Promise)/sizeof(PadObject));
  } else {
    return sizeof(SmallRecord<0>)/sizeof(PadObject) + size * (sizeof(Promise)/sizeof(PadObject));
  }
}

Record *Record::claim(Heap &h, Constructor *cons, size_t size) {
  bool big = size > 4;
  if (big) {
    return new (h.claim(reserve(size))) BigRecord(cons, size);
  } else {
    PadObject *dest = h.claim(reserve(size));
    switch (size) {
      case 0:  return new (dest) SmallRecord<0>(cons);
      case 1:  return new (dest) SmallRecord<1>(cons);
      case 2:  return new (dest) SmallRecord<2>(cons);
      case 3:  return new (dest) SmallRecord<3>(cons);
      default: return new (dest) SmallRecord<4>(cons);
    }
  }
}

Record *Record::alloc(Heap &h, Constructor *cons, size_t size) {
  h.reserve(reserve(size));
  return claim(h, cons, size);
}

struct BigScope final : public TupleObject<BigScope, Scope> {
  size_t tsize;

  BigScope(Scope *next, size_t tsize_)
   : TupleObject<BigScope, Scope>(tsize_, next), tsize(tsize_) { }

  size_t size() const override;
};

size_t BigScope::size() const {
  return tsize;
}

template <size_t tsize>
struct SmallScope final : public TupleObject<SmallScope<tsize>, Scope> {
  SmallScope(Scope *next)
   : TupleObject<SmallScope<tsize>, Scope>(tsize, next) { }

  size_t size() const override;
};

template <size_t tsize>
size_t SmallScope<tsize>::size() const {
  return tsize;
}

bool Scope::debug = false;

std::vector<Location> Scope::stack_trace() const {
  std::vector<Location> out;
/* !!! stack tracing missing
  for (const Binding *i = this; i; i = i->invoker.get())
    if (i->expr->type != &DefBinding::type)
      out.emplace_back(i->expr->location);
*/
  return out;
}

size_t Scope::reserve(size_t size) {
  bool big = size > 4;
  if (big) {
    return sizeof(BigScope)/sizeof(PadObject) + size * (sizeof(Promise)/sizeof(PadObject));
  } else {
    return sizeof(SmallScope<0>)/sizeof(PadObject) + size * (sizeof(Promise)/sizeof(PadObject));
  }
}

Scope *Scope::claim(Heap &h, Scope *next, size_t size) {
  bool big = size > 4;
  if (big) {
    return new (h.claim(reserve(size))) BigScope(next, size);
  } else {
    PadObject *dest = h.claim(reserve(size));
    switch (size) {
      case 0:  return new (dest) SmallScope<0>(next);
      case 1:  return new (dest) SmallScope<1>(next);
      case 2:  return new (dest) SmallScope<2>(next);
      case 3:  return new (dest) SmallScope<3>(next);
      default: return new (dest) SmallScope<4>(next);
    }
  }
}

Scope *Scope::alloc(Heap &h, Scope *next, size_t size) {
  h.reserve(reserve(size));
  return claim(h, next, size);
}
