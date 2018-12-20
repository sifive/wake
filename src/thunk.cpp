#include "thunk.h"
#include "expr.h"
#include "value.h"
#include "datatype.h"
#include <cassert>

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
    args->expr = clo->lambda;
    queue.emplace(clo->lambda->body.get(), std::move(args), std::move(receiver));
  }
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
  if (value->type == Exception::type) {
    Receiver::receive(queue, std::move(receiver), std::move(value));
  } else if (value->type != Data::type) {
    auto exception = std::make_shared<Exception>("Attempt to destructure " + value->to_str() + " which is not a Data", args->invoker);
    Receiver::receive(queue, std::move(receiver), std::move(exception));
  } else {
    Data *data = reinterpret_cast<Data*>(value.get());
    if (data->cons->sum != des->sum){
      auto exception = std::make_shared<Exception>("Attempt to destructure " + value->to_str() + " which is not a " + des->sum->name, args->invoker);
      Receiver::receive(queue, std::move(receiver), std::move(exception));
    } else {
      // Create a binding to hold 'data' and 'fn'
      auto flip = std::make_shared<Binding>(data->binding, queue.stack_trace?args:nullptr, des, 2);
      flip->future[1].value = std::move(value);
      // Find the correct handler function
      Binding *fn = args.get();
      for (int i = 0; i <= data->cons->index; ++i) fn = fn->next.get();
      fn->future[0].depend(queue, Binding::make_completer(flip, 0));
      // Invoke the chain expr to setup user function
      queue.emplace(data->cons->expr.get(), std::move(flip), std::move(receiver));
    }
  }
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

struct Hasher : public Finisher {
  std::unique_ptr<Receiver> receiver;
  std::shared_ptr<Binding> binding;
  Memoize *memoize;
  Hasher(std::unique_ptr<Receiver> receiver_, std::shared_ptr<Binding> &&binding_, Memoize *memoize_)
   : receiver(std::move(receiver_)), binding(std::move(binding_)), memoize(memoize_) { }
  void finish(WorkQueue &queue);
};

void Hasher::finish(WorkQueue &queue)
{
  Hash hash = binding->hash();
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
      auto closure = std::make_shared<Closure>(defs->fun[ref->offset-vals].get(), *iter);
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
    auto closure = std::make_shared<Closure>(lambda, binding);
    Receiver::receive(queue, std::move(receiver), std::move(closure));
  } else if (expr->type == Memoize::type) {
    Memoize *memoize = reinterpret_cast<Memoize*>(expr);
    Binding *held = binding.get();
    Binding::wait(held, queue, std::unique_ptr<Finisher>(
      new Hasher(std::move(receiver), std::move(binding), memoize)));
  } else if (expr->type == DefBinding::type) {
    DefBinding *defbinding = reinterpret_cast<DefBinding*>(expr);
    auto defs = std::make_shared<Binding>(binding, queue.stack_trace?binding:nullptr, defbinding, defbinding->val.size());
    int j = 0;
    for (auto &i : defbinding->val)
      queue.emplace(i.get(), binding, Binding::make_completer(defs, j++));
    queue.emplace(defbinding->body.get(), std::move(defs), std::move(receiver));
  } else if (expr->type == Construct::type) {
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
  } else if (expr->type == Destruct::type) {
    Destruct *des = reinterpret_cast<Destruct*>(expr);
    binding->future[0].depend(queue, std::unique_ptr<Receiver>(
      new Destructure(std::move(binding), std::move(receiver), des)));
  } else if (expr->type == Literal::type) {
    Literal *lit = reinterpret_cast<Literal*>(expr);
    Receiver::receive(queue, std::move(receiver), lit->value);
  } else if (expr->type == Prim::type) {
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
      Binding::wait(held, queue, std::unique_ptr<Finisher>(
        new Primitive(std::move(receiver), std::move(binding), prim)));
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

void Finisher::finish(WorkQueue &queue, std::unique_ptr<Finisher> finisher) {
  queue.receives.emplace(std::move(finisher), nullptr);
}
