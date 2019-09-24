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
#include "ssa.h"
#include "value.h"
#include "status.h"
#include "job.h"
#include "profile.h"
#include <cassert>
#include <sys/time.h>
#include <signal.h>

#define PROFILE_HZ 1000

static volatile bool trace_needed = false;
static void handle_SIGPROF(int sig) {
  (void)sig;
  trace_needed = true;
}

Closure::Closure(RFun *fun_, size_t applied_, Scope *scope_)
 : fun(fun_), applied(applied_), scope(scope_) { }

void Work::format(std::ostream &os, FormatState &state) const {
  os << "Work";
}

Hash Work::hash() const {
  assert(0 /* unreachable */);
  return Hash();
}

Category Work::category() const {
  return WORK;
}

Runtime::Runtime(Profile *profile_, int profile_heap, double heap_factor)
 : abort(false),
   profile(profile_),
   heap(profile_heap, heap_factor),
   stack(heap.root<Work>(nullptr)),
   output(heap.root<HeapObject>(nullptr)),
   sources(heap.root<HeapObject>(nullptr)) {
  if (profile) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));

    // Setup a SIGPROF timer to trigger stack tracing
    struct itimerval timer;
    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = 1000000/PROFILE_HZ;
    timer.it_interval = timer.it_value;

    sa.sa_handler = handle_SIGPROF;
    sa.sa_flags = SA_RESTART;
    sigaction(SIGPROF, &sa, 0);
    setitimer(ITIMER_PROF, &timer, 0);
  }
}

Runtime::~Runtime() {
  struct itimerval timer;
  memset(&timer, 0, sizeof(timer));
  setitimer(ITIMER_PROF, &timer, 0);
}

struct Interpret final : public GCObject<Interpret, Work> {
  RFun *fun;
  size_t index;
  HeapPointer<Scope> scope;
  HeapPointer<Continuation> cont;

  Interpret(RFun *fun_, Scope *scope_, Continuation *cont_)
   : fun(fun_), index(0), scope(scope_), cont(cont_) { }

  template <typename T, T (HeapPointerBase::*memberfn)(T x)>
  T recurse(T arg) {
    arg = Work::recurse<T, memberfn>(arg);
    arg = (scope.*memberfn)(arg);
    arg = (cont.*memberfn)(arg);
    return arg;
  }

  void execute(Runtime &runtime) override;
};

struct InterpretContext {
  Runtime &runtime;
  Interpret *interpret;
  Scope *scope;
  size_t output; // index into scope
  Continuation *cont; // optional

  InterpretContext(Runtime &runtime_) : runtime(runtime_) { }
  static Promise *arg(Scope *scope, size_t arg);

  Promise *arg(size_t arg_) { return arg(scope, arg_); }
  Continuation *defer();
  void finish(HeapObject *obj);
  void finish(Promise *p);
};

void Interpret::execute(Runtime &runtime) {
  InterpretContext context(runtime);
  context.interpret = this;
  context.scope = scope.get();
  context.cont = nullptr;

  size_t limit = fun->terms.size();
  bool tail = fun->output == make_arg(0, limit-1);
  if (tail) --limit;

  next = nullptr; // potentially reschedule
  for (context.output = index; context.output < limit; context.output = index) {
    fun->terms[context.output]->interpret(context);
    index = context.output+1;
    if (next) return;
  }

  if (tail) {
    context.interpret = nullptr;
    context.cont = cont.get();
    fun->terms.back()->interpret(context);
  } else {
    context.arg(fun->output)->await(runtime, cont.get());
  }
}

Continuation *InterpretContext::defer() {
  if (cont) {
    return cont;
  } else {
    return scope->claim_fulfiller(runtime, output);
  }
}

void InterpretContext::finish(HeapObject *obj) {
  if (cont) {
    cont->resume(runtime, obj);
  } else {
    scope->at(output)->instant_fulfill(obj);
  }
}

void InterpretContext::finish(Promise *p) {
  if (*p) {
    finish(p->coerce<HeapObject>());
  } else {
    p->await(runtime, defer());
  }
}

Promise *InterpretContext::arg(Scope *it, size_t arg) {
  for (size_t depth = arg_depth(arg); depth > 0; --depth)
    it = it->next.get();
  return it->at(arg_offset(arg));
}

void RArg::interpret(InterpretContext &context) {
  // noop; filled in by App
}

void RLit::interpret(InterpretContext &context) {
  context.finish(value->get());
}

void RFun::interpret(InterpretContext &context) {
  context.finish(Closure::alloc(context.runtime.heap, this, 0, context.scope));
}

void RCon::interpret(InterpretContext &context) {
  size_t size = kind->ast.args.size();
  context.runtime.heap.reserve(Record::reserve(size) + size * Tuple::fulfiller_pads);
  Record *bind = Record::claim(context.runtime.heap, kind.get(), size);
  for (size_t i = 0; i < args.size(); ++i)
    bind->claim_instant_fulfiller(context.runtime, i, context.arg(args[i]));
  context.finish(bind);
}

struct CGet final : public GCObject<CGet, Continuation> {
  HeapPointer<Continuation> cont;
  size_t index;

  CGet(Continuation *cont_, size_t index_)
   : cont(cont_), index(index_) { }

  template <typename T, T (HeapPointerBase::*memberfn)(T x)>
  T recurse(T arg) {
    arg = Continuation::recurse<T, memberfn>(arg);
    arg = (cont.*memberfn)(arg);
    return arg;
  }

  void execute(Runtime &runtime) override {
     Record *record = static_cast<Record*>(value.get());
     record->at(index)->await(runtime, cont.get());
  }
};

void RGet::interpret(InterpretContext &context) {
  Promise *arg = context.arg(args[0]);
  if (*arg) {
    context.runtime.heap.reserve(Tuple::fulfiller_pads);
    context.finish(arg->coerce<Record>()->at(index));
  } else {
    context.runtime.heap.reserve(Tuple::fulfiller_pads + CGet::reserve());
    arg->await(context.runtime,
      CGet::claim(context.runtime.heap, context.defer(), index));
  }    
}

struct CDes final : public GCObject<CDes, Continuation> {
  HeapPointer<Scope> scope;
  HeapPointer<Continuation> cont;
  RDes *des;

  CDes(Scope *scope_, Continuation *cont_, RDes *des_)
   : scope(scope_), cont(cont_), des(des_) { }

  template <typename T, T (HeapPointerBase::*memberfn)(T x)>
  T recurse(T arg) {
    arg = Continuation::recurse<T, memberfn>(arg);
    arg = (scope.*memberfn)(arg);
    arg = (cont.*memberfn)(arg);
    return arg;
  }

  void execute(Runtime &runtime) override {
    Record *record = static_cast<Record*>(value.get());
    Closure *handler = InterpretContext::arg(scope.get(), des->args[record->cons->index])->coerce<Closure>();
    runtime.heap.reserve(runtime.reserve_apply(handler->fun));
    runtime.claim_apply(handler, record, cont.get(), scope.get());
  }
};

void RDes::interpret(InterpretContext &context) {
  Promise *arg = context.arg(args.back());
  if (*arg) {
    Record *record = arg->coerce<Record>();
    Closure *handler = InterpretContext::arg(context.scope, args[record->cons->index])->coerce<Closure>();
    context.runtime.heap.reserve(context.runtime.reserve_apply(handler->fun));
    if (context.interpret) context.runtime.schedule(context.interpret);
    context.runtime.claim_apply(handler, record, context.defer(), context.scope);
  } else {
    context.runtime.heap.reserve(Tuple::fulfiller_pads + CDes::reserve());
    arg->await(context.runtime,
      CDes::claim(context.runtime.heap, context.scope, context.defer(), this));
  }
}

struct CPrim final : public GCObject<CPrim, Continuation> {
  HeapPointer<Scope> scope;
  HeapPointer<Continuation> cont;
  RPrim *prim;

  CPrim(Scope *scope_, Continuation *cont_, RPrim *prim_)
   : scope(scope_), cont(cont_), prim(prim_) { }

  template <typename T, T (HeapPointerBase::*memberfn)(T x)>
  T recurse(T arg) {
    arg = Continuation::recurse<T, memberfn>(arg);
    arg = (scope.*memberfn)(arg);
    arg = (cont.*memberfn)(arg);
    return arg;
  }

  static Promise *doit(Runtime &runtime, Scope *scope, RPrim *prim, Continuation *cont);
  void execute(Runtime &runtime) override {
    if (Promise *p = doit(runtime, scope.get(), prim, cont.get())) {
      next = nullptr; // reschedule
      p->await(runtime, this);
    }
  }
};

Promise *CPrim::doit(Runtime &runtime, Scope *scope, RPrim *prim, Continuation *cont) {
  HeapObject *pargs[prim->args.size()];
  Promise *p = nullptr;
  size_t i;
  for (i = 0; i < prim->args.size(); ++i) {
    p = InterpretContext::arg(scope, prim->args[i]);
    if (!*p) break;
    pargs[i] = p->coerce<HeapObject>();
  }
  if (i == prim->args.size()) {
    prim->fn(prim->data, runtime, cont, scope, prim->args.size(), pargs);
    return nullptr;
  } else {
    return p;
  }
}

void RPrim::interpret(InterpretContext &context) {
  context.runtime.heap.reserve(Tuple::fulfiller_pads + CPrim::reserve());
  Continuation *cont = context.defer(); // !!! pointless deferral for pure Prims
  if (Promise *p = CPrim::doit(context.runtime, context.scope, this, cont)) {
    CPrim *prim = CPrim::claim(context.runtime.heap, context.scope, context.defer(), this);
    p->await(context.runtime, prim);
  }
}

struct CApp final : public GCObject<CApp, Continuation> {
  HeapPointer<Scope> caller;
  HeapPointer<Continuation> cont;
  RApp *app;

  CApp(Scope *caller_, Continuation *cont_, RApp *app_) : caller(caller_), cont(cont_), app(app_) { }

  template <typename T, T (HeapPointerBase::*memberfn)(T x)>
  T recurse(T arg) {
    arg = Continuation::recurse<T, memberfn>(arg);
    arg = (caller.*memberfn)(arg);
    arg = (cont.*memberfn)(arg);
    return arg;
  }

  static void doit(Runtime &runtime, Closure *closure, RApp *app, Scope *caller, Continuation *cont, Interpret *resume);
  void execute(Runtime &runtime) override {
    doit(runtime, static_cast<Closure*>(value.get()), app, caller.get(), cont.get(), nullptr);
  }
};

void CApp::doit(Runtime &runtime, Closure *closure, RApp *app, Scope *caller, Continuation *cont, Interpret *resume) {
  RFun *fun = closure->fun;
  size_t applied = closure->applied;
  size_t nargs = app->args.size() - 1;
  size_t fargs = fun->args();
  size_t terms = fun->terms.size();
  Scope *callee = closure->scope.get();
  
  if (applied + nargs == fargs) {
    runtime.heap.reserve(Scope::reserve(terms) + fargs*Tuple::fulfiller_pads + Interpret::reserve());
    // Skip over partially applied arguments
    Scope *it = callee;
    for (size_t pop = applied; pop; pop -= it->size())
      it = it->next.get();
    // Fully applied function; allocate "stack" frame
    Scope *bind = Scope::claim(runtime.heap, terms, it, caller, fun);
    // Fill in App() args
    for (size_t i = 0; i < nargs; ++i)
      bind->claim_instant_fulfiller(runtime, applied+i, InterpretContext::arg(caller, app->args[i+1]));
    // Forward the partially applied arguments
    it = callee;
    size_t pop = applied;
    while (pop) {
      size_t size = it->size();
      pop -= size;
      for (size_t i = 0; i <= size; ++i)
        bind->claim_instant_fulfiller(runtime, pop+i, it->at(i));
      it = it->next.get();
    }
    // Schedule an Interpreter
    Interpret *interpret = Interpret::claim(runtime.heap, fun, bind, cont);
    if (resume) runtime.schedule(resume);
    runtime.schedule(interpret);
  } else {
    runtime.heap.reserve(Scope::reserve(nargs) + nargs*Tuple::fulfiller_pads + Closure::reserve());
    Scope *bind = Scope::claim(runtime.heap, nargs, callee, caller, fun);
    for (size_t i = 0; i <= nargs; ++i)
      bind->claim_instant_fulfiller(runtime, i, InterpretContext::arg(caller, app->args[i+1]));
    cont->resume(runtime,
      Closure::claim(runtime.heap, fun, applied+nargs, bind));
  }
}

void RApp::interpret(InterpretContext &context) {
  context.runtime.heap.reserve(Tuple::fulfiller_pads + CApp::reserve());
  Continuation *cont = context.defer();
  Promise *fn = context.arg(args[0]);
  if (*fn) {
    CApp::doit(context.runtime, fn->coerce<Closure>(), this, context.scope, cont, context.interpret);
  } else {
    fn->await(context.runtime, CApp::claim(context.runtime.heap, context.scope, cont, this));
  }
}

size_t Runtime::reserve_apply(RFun *fun) {
  return Scope::reserve(fun->terms.size()) + Tuple::fulfiller_pads + Interpret::reserve();
}

void Runtime::claim_apply(Closure *closure, HeapObject *value, Continuation *cont, Scope *caller) {
  RFun *fun = closure->fun;
  Scope *bind = Scope::claim(heap, fun->terms.size(), closure->scope.get(), caller, fun);
  bind->at(0)->instant_fulfill(value);
  Interpret *interpret = Interpret::claim(heap, fun, bind, cont);
  schedule(interpret);
}

void Runtime::run() {
  int count = 0;
  bool lprofile = profile;
  trace_needed = false; // don't count time spent waiting for Jobs
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
      if (lprofile && trace_needed) {
        if (Interpret *i = dynamic_cast<Interpret*>(w)) {
          auto stack = i->scope->stack_trace(false);
          Profile *node = profile;
          for (auto it = stack.rbegin(); it != stack.rend(); ++it)
            node = &node->children[*it];
          ++node->count;
          trace_needed = false;
        }
      }
    } catch (GCNeededException gc) {
      // retry work after memory is available
      w->next = stack;
      stack = w;
      heap.GC(gc.needed);
      trace_needed = false; // don't count time spent running GC
    }
  }
}

struct CInit final : public GCObject<CInit, Continuation> {
  void execute(Runtime &runtime) override {
    runtime.output = value;
  }
};

void Runtime::init(RFun *root) {
  heap.guarantee(CInit::reserve() + Closure::reserve() + reserve_apply(root));
  CInit *done = CInit::claim(heap);
  Closure *clo = Closure::claim(heap, root, 0, nullptr);
  claim_apply(clo, clo, done, nullptr);
}
