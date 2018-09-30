#include "thunk.h"
#include "expr.h"
#include "value.h"
#include <cassert>
#include <iostream>

struct Application : public Receiver {
  std::shared_ptr<Binding> args;
  std::unique_ptr<Receiver> receiver;
  void receive(WorkQueue &queue, std::shared_ptr<Value> &&value);
  Application(std::shared_ptr<Binding> &&args_, std::unique_ptr<Receiver> receiver_)
   : args(std::move(args_)), receiver(std::move(receiver_)) { }
};

void Application::receive(WorkQueue &queue, std::shared_ptr<Value> &&value) {
  if (value->type == Exception::type) {
    Receiver::receive(queue, std::move(receiver), std::move(value));
  } else if (value->type != Closure::type) {
    auto exception = std::make_shared<Exception>("Attempt to apply " + value->to_str() + " which is not a Closure", args->invoker);
    Receiver::receive(queue, std::move(receiver), std::move(exception));
  } else {
    Closure *clo = reinterpret_cast<Closure*>(value.get());
    args->next = clo->binding;
    args->expr = clo->body;
    queue.emplace(clo->body, std::move(args), std::move(receiver));
  }
}

struct Primitive : public Receiver {
  std::vector<std::shared_ptr<Value> > args;
  std::unique_ptr<Receiver> receiver;
  std::shared_ptr<Binding> binding;
  Prim *prim;
  void receive(WorkQueue &queue, std::shared_ptr<Value> &&value);
  Primitive(std::vector<std::shared_ptr<Value> > &&args_, std::unique_ptr<Receiver> receiver_, std::shared_ptr<Binding> &&binding_, Prim *prim_)
   : args(std::move(args_)), receiver(std::move(receiver_)), binding(std::move(binding_)), prim(prim_) { }
};

static void chain_app(WorkQueue &queue, std::unique_ptr<Receiver> receiver, Prim *prim, std::shared_ptr<Binding> &&binding, std::vector<std::shared_ptr<Value> > &&args) {
  int idx = args.size();
  if (idx == prim->args) {
    prim->fn(prim->data, queue, std::move(receiver), std::move(binding), std::move(args));
  } else {
    Binding *arg = binding.get();
    for (int i = idx+1; i < prim->args; ++i) arg = arg->next.get();
    arg->future[0].depend(queue, std::unique_ptr<Receiver>(
      new Primitive(std::move(args), std::move(receiver), std::move(binding), prim)));
  }
}

void Primitive::receive(WorkQueue &queue, std::shared_ptr<Value> &&value) {
  args.emplace_back(std::move(value));
  chain_app(queue, std::move(receiver), prim, std::move(binding), std::move(args));
}

struct MemoizeHasher : public Hasher {
  std::unique_ptr<Receiver> receiver;
  std::shared_ptr<Binding> binding;
  Memoize *memoize;
  MemoizeHasher(std::unique_ptr<Receiver> receiver_, const std::shared_ptr<Binding> &binding_, Memoize *memoize_)
   : receiver(std::move(receiver_)), binding(binding_), memoize(memoize_) { }
  void receive(WorkQueue &queue, Hash hash);
};

void MemoizeHasher::receive(WorkQueue &queue, Hash hash)
{
  auto i = memoize->values.find(hash);
  if (i == memoize->values.end()) {
    Future &future = memoize->values[hash];
    future.depend(queue, std::move(receiver));
    queue.emplace(memoize->body.get(), std::move(binding), future.make_completer());
  } else {
    i->second.depend(queue, std::move(receiver));
  }
}

void Thunk::eval(WorkQueue &queue) {
  if (expr->type == VarRef::type) {
    VarRef *ref = reinterpret_cast<VarRef*>(expr);
    std::shared_ptr<Binding> *iter = &binding;
    for (int depth = ref->depth; depth; --depth)
      iter = &(*iter)->next;
    int vals = (*iter)->nargs;
    if (ref->offset >= vals) {
      auto defs = reinterpret_cast<DefBinding*>((*iter)->expr);
      auto closure = std::make_shared<Closure>(defs->fun[ref->offset-vals]->body.get(), *iter);
      Receiver::receive(queue, std::move(receiver), std::move(closure));
    } else {
      (*iter)->future[ref->offset].depend(queue, std::move(receiver));
    }
  } else if (expr->type == App::type) {
    App *app = reinterpret_cast<App*>(expr);
    auto args = std::make_shared<Binding>(nullptr, queue.stack_trace?binding:nullptr, nullptr, 1);
    queue.emplace(app->val.get(), binding, Binding::make_completer(args, 0));
    queue.emplace(app->fn .get(), std::move(binding), std::unique_ptr<Receiver>(
      new Application(std::move(args), std::move(receiver))));
  } else if (expr->type == Lambda::type) {
    Lambda *lambda = reinterpret_cast<Lambda*>(expr);
    auto closure = std::make_shared<Closure>(lambda->body.get(), binding);
    Receiver::receive(queue, std::move(receiver), std::move(closure));
  } else if (expr->type == Memoize::type) {
    Memoize *memoize = reinterpret_cast<Memoize*>(expr);
    Binding::hash(queue, binding, std::unique_ptr<Hasher>(new MemoizeHasher(std::move(receiver), binding, memoize)));
  } else if (expr->type == DefBinding::type) {
    DefBinding *defbinding = reinterpret_cast<DefBinding*>(expr);
    auto defs = std::make_shared<Binding>(binding, queue.stack_trace?binding:nullptr, defbinding, defbinding->val.size());
    int j = 0;
    for (auto &i : defbinding->val)
      queue.emplace(i.get(), binding, Binding::make_completer(defs, j++));
    queue.emplace(defbinding->body.get(), std::move(defs), std::move(receiver));
  } else if (expr->type == Literal::type) {
    Literal *lit = reinterpret_cast<Literal*>(expr);
    Receiver::receive(queue, std::move(receiver), lit->value);
  } else if (expr->type == Prim::type) {
    Prim *prim = reinterpret_cast<Prim*>(expr);
    // Cut the scope of primitive to only it's own arguments
    if (prim->args == 0) {
      binding.reset();
    } else {
      Binding *iter = binding.get();
      for (int i = 1; i < prim->args; ++i) iter = iter->next.get();
      iter->next.reset();
    }
    std::vector<std::shared_ptr<Value> > args;
    args.reserve(prim->args);
    chain_app(queue, std::move(receiver), prim, std::move(binding), std::move(args));
  } else {
    assert(0 /* unreachable */);
  }
}

void Receive::eval(WorkQueue &queue) {
  receiver->receive(queue, std::move(value));
}

void WorkQueue::run() {
  while (!receives.empty()) {
    receives.front().eval(*this);
    receives.pop();
  }
  while (!thunks.empty()) {
    thunks.front().eval(*this);
    thunks.pop();
    while (!receives.empty()) {
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
