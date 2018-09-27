#include "thunk.h"
#include "expr.h"
#include "value.h"
#include <cassert>
#include <iostream>

struct Application : public Receiver {
  std::shared_ptr<Binding> args;
  std::unique_ptr<Receiver> receiver;
  void receive(ThunkQueue &queue, std::shared_ptr<Value> &&value);
  Application(std::shared_ptr<Binding> &&args_, std::unique_ptr<Receiver> receiver_)
   : args(std::move(args_)), receiver(std::move(receiver_)) { }
};

void Application::receive(ThunkQueue &queue, std::shared_ptr<Value> &&value) {
  if (value->type == Exception::type) {
    Receiver::receiveM(queue, std::move(receiver), std::move(value));
  } else if (value->type != Closure::type) {
    auto exception = std::make_shared<Exception>("Attempt to apply " + value->to_str() + " which is not a Closure", args->invoker);
    Receiver::receiveM(queue, std::move(receiver), std::move(exception));
  } else {
    Closure *clo = reinterpret_cast<Closure*>(value.get());
    args->next = clo->binding;
    args->location = &clo->body->location;
    queue.queue.emplace(clo->body, std::move(args), std::move(receiver));
  }
}

struct Primitive : public Receiver {
  std::vector<std::shared_ptr<Value> > args;
  std::unique_ptr<Receiver> receiver;
  std::shared_ptr<Binding> binding;
  Prim *prim;
  void receive(ThunkQueue &queue, std::shared_ptr<Value> &&value);
  Primitive(std::vector<std::shared_ptr<Value> > &&args_, std::unique_ptr<Receiver> receiver_, std::shared_ptr<Binding> &&binding_, Prim *prim_)
   : args(std::move(args_)), receiver(std::move(receiver_)), binding(std::move(binding_)), prim(prim_) { }
};

static void chain_app(ThunkQueue &queue, std::unique_ptr<Receiver> receiver, Prim *prim, std::shared_ptr<Binding> &&binding, std::vector<std::shared_ptr<Value> > &&args) {
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

void Primitive::receive(ThunkQueue &queue, std::shared_ptr<Value> &&value) {
  args.emplace_back(std::move(value));
  chain_app(queue, std::move(receiver), prim, std::move(binding), std::move(args));
}

struct MemoizeHasher : public Hasher {
  std::unique_ptr<Receiver> receiver;
  std::shared_ptr<Binding> binding;
  Memoize *memoize;
  MemoizeHasher(std::unique_ptr<Receiver> receiver_, const std::shared_ptr<Binding> &binding_, Memoize *memoize_)
   : receiver(std::move(receiver_)), binding(binding_), memoize(memoize_) { }
  void receive(ThunkQueue &queue, Hash hash);
};

void MemoizeHasher::receive(ThunkQueue &queue, Hash hash)
{
  auto i = memoize->values.find(hash);
  if (i == memoize->values.end()) {
    Future &future = memoize->values[hash];
    future.depend(queue, std::move(receiver));
    queue.queue.emplace(memoize->body.get(), std::move(binding), future.make_completer());
  } else {
    i->second.depend(queue, std::move(receiver));
  }
}

void Thunk::eval(ThunkQueue &queue)
{
  if (expr->type == VarRef::type) {
    VarRef *ref = reinterpret_cast<VarRef*>(expr);
    std::shared_ptr<Binding> *iter = &binding;
    for (int depth = ref->depth; depth; --depth)
      iter = &(*iter)->next;
    int vals = (*iter)->nargs;
    if (ref->offset >= vals) {
      auto closure = std::make_shared<Closure>((*iter)->binding->fun[ref->offset-vals]->body.get(), *iter);
      Receiver::receiveM(queue, std::move(receiver), std::move(closure));
    } else {
      (*iter)->future[ref->offset].depend(queue, std::move(receiver));
    }
  } else if (expr->type == App::type) {
    App *app = reinterpret_cast<App*>(expr);
    auto args = std::make_shared<Binding>(nullptr, queue.stack_trace?binding:nullptr, nullptr, nullptr, 1);
    queue.queue.emplace(app->val.get(), binding, Binding::make_completer(args, 0));
    queue.queue.emplace(app->fn .get(), std::move(binding), std::unique_ptr<Receiver>(
      new Application(std::move(args), std::move(receiver))));
  } else if (expr->type == Lambda::type) {
    Lambda *lambda = reinterpret_cast<Lambda*>(expr);
    auto closure = std::make_shared<Closure>(lambda->body.get(), binding);
    Receiver::receiveM(queue, std::move(receiver), std::move(closure));
  } else if (expr->type == Memoize::type) {
    Memoize *memoize = reinterpret_cast<Memoize*>(expr);
    Binding::hash(queue, binding, std::unique_ptr<Hasher>(new MemoizeHasher(std::move(receiver), binding, memoize)));
  } else if (expr->type == DefBinding::type) {
    DefBinding *defbinding = reinterpret_cast<DefBinding*>(expr);
    auto defs = std::make_shared<Binding>(binding, queue.stack_trace?binding:nullptr, &defbinding->location, defbinding, defbinding->val.size());
    int j = 0;
    for (auto &i : defbinding->val)
      queue.queue.emplace(i.get(), binding, Binding::make_completer(defs, j++));
    queue.queue.emplace(defbinding->body.get(), std::move(defs), std::move(receiver));
  } else if (expr->type == Literal::type) {
    Literal *lit = reinterpret_cast<Literal*>(expr);
    Receiver::receiveC(queue, std::move(receiver), lit->value);
  } else if (expr->type == Prim::type) {
    Prim *prim = reinterpret_cast<Prim*>(expr);
    std::vector<std::shared_ptr<Value> > args;
    args.reserve(prim->args);
    chain_app(queue, std::move(receiver), prim, std::move(binding), std::move(args));
  } else {
    assert(0 /* unreachable */);
  }
}

void ThunkQueue::run() {
  while (!queue.empty()) {
    queue.front().eval(*this);
    queue.pop();
  }
}
