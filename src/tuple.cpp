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
#include "expr.h"

void Promise::awaken(Runtime &runtime, HeapObject *obj) {
  if (value->category() == DEFERRAL) return;
  Continuation *c = static_cast<Continuation*>(value.get());
  while (c->next) {
    c->value = obj;
    c = static_cast<Continuation*>(c->next.get());
  }
  c->value = obj;
  c->next = runtime.stack;
  runtime.stack = value;
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

  void execute(Runtime &runtime) override {
    tuple->at(i)->fulfill(runtime, value.get());
  }

  void demand(Runtime &runtime, Deferral *def) override {
    Promise *p = tuple->at(i);
    if (p->fresh()) {
      p->defer(def);
    } else {
      def->demand(runtime);
    }
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
  const char *type() const override;

  template <typename ... ARGS>
  TupleObject(size_t size, ARGS&&... args);
  TupleObject(const TupleObject &other);

  template <typename R, R (HeapPointerBase::*memberfn)(R x)>
  R recurse(R arg);
  PadObject *objend();
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
const char *TupleObject<T,B>::type() const {
  return B::type();
}

template <typename T, typename B>
template <typename ... ARGS>
TupleObject<T,B>::TupleObject(size_t size, ARGS&&... args) : GCObject<T,B>(std::forward<ARGS>(args) ... ) {
  for (size_t i = 0; i < size; ++i)
    new (at(i)) Promise();
}

template <typename T, typename B>
TupleObject<T,B>::TupleObject(const TupleObject &b) : GCObject<T, B>(b) {
  for (size_t i = 0; i < b.GCObject<T,B>::self()->size(); ++i)
    new (at(i)) Promise(*b.at(i));
}

template <typename T, typename B>
PadObject *TupleObject<T,B>::objend() {
  return GCObject<T,B>::objend() + GCObject<T,B>::self()->size() * (sizeof(Promise)/sizeof(PadObject));
}

template <typename T, typename B>
template <typename R, R (HeapPointerBase::*memberfn)(R x)>
R TupleObject<T,B>::recurse(R arg) {
  arg = GCObject<T,B>::template recurse<R, memberfn>(arg);
  for (size_t i = 0; i < GCObject<T,B>::self()->size(); ++i)
    arg = at(i)->template recurse<R, memberfn>(arg);
  return arg;
}

const char *Record::type() const {
  return cons->ast.name.c_str();
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

struct alignas(PadObject) ScopeStack {
  HeapPointer<Scope> parent;
  Expr *expr;

  ScopeStack(Scope *parent_, Expr* expr_) : parent(parent_), expr(expr_) { }
};

bool Scope::debug = false;

const char *Scope::type() const {
  return "StackTree";
}

void Scope::set_expr(Expr *expr) {
  if (debug) stack()->expr = expr;
}

std::vector<Location> Scope::stack_trace() const {
  std::vector<Location> out;
  if (debug) {
    const ScopeStack *s;
    for (const Scope *i = this; i; i = s->parent.get()) {
      s = i->stack();
      if (s->expr->type != &DefBinding::type)
        out.emplace_back(s->expr->location);
    }
  }
  return out;
}

template <typename T>
struct ScopeObject : public TupleObject<T, Scope> {
  ScopeObject(size_t size, Scope *next, Scope *parent, Expr *expr);
  ScopeObject(const ScopeObject &other);

  template <typename R, R (HeapPointerBase::*memberfn)(R x)>
  R recurse(R arg);
  PadObject *objend();

  const ScopeStack *stack() const final override;
  ScopeStack *stack() final override;
};

template <typename T>
const ScopeStack *ScopeObject<T>::stack() const {
  return reinterpret_cast<const ScopeStack*>(TupleObject<T,Scope>::at(GCObject<T,Scope>::self()->size()));
}

template <typename T>
ScopeStack *ScopeObject<T>::stack() {
  return reinterpret_cast<ScopeStack*>(TupleObject<T,Scope>::at(GCObject<T,Scope>::self()->size()));
}

template <typename T>
PadObject *ScopeObject<T>::objend() {
  PadObject *end = TupleObject<T, Scope>::objend();
  if (Scope::debug) end += (sizeof(ScopeStack)/sizeof(PadObject));
  return end;
}

template <typename T>
ScopeObject<T>::ScopeObject(size_t size, Scope *next, Scope *parent, Expr *expr)
 : TupleObject<T, Scope>(size, next) {
  if (Scope::debug) {
    new (TupleObject<T, Scope>::at(size)) ScopeStack(parent, expr);
  }
}

template <typename T>
ScopeObject<T>::ScopeObject(const ScopeObject &other)
 : TupleObject<T, Scope>(other) {
  if (Scope::debug) {
    new (TupleObject<T, Scope>::at(other.GCObject<T,Scope>::self()->size())) ScopeStack(*other.stack());
  }
}

template <typename T>
template <typename R, R (HeapPointerBase::*memberfn)(R x)>
R ScopeObject<T>::recurse(R arg) {
  arg = TupleObject<T, Scope>::template recurse<R, memberfn>(arg);
  if (Scope::debug && typeid(memberfn) != typeid(&HeapPointerBase::explore)) arg = (stack()->parent.*memberfn)(arg);
  return arg;
}

struct BigScope final : public ScopeObject<BigScope> {
  size_t tsize;

  BigScope(size_t tsize_, Scope *next, Scope *parent, Expr *expr)
   : ScopeObject<BigScope>(tsize_, next, parent, expr), tsize(tsize_) { }

  size_t size() const override;
};

size_t BigScope::size() const {
  return tsize;
}

template <size_t tsize>
struct SmallScope final : public ScopeObject<SmallScope<tsize> > {
  SmallScope(Scope *next, Scope *parent, Expr *expr)
   : ScopeObject<SmallScope<tsize> >(tsize, next, parent, expr) { }

  size_t size() const override;
};

template <size_t tsize>
size_t SmallScope<tsize>::size() const {
  return tsize;
}

size_t Scope::reserve(size_t size) {
  bool big = size > 4;
  size_t add = size * (sizeof(Promise)/sizeof(PadObject)) + (debug?sizeof(ScopeStack)/sizeof(PadObject):0);
  if (big) {
    return sizeof(BigScope)/sizeof(PadObject) + add;
  } else {
    return sizeof(SmallScope<0>)/sizeof(PadObject) + add;
  }
}

Scope *Scope::claim(Heap &h, size_t size, Scope *next, Scope *parent, Expr *expr) {
  bool big = size > 4;
  if (big) {
    return new (h.claim(reserve(size))) BigScope(size, next, parent, expr);
  } else {
    PadObject *dest = h.claim(reserve(size));
    switch (size) {
      case 0:  return new (dest) SmallScope<0>(next, parent, expr);
      case 1:  return new (dest) SmallScope<1>(next, parent, expr);
      case 2:  return new (dest) SmallScope<2>(next, parent, expr);
      case 3:  return new (dest) SmallScope<3>(next, parent, expr);
      default: return new (dest) SmallScope<4>(next, parent, expr);
    }
  }
}

Scope *Scope::alloc(Heap &h, size_t size, Scope *next, Scope *parent, Expr *expr) {
  h.reserve(reserve(size));
  return claim(h, size, next, parent, expr);
}
