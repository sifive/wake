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

Runtime::Runtime() : heap(), stack(heap.root<Work>(nullptr)) {
}

void Runtime::run() {
  while (stack) {
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
  HeapPointer<Tuple> scope;
  HeapPointer<Continuation> cont;

  Interpret(Expr *expr_, Tuple *scope_, Continuation *cont_)
   : expr(expr_), scope(scope_), cont(cont_) { }

  PadObject *recurse(PadObject *free) {
    free = Work::recurse(free);
    free = scope.moveto(free);
    free = cont.moveto(free);
    return free;
  }

  void execute(Runtime &runtime) override {
    expr->interpret(runtime, scope.get(), cont.get());
  }
};

struct FClosure final : public GCObject<FClosure, HeapObject> {
  Lambda *lambda;
  HeapPointer<Tuple> scope;

  FClosure(Lambda *lambda_, Tuple *scope_)
   : lambda(lambda_), scope(scope_) { }

  PadObject *recurse(PadObject *free) {
    free = HeapObject::recurse(free);
    free = scope.moveto(free);
    return free;
  }
};

void Lambda::interpret(Runtime &runtime, Tuple *scope, Continuation *cont) {
  cont->resume(runtime, FClosure::alloc(runtime.heap, this, scope));
}

void VarRef::interpret(Runtime &runtime, Tuple *scope, Continuation *cont) {
  for (int i = depth; i; --i)
    scope = (*scope)[0].coerce<Tuple>();
  int vals = scope->size() - 1;
  if (offset >= vals) {
    auto defs = static_cast<DefBinding*>(scope->meta);
    cont->resume(runtime, FClosure::alloc(runtime.heap,
      defs->fun[offset-vals].get(), scope));
  } else {
    (*scope)[offset+1].await(runtime, cont);
  }
}

struct FApp final : public GCObject<FApp, Continuation> {
  HeapPointer<Tuple> bind;
  HeapPointer<Continuation> cont;

  FApp(Tuple *bind_, Continuation *cont_) : bind(bind_), cont(cont_) { }

  PadObject *recurse(PadObject *free) {
    free = Continuation::recurse(free);
    free = bind.moveto(free);
    free = cont.moveto(free);
    return free;
  }

  void execute(Runtime &runtime) override {
    auto clo = static_cast<FClosure*>(value.get());
    bind->meta = clo->lambda;
    (*bind.get())[0].fulfill(runtime, clo->scope.get());
    runtime.schedule(Interpret::alloc(runtime.heap,
      clo->lambda->body.get(), bind.get(), cont.get()));
  }
};

void App::interpret(Runtime &runtime, Tuple *scope, Continuation *cont) {
  runtime.heap.reserve(Tuple::reserve(2) +
    Interpret::reserve() + FApp::reserve() +
    Interpret::reserve() + Tuple::fulfiller_pads);
  Tuple *bind = Tuple::claim(runtime.heap, nullptr, 2);
  runtime.schedule(Interpret::claim(runtime.heap,
    fn.get(), scope, FApp::claim(runtime.heap, bind, cont)));
  runtime.schedule(Interpret::claim(runtime.heap,
    val.get(), scope, bind->claim_fulfiller(runtime, 1)));
}

void DefBinding::interpret(Runtime &runtime, Tuple *scope, Continuation *cont) {
  size_t size = 1+val.size();
  runtime.heap.reserve(Tuple::reserve(size) + Interpret::reserve() +
    val.size() * (Interpret::reserve() + Tuple::fulfiller_pads));
  Tuple *bind = Tuple::claim(runtime.heap, this, size);
  (*bind)[0].fulfill(runtime, scope);
  runtime.schedule(Interpret::claim(runtime.heap,
    body.get(), bind, cont));
  for (auto it = val.rbegin(); it != val.rend(); ++it)
    runtime.schedule(Interpret::claim(runtime.heap,
      it->get(), scope, bind->claim_fulfiller(runtime, --size)));
}

void Literal::interpret(Runtime &runtime, Tuple *scope, Continuation *cont) {
  // !!! TODO
  // cont->resume(runtime, root.get());
}

void Prim::interpret(Runtime &runtime, Tuple *scope, Continuation *cont) {
  // !!! TODO
}

void Construct::interpret(Runtime &runtime, Tuple *scope, Continuation *cont) {
  size_t size = cons->ast.args.size();
  runtime.heap.reserve(Tuple::reserve(size) + size * Tuple::fulfiller_pads);
  Tuple *bind = Tuple::claim(runtime.heap, cons, size);
  cont->resume(runtime, bind);
  // this will benefit greatly from App+App+App+Lam+Lam+Lam->DefMap fusion
  for (size_t i = size; i; --i) {
    (*scope)[1].await(runtime, bind->claim_fulfiller(runtime, i-1));
    scope = (*scope)[0].coerce<Tuple>();
  }
}

struct SelectDestructor final : public GCObject<SelectDestructor, Continuation> {
  HeapPointer<Tuple> scope;
  HeapPointer<Continuation> cont;
  Destruct *des;

  SelectDestructor(Tuple *scope_, Continuation *cont_, Destruct *des_)
   : scope(scope_), cont(cont_), des(des_) { }

  PadObject *recurse(PadObject *free) {
    free = Continuation::recurse(free);
    free = scope.moveto(free);
    free = cont.moveto(free);
    return free;
  }

  void execute(Runtime &runtime) {
    auto tuple = static_cast<Tuple*>(value.get());
    auto cons = static_cast<Constructor*>(tuple->meta);
    size_t size = tuple->size();
    size_t scope_cost = Tuple::reserve(2);
    runtime.heap.reserve(size * (scope_cost + Tuple::fulfiller_pads) + scope_cost + Interpret::reserve());
    // Find the handler function body -- inlining + App fusion would eliminate this loop
    for (int index = des->sum.members.size() - cons->index; index; --index)
      scope = (*scope.get())[0].coerce<Tuple>();
    // This coercion is safe because we evaluate pure lambda args before their consumer
    auto closure = (*scope.get())[1].coerce<FClosure>();
    auto body = closure->lambda->body.get();
    auto scope = closure->scope.get();
    // !!! avoid pointless copying; have handlers directly accept the tuple and use a DefMap:argX = atX tuple
    // -> this would allow unused arguments to be deadcode optimized away
    for (; size; --size) {
      auto next = Tuple::claim(runtime.heap, des, 2);
      (*next)[0].fulfill(runtime, scope);
      (*tuple)[size-1].await(runtime, next->claim_fulfiller(runtime, 1));
      scope = next;
      body = static_cast<Lambda*>(body)->body.get();
    }
    // !!! actually want to schedule this BEFORE the fulfillers above
    auto next = Tuple::claim(runtime.heap, des, 2);
    (*next)[0].fulfill(runtime, scope);
    (*next)[1].fulfill(runtime, tuple);
    // add scope with tuple itself
    runtime.schedule(Interpret::claim(runtime.heap, body, next, cont.get()));
  }
};

void Destruct::interpret(Runtime &runtime, Tuple *scope, Continuation *cont) {
  (*scope)[1].await(runtime, SelectDestructor::alloc(runtime.heap, scope, cont, this));
}
