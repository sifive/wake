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

#include "runtime.h"
#include "tuple.h"
#include "expr.h"
#include "value.h"
#include "status.h"
#include "job.h"
#include <cassert>

Closure::Closure(Lambda *lambda_, Scope *scope_) : lambda(lambda_), scope(scope_) { }

void Work::format(std::ostream &os, FormatState &state) const {
  os << "Work";
}

Hash Work::hash() const {
  assert(0 /* unreachable */);
  return Hash();
}

bool Work::is_work() const {
  return true;
}

Runtime::Runtime()
 : abort(false), heap(),
   stack(heap.root<Work>(nullptr)),
   output(heap.root<HeapObject>(nullptr)),
   sources(heap.root<HeapObject>(nullptr)) {
}

void Runtime::run() {
  int count = 0;
  while (stack && !abort) {
    if (++count >= 10000) {
      if (JobTable::exit_now()) break;
      status_refresh();
      count = 0;
    }
    Work *w = stack.get();
    stack = w->next;
    try {
      w->execute(*this);
    } catch (GCNeededException gc) {
      // retry work after memory is available
      w->next = stack;
      stack = w;
      heap.GC(gc.needed);
    }
  }
}

struct Interpret final : public GCObject<Interpret, Work> {
  Expr *expr;
  HeapPointer<Scope> scope;
  HeapPointer<Continuation> cont;

  Interpret(Expr *expr_, Scope *scope_, Continuation *cont_)
   : expr(expr_), scope(scope_), cont(cont_) { }

  template <typename T, T (HeapPointerBase::*memberfn)(T x)>
  T recurse(T arg) {
    arg = Work::recurse<T, memberfn>(arg);
    arg = (scope.*memberfn)(arg);
    arg = (cont.*memberfn)(arg);
    return arg;
  }

  void execute(Runtime &runtime) override {
    expr->interpret(runtime, scope.get(), cont.get());
  }
};

struct CInit final : public GCObject<CInit, Continuation> {
  void execute(Runtime &runtime) override {
    runtime.output = value;
  }
};

void Runtime::init(Expr *root) {
  heap.guarantee(Interpret::reserve() + CInit::reserve());
  CInit *done = CInit::claim(heap);
  schedule(Interpret::claim(heap, root, nullptr, done));
}

size_t Runtime::reserve_eval() {
  return Interpret::reserve();
}

void Runtime::claim_eval(Expr *expr, Scope *scope, Continuation *cont) {
  schedule(Interpret::claim(heap, expr, scope, cont));
}

void Lambda::interpret(Runtime &runtime, Scope *scope, Continuation *cont) {
  cont->resume(runtime, Closure::alloc(runtime.heap, this, scope));
}

void VarRef::interpret(Runtime &runtime, Scope *scope, Continuation *cont) {
  for (int i = depth; i; --i)
    scope = scope->next.get();
  if (lambda) {
    cont->resume(runtime, Closure::alloc(runtime.heap, lambda, scope));
  } else {
    scope->at(offset)->await(runtime, cont);
  }
}

struct CApp final : public GCObject<CApp, Continuation> {
  HeapPointer<Scope> bind;
  HeapPointer<Continuation> cont;

  CApp(Scope *bind_, Continuation *cont_) : bind(bind_), cont(cont_) { }

  template <typename T, T (HeapPointerBase::*memberfn)(T x)>
  T recurse(T arg) {
    arg = Continuation::recurse<T, memberfn>(arg);
    arg = (bind.*memberfn)(arg);
    arg = (cont.*memberfn)(arg);
    return arg;
  }

  void execute(Runtime &runtime) override {
    auto clo = static_cast<Closure*>(value.get());
    runtime.heap.reserve(Interpret::reserve());
    bind->next = clo->scope.get();
    runtime.schedule(Interpret::claim(runtime.heap,
      clo->lambda->body.get(), bind.get(), cont.get()));
  }
};

void App::interpret(Runtime &runtime, Scope *scope, Continuation *cont) {
  runtime.heap.reserve(Scope::reserve(1) +
    Interpret::reserve() + CApp::reserve() +
    Interpret::reserve() + Tuple::fulfiller_pads);
  Scope *bind = Scope::claim(runtime.heap, nullptr, 1);
  runtime.schedule(Interpret::claim(runtime.heap,
    fn.get(), scope, CApp::claim(runtime.heap, bind, cont)));
  runtime.schedule(Interpret::claim(runtime.heap,
    val.get(), scope, bind->claim_fulfiller(runtime, 0)));
}

void DefBinding::interpret(Runtime &runtime, Scope *scope, Continuation *cont) {
  size_t size = val.size();
  runtime.heap.reserve(Scope::reserve(size) + Interpret::reserve() +
    val.size() * (Interpret::reserve() + Tuple::fulfiller_pads));
  Scope *bind = Scope::claim(runtime.heap, scope, size);
  runtime.schedule(Interpret::claim(runtime.heap,
    body.get(), bind, cont));
  for (auto it = val.rbegin(); it != val.rend(); ++it)
    runtime.schedule(Interpret::claim(runtime.heap,
      it->get(), scope, bind->claim_fulfiller(runtime, --size)));
}

void Literal::interpret(Runtime &runtime, Scope *scope, Continuation *cont) {
  cont->resume(runtime, value.get());
}

struct CPrim final : public GCObject<CPrim, Continuation> {
  HeapPointer<Scope> scope;
  HeapPointer<Continuation> cont;
  Prim *prim;

  CPrim(Scope *scope_, Continuation *cont_, Prim *prim_)
   : scope(scope_), cont(cont_), prim(prim_) { }

  template <typename T, T (HeapPointerBase::*memberfn)(T x)>
  T recurse(T arg) {
    arg = Continuation::recurse<T, memberfn>(arg);
    arg = (scope.*memberfn)(arg);
    arg = (cont.*memberfn)(arg);
    return arg;
  }

  void execute(Runtime &runtime) override {
    HeapObject *args[prim->args];
    Scope *it = scope.get();
    size_t i;
    for (i = prim->args; i; --i) {
      if (!*it->at(0)) break;
      args[i-1] = it->at(0)->coerce<HeapObject>();
      it = it->next.get();
    }
    if (i == 0) {
      prim->fn(prim->data, runtime, cont.get(), scope.get(), prim->args, args);
    } else {
      it->at(0)->await(runtime, this);
    }
  }
};

void Prim::interpret(Runtime &runtime, Scope *scope, Continuation *cont) {
  CPrim *prim = CPrim::alloc(runtime.heap, scope, cont, this);
  runtime.schedule(prim);
}

void Construct::interpret(Runtime &runtime, Scope *scope, Continuation *cont) {
  size_t size = cons->ast.args.size();
  runtime.heap.reserve(Record::reserve(size) + size * Tuple::fulfiller_pads);
  Record *bind = Record::claim(runtime.heap, cons, size);
  cont->resume(runtime, bind);
  // this will benefit greatly from App+App+App+Lam+Lam+Lam->DefMap fusion
  for (size_t i = size; i; --i) {
    bind->claim_instant_fulfiller(runtime, i-1, scope->at(0));
    scope = scope->next.get();
  }
}

struct SelectDestructor final : public GCObject<SelectDestructor, Continuation> {
  HeapPointer<Scope> scope;
  HeapPointer<Continuation> cont;
  Destruct *des;

  SelectDestructor(Scope *scope_, Continuation *cont_, Destruct *des_)
   : scope(scope_), cont(cont_), des(des_) { }

  template <typename T, T (HeapPointerBase::*memberfn)(T x)>
  T recurse(T arg) {
    arg = Continuation::recurse<T, memberfn>(arg);
    arg = (scope.*memberfn)(arg);
    arg = (cont.*memberfn)(arg);
    return arg;
  }

  void execute(Runtime &runtime) override {
    auto record = static_cast<Record*>(value.get());
    size_t size = record->size();
    size_t scope_cost = Scope::reserve(1);
    runtime.heap.reserve(size * (scope_cost + Tuple::fulfiller_pads) + scope_cost + Interpret::reserve());
    // Find the handler function body -- inlining + App fusion would eliminate this loop
    for (int index = des->sum.members.size() - record->cons->index; index; --index)
      scope = scope->next.get();
    // This coercion is safe because we evaluate pure lambda args before their consumer
    auto closure = scope->at(0)->coerce<Closure>();
    auto body = closure->lambda->body.get();
    auto scope = closure->scope.get();
    // Add the record to the handler scope
    auto next = Scope::claim(runtime.heap, scope, 1);
    next->at(0)->instant_fulfill(record);
    scope = next;
    // !!! avoid pointless copying; have handlers directly accept the tuple and use a DefMap:argX = atX tuple
    // -> this would allow unused arguments to be deadcode optimized away
    for (size_t i = 0; i < size; ++i) {
      next = Scope::claim(runtime.heap, scope, 1);
      next->claim_instant_fulfiller(runtime, 0, record->at(i));
      scope = next;
      body = static_cast<Lambda*>(body)->body.get();
    }
    runtime.schedule(Interpret::claim(runtime.heap, body, scope, cont.get()));
  }
};

void Destruct::interpret(Runtime &runtime, Scope *scope, Continuation *cont) {
  scope->at(0)->await(runtime, SelectDestructor::alloc(runtime.heap, scope, cont, this));
}
