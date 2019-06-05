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

#include "thunk.h"
#include "expr.h"
#include "value.h"
#include "datatype.h"
#include "prim.h"
#include "status.h"
#include "job.h"
#include <cassert>

struct Application : public Receiver {
  std::shared_ptr<Binding> args;
  std::unique_ptr<Receiver> receiver;
  void receive(WorkQueue &queue, std::shared_ptr<Value> &&value);
  Application(std::shared_ptr<Binding> &&args_, std::unique_ptr<Receiver> receiver_)
   : args(std::move(args_)), receiver(std::move(receiver_)) { }
};

void Application::receive(WorkQueue &queue, std::shared_ptr<Value> &&value) {
  assert (value->type == &Closure::type);
  Closure *clo = reinterpret_cast<Closure*>(value.get());
  args->next = clo->binding;
  args->expr = clo->lambda;
  queue.emplace(clo->lambda->body.get(), std::move(args), std::move(receiver));
}

struct Destructure : public Receiver {
  std::shared_ptr<Binding> args;
  std::unique_ptr<Receiver> receiver;
  Destruct *des;
  void receive(WorkQueue &queue, std::shared_ptr<Value> &&value);
  Destructure(std::shared_ptr<Binding> &&args_, std::unique_ptr<Receiver> receiver_, Destruct *des_)
   : args(std::move(args_)), receiver(std::move(receiver_)), des(des_) { }
};

void Destructure::receive(WorkQueue &queue, std::shared_ptr<Value> &&value) {
  assert (value->type == &Data::type);
  Data *data = reinterpret_cast<Data*>(value.get());
  assert (&des->sum.members[data->cons->index] == data->cons);
  // Create a binding to hold 'data' and 'fn'
  auto flip = std::make_shared<Binding>(data->binding, queue.stack_trace?args:nullptr, des, 2);
  flip->future[1].value = std::move(value);
  flip->state = -1;
  // Find the correct handler function
  Binding *fn = args.get();
  int limit = des->sum.members.size() - data->cons->index;
  for (int i = 0; i < limit; ++i) fn = fn->next.get();
  fn->future[0].depend(queue, Binding::make_completer(flip, 0));
  // Invoke the chain expr to setup user function
  queue.emplace(data->cons->expr.get(), std::move(flip), std::move(receiver));
}

struct Primitive : public Finisher {
  std::unique_ptr<Receiver> receiver;
  std::shared_ptr<Binding> binding;
  Prim *prim;
  void finish(WorkQueue &queue);
  Primitive(std::unique_ptr<Receiver> receiver_, std::shared_ptr<Binding> &&binding_, Prim *prim_)
   : receiver(std::move(receiver_)), binding(std::move(binding_)), prim(prim_) { }
};

void Primitive::finish(WorkQueue &queue) {
  std::vector<std::shared_ptr<Value> > args(prim->args);
  Binding *iter = binding.get();
  for (int i = prim->args-1; i >= 0; --i) {
    args[i] = iter->future[0].value;
    iter = iter->next.get();
  }
  prim->fn(prim->data, queue, std::move(receiver), std::move(binding), std::move(args));
}

struct MultiReceiverShared {
  std::unique_ptr<Finisher> finisher;
  int todo;
  MultiReceiverShared(std::unique_ptr<Finisher> finisher_, int todo_)
   : finisher(std::move(finisher_)), todo(todo_) { }
};

struct MultiReceiver : public Receiver {
  std::shared_ptr<MultiReceiverShared> shared;
  MultiReceiver(std::shared_ptr<MultiReceiverShared> shared_)
   : shared(std::move(shared_)) { }
  void receive(WorkQueue &queue, std::shared_ptr<Value> &&value);
};

void MultiReceiver::receive(WorkQueue &queue, std::shared_ptr<Value> &&value) {
  (void)value; // we're just waiting for the value to be ready
  if (--shared->todo == 0)
    Finisher::finish(queue, std::move(shared->finisher));
}

void Thunk::eval(WorkQueue &queue) {
  if (expr->type == &VarRef::type) {
    VarRef *ref = reinterpret_cast<VarRef*>(expr);
    std::shared_ptr<Binding> *iter = &binding;
    for (int depth = ref->depth; depth; --depth)
      iter = &(*iter)->next;
    int vals = (*iter)->nargs;
    if (ref->offset >= vals) {
      auto defs = reinterpret_cast<DefBinding*>((*iter)->expr);
      auto closure = std::make_shared<Closure>(defs->fun[ref->offset-vals].get(), *iter);
      Receiver::receive(queue, std::move(receiver), std::move(closure));
    } else {
      (*iter)->future[ref->offset].depend(queue, std::move(receiver));
    }
  } else if (expr->type == &App::type) {
    App *app = reinterpret_cast<App*>(expr);
    auto args = std::make_shared<Binding>(nullptr, queue.stack_trace?binding:nullptr, nullptr, 1);
    queue.emplace(app->val.get(), binding, Binding::make_completer(args, 0));
    queue.emplace(app->fn .get(), std::move(binding), std::unique_ptr<Receiver>(
      new Application(std::move(args), std::move(receiver))));
  } else if (expr->type == &Lambda::type) {
    Lambda *lambda = reinterpret_cast<Lambda*>(expr);
    auto closure = std::make_shared<Closure>(lambda, binding);
    Receiver::receive(queue, std::move(receiver), std::move(closure));
  } else if (expr->type == &DefBinding::type) {
    DefBinding *defbinding = reinterpret_cast<DefBinding*>(expr);
    auto defs = std::make_shared<Binding>(binding, queue.stack_trace?binding:nullptr, defbinding, defbinding->val.size());
    int j = 0;
    for (auto &i : defbinding->val)
      queue.emplace(i.get(), binding, Binding::make_completer(defs, j++));
    queue.emplace(defbinding->body.get(), std::move(defs), std::move(receiver));
  } else if (expr->type == &Construct::type) {
    Construct *cons = reinterpret_cast<Construct*>(expr);
    if (cons->cons->ast.args.size() == 0) {
      Receiver::receive(queue, std::move(receiver),
        std::make_shared<Data>(cons->cons, nullptr));
    } else {
      Binding *iter = binding.get();
      for (size_t i = 1; i < cons->cons->ast.args.size(); ++i) iter = iter->next.get();
      iter->next.reset();
      Receiver::receive(queue, std::move(receiver),
        std::make_shared<Data>(cons->cons, std::move(binding)));
    }
  } else if (expr->type == &Destruct::type) {
    Destruct *des = reinterpret_cast<Destruct*>(expr);
    Binding *data = binding.get();
    data->future[0].depend(queue, std::unique_ptr<Receiver>(
      new Destructure(std::move(binding), std::move(receiver), des)));
  } else if (expr->type == &Literal::type) {
    Literal *lit = reinterpret_cast<Literal*>(expr);
    Receiver::receive(queue, std::move(receiver), lit->value);
  } else if (expr->type == &Prim::type) {
    Prim *prim = reinterpret_cast<Prim*>(expr);
    // Cut the scope of primitive to only it's own arguments
    if (prim->args == 0) {
      std::vector<std::shared_ptr<Value> > args;
      prim->fn(prim->data, queue, std::move(receiver), nullptr, std::move(args));
    } else {
      Binding *iter = binding.get();
      for (int i = 1; i < prim->args; ++i) iter = iter->next.get();
      iter->next.reset();
      Binding *held = binding.get();
      std::unique_ptr<Finisher> finisher(
        new Primitive(std::move(receiver), std::move(binding), prim));
      if ((prim->flags & PRIM_SHALLOW)) {
        auto shared = std::make_shared<MultiReceiverShared>(
          std::move(finisher), prim->args);
        for (iter = held; iter; iter = iter->next.get())
          for (int i = 0; i < iter->nargs; ++i)
            iter->future[i].depend(queue,
              std::unique_ptr<Receiver>(new MultiReceiver(shared)));
      } else {
        Binding::wait(held, queue, std::move(finisher));
      }
    }
  } else {
    assert(0 /* unreachable */);
  }
}

void Receive::eval(WorkQueue &queue) {
  if (value) {
    reinterpret_cast<Receiver*>(callback.get())->receive(queue, std::move(value));
  } else {
    reinterpret_cast<Finisher*>(callback.get())->finish(queue);
  }
}

void WorkQueue::run() {
  int count = 0;
  while (!receives.empty() && !abort) {
    receives.front().eval(*this);
    receives.pop();
  }
  while (!thunks.empty() && !abort) {
    if (++count >= 10000) {
      if (JobTable::exit_now()) break;
      status_refresh();
      count = 0;
    }
    thunks.front().eval(*this);
    thunks.pop();
    while (!receives.empty() && !abort) {
      receives.front().eval(*this);
      receives.pop();
    }
  }
}

void WorkQueue::emplace(Expr *expr, std::shared_ptr<Binding> &&binding, std::unique_ptr<Receiver> receiver) {
  thunks.emplace(expr, std::move(binding), std::move(receiver));
}

void WorkQueue::emplace(Expr *expr, const std::shared_ptr<Binding> &binding, std::unique_ptr<Receiver> receiver) {
  thunks.emplace(expr, std::shared_ptr<Binding>(binding), std::move(receiver));
}

void Receiver::receive(WorkQueue &queue, std::unique_ptr<Receiver> receiver, std::shared_ptr<Value> &&value) {
  queue.receives.emplace(std::move(receiver), std::move(value));
}

void Receiver::receive(WorkQueue &queue, std::unique_ptr<Receiver> receiver, const std::shared_ptr<Value> &value) {
  queue.receives.emplace(std::move(receiver), std::shared_ptr<Value>(value));
}

void Finisher::finish(WorkQueue &queue, std::unique_ptr<Finisher> finisher) {
  queue.receives.emplace(std::move(finisher), nullptr);
}
