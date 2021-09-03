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

// Open Group Base Specifications Issue 7
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <sstream>
#include <unordered_map>

#include "optimizer/ssa.h"
#include "tuple.h"

void Promise::awaken(Runtime &runtime, HeapObject *obj) {
#ifdef DEBUG_GC
  assert(category() == WORK);
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
  RFun *fun;

  ScopeStack(Scope *parent_, RFun* fun_) : parent(parent_), fun(fun_) { }
};

bool Scope::debug = false;

const char *Scope::type() const {
  return "ScopeTree";
}

void Scope::set_fun(RFun *fun) {
  if (debug) stack()->fun = fun;
}

struct Compressor {
  uint32_t value;
  uint16_t depth;
  uint8_t erased; // 1 or 0
  uint8_t pad; // 0

  Compressor(uint32_t value_, uint16_t depth_ = 0, bool erased_ = false)
   : value(value_), depth(depth_), erased(erased_), pad(0) { }

  bool operator == (Compressor o) const {
    return value == o.value && depth == o.depth && erased == o.erased && pad == o.pad;
  }
};

static std::vector<std::string> scompress(std::vector<std::string> &&raw, bool indent_compress) {
  std::unordered_map<std::string, uint32_t> map;
  std::vector<Compressor> run;
  run.reserve(raw.size());
  for (unsigned i = 0; i < raw.size(); ++i)
    run.emplace_back(map.insert(std::make_pair(raw[i], i)).first->second);
  map.clear();
  // Magic O(nlogn) stack compression algorithm
  for (unsigned stride = 1; stride <= run.size()/2; ++stride) {
    for (unsigned i = stride; i < run.size(); i += stride) {
      if (run[i-stride] == run[i]) {
        unsigned s = i, f = i;
        while (s > stride && run[s-stride-1] == run[s-1]) --s;
        while (f+1 < run.size() && run[f-stride+1] == run[f+1]) ++f;
        while (run[s-stride].erased && s < f) ++s;
        if (unsigned reps = (f+1-s)/stride) {
          unsigned e = s + reps*stride;
          unsigned prng = 0;
          for (unsigned j = s-stride; j < s; ++j) {
            ++run[j].depth;
            prng = prng * 0x3ba78125 ^ static_cast<unsigned char>(run[j].value);
          }
          for (unsigned j = s; j < e; ++j) {
            prng *= 0x1b642835;
            run[j].value = prng >> 24;
            run[j].depth++;
            run[j].erased = true;
          }
          --run[e-1].depth;
        }
        i = f;
      }
    }
  }
  std::string pad;
  std::vector<uint16_t> depths;
  std::vector<std::string> out;
  unsigned stride = 0;
  for (unsigned i = 0; i < run.size(); ++i) {
    Compressor c = run[i];
    if (c.erased) {
      if (!stride) stride = i-depths.back();
      if (c.depth < depths.size()) {
        if (indent_compress) {
          unsigned repeat = ((i+1)-depths.back()) / stride;
          pad.resize(depths.size()*2, ' ');
          out.push_back(pad + "x " + std::to_string(repeat));
        }
        stride = 0;
        depths.pop_back();
      }
    } else {
      if (indent_compress) pad.resize(c.depth*2, ' ');
      while (c.depth > depths.size()) { depths.push_back(i); }
      out.push_back(pad + raw[c.value]);
    }
  }
  return out;
}

std::vector<std::string> Scope::stack_trace(bool indent_compress) const {
  std::vector<std::string> out;
  if (debug) {
    const ScopeStack *s;
    std::stringstream ss;
    for (const Scope *i = this; i; i = s->parent.get()) {
      s = i->stack();
      ss << s->fun->label << ": " << s->fun->fragment.location();
      auto x = ss.str();
      ss.str(std::string());
      if (out.empty() || out.back() != x)
        out.emplace_back(std::move(x));
    }
  }
  return scompress(std::move(out), indent_compress);
}

template <typename T>
struct ScopeObject : public TupleObject<T, Scope> {
  ScopeObject(size_t size, Scope *next, Scope *parent, RFun *fun);
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
ScopeObject<T>::ScopeObject(size_t size, Scope *next, Scope *parent, RFun *fun)
 : TupleObject<T, Scope>(size, next) {
  if (Scope::debug) {
    new (TupleObject<T, Scope>::at(size)) ScopeStack(parent, fun);
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

  BigScope(size_t tsize_, Scope *next, Scope *parent, RFun *fun)
   : ScopeObject<BigScope>(tsize_, next, parent, fun), tsize(tsize_) { }

  size_t size() const override;
};

size_t BigScope::size() const {
  return tsize;
}

template <size_t tsize>
struct SmallScope final : public ScopeObject<SmallScope<tsize> > {
  SmallScope(Scope *next, Scope *parent, RFun *fun)
   : ScopeObject<SmallScope<tsize> >(tsize, next, parent, fun) { }

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

Scope *Scope::claim(Heap &h, size_t size, Scope *next, Scope *parent, RFun *fun) {
  bool big = size > 4;
  if (big) {
    return new (h.claim(reserve(size))) BigScope(size, next, parent, fun);
  } else {
    PadObject *dest = h.claim(reserve(size));
    switch (size) {
      case 0:  return new (dest) SmallScope<0>(next, parent, fun);
      case 1:  return new (dest) SmallScope<1>(next, parent, fun);
      case 2:  return new (dest) SmallScope<2>(next, parent, fun);
      case 3:  return new (dest) SmallScope<3>(next, parent, fun);
      default: return new (dest) SmallScope<4>(next, parent, fun);
    }
  }
}

Scope *Scope::alloc(Heap &h, size_t size, Scope *next, Scope *parent, RFun *fun) {
  h.reserve(reserve(size));
  return claim(h, size, next, parent, fun);
}
