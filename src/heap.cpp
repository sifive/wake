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

#include "heap.h"
#include "location.h"
#include "value.h"
#include "expr.h"
#include <cassert>

Callback::~Callback() { }

struct Completer : public Receiver {
  std::shared_ptr<Binding> binding;
  Future *future;
  void receive(WorkQueue &queue, std::shared_ptr<Value> &&value);
  Completer(const std::shared_ptr<Binding> &binding_, Future *future_)
   : binding(binding_), future(future_) { }
};

void Completer::receive(WorkQueue &queue, std::shared_ptr<Value> &&value) {
  future->broadcast(queue, std::move(value));
  if (binding) binding->future_completed(queue);
}

std::unique_ptr<Receiver> Future::make_completer() {
  return std::unique_ptr<Receiver>(new Completer(nullptr, this));
}

void Future::broadcast(WorkQueue &queue, std::shared_ptr<Value> &&value_) {
  value = std::move(value_);
  std::unique_ptr<Receiver> iter, next;
  for (iter = std::move(waiting); iter; iter = std::move(next)) {
    next = std::move(iter->next);
    Receiver::receive(queue, std::move(iter), value);
  }
}

struct ChildFinisher : public Finisher {
  Binding *binding;
  ChildFinisher(Binding *binding_) : binding(binding_) { }
  void finish(WorkQueue &queue);
};

void ChildFinisher::finish(WorkQueue &queue) {
  ++binding->state;
  binding->future_finished(queue);
}

Binding::Binding(const std::shared_ptr<Binding> &next_, const std::shared_ptr<Binding> &invoker_, Expr *expr_, int nargs_)
  : next(next_), invoker(invoker_), finisher(), future(new Future[nargs_]),
    hashcode(), expr(expr_), nargs(nargs_), state(0), flags(0) { }

std::unique_ptr<Receiver> Binding::make_completer(const std::shared_ptr<Binding> &binding, int arg) {
  return std::unique_ptr<Receiver>(new Completer(binding, &binding->future[arg]));
}

std::vector<Location> Binding::stack_trace() const {
  std::vector<Location> out;
  for (const Binding *i = this; i; i = i->invoker.get())
    if (i->expr->type != &DefBinding::type)
      out.emplace_back(i->expr->location);
  return out;
}

void Binding::wait(Binding *iter, WorkQueue &queue, std::unique_ptr<Finisher> finisher) {
  while (iter && iter->state == iter->nargs)
    iter = iter->next.get();

  if (iter) {
    finisher->next = std::move(iter->finisher);
    iter->finisher = std::move(finisher);
    // This can only cause recursion as deep as the deepest lexical scope
    if (iter->state == -iter->nargs)
      iter->future_finished(queue);
  } else {
    Finisher::finish(queue, std::move(finisher));
  }
}

Hash Binding::hash() const {
  std::vector<const Binding*> stack;

  // Post-order iterative DAG traversal
  stack.push_back(this);
  while (!stack.empty()) {
    const Binding *top = stack.back();
    if ((top->flags & FLAG_HASH_POST) != 0) {
      // already finished
      stack.pop_back();
    } else if ((top->flags & FLAG_HASH_PRE) != 0) {
      // children visited; compute hashcode
      top->flags |= FLAG_HASH_POST;
      std::vector<uint64_t> codes;
      if (top->next) top->next->hashcode.push(codes);
      for (int arg = 0; arg < top->nargs; ++arg)
        top->future[arg].value->hash().push(codes);
      top->hashcode = Hash(codes);
      stack.pop_back();
    } else {
      // explore children, NO POP
      top->flags |= FLAG_HASH_PRE;
      if (top->next) stack.push_back(top->next.get());
      for (int arg = 0; arg < top->nargs; ++arg) {
        Value *value = top->future[arg].value.get();
        if (value->type == &Closure::type) {
          Binding *child = reinterpret_cast<Closure*>(value)->binding.get();
          if (child) stack.push_back(child);
          assert (!child || (child->flags & FLAG_HASH_POST) || !(child->flags & FLAG_HASH_PRE)); // cycle
        }
        if (value->type == &Data::type) {
          Binding *child = reinterpret_cast<Data*>(value)->binding.get();
          if (child) stack.push_back(child);
          assert (!child || (child->flags & FLAG_HASH_POST) || !(child->flags & FLAG_HASH_PRE)); // cycle
        }
      }
    }
  }

  return hashcode;
}

static void flatten_orphan(Binding *top, std::shared_ptr<Binding> &&orphan) {
  while (orphan.use_count() == 1) {
    // this sort of pointer manipulation is safe because we hold the only reference
    std::shared_ptr<Binding> deref = std::move(orphan->next);
    orphan->next = std::move(top->next);
    top->next = std::move(orphan);
    orphan = std::move(deref);
  }
}

static void flatten_orphans(Binding *top) {
  top->flags |= FLAG_FLATTENED;
  if (top->invoker == top->next) top->invoker.reset();
  for (int arg = 0; arg < top->nargs; ++arg) {
    std::shared_ptr<Value> &value = top->future[arg].value;
    if (value.use_count() == 1) {
      if (value->type == &Closure::type)
        flatten_orphan(top, std::move(reinterpret_cast<Closure*>(value.get())->binding));
      if (value->type == &Data::type)
        flatten_orphan(top, std::move(reinterpret_cast<Data*>(value.get())->binding));
    }
    value.reset();
  }
  flatten_orphan(top, std::move(top->invoker));
}

Binding::~Binding() {
  if ((flags & FLAG_FLATTENED) != 0) return;
  flatten_orphans(this);
  while (next.use_count() == 1) {
    flatten_orphans(next.get());
    next = std::move(next->next); // does not cause recursive ~Binding
  }
}

void Binding::future_completed(WorkQueue &queue) {
  --state;
  if (state == -nargs && finisher)
    future_finished(queue);
}

void Binding::future_finished(WorkQueue &queue) {
  if (state < 0) state = 0;

  while (state < nargs &&
    (future[state].value->type != &Closure::type ||
     !reinterpret_cast<Closure*>(future[state].value.get())->binding) &&
    (future[state].value->type != &Data::type ||
     !reinterpret_cast<Data*>(future[state].value.get())->binding)) {
    ++state;
  }

  if (state == nargs) {
    std::unique_ptr<Finisher> iter, iter_next;
    for (iter = std::move(finisher); iter; iter = std::move(iter_next)) {
      iter_next = std::move(iter->next);
      Binding::wait(next.get(), queue, std::move(iter));
    }
  } else {
    if (future[state].value->type == &Closure::type) {
      Closure *closure = reinterpret_cast<Closure*>(future[state].value.get());
      Binding::wait(closure->binding.get(), queue, std::unique_ptr<Finisher>(new ChildFinisher(this)));
    }
    if (future[state].value->type == &Data::type) {
      Data *data = reinterpret_cast<Data*>(future[state].value.get());
      Binding::wait(data->binding.get(), queue, std::unique_ptr<Finisher>(new ChildFinisher(this)));
    }
  }
}
