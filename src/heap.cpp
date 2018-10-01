#include "heap.h"
#include "location.h"
#include "value.h"
#include "expr.h"

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
    if (i->expr->type != DefBinding::type)
      out.emplace_back(i->expr->location);
  return out;
}

void Binding::wait(WorkQueue &queue, std::unique_ptr<Finisher> finisher) {
  Binding *iter;
  for (iter = this; iter; iter = iter->next.get())
    if (iter->state != iter->nargs) break;

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
  if (hashcode) return hashcode;
  std::vector<uint64_t> codes;
  if (next) next->hash().push(codes);
  for (int arg = 0; arg < nargs; ++arg)
    future[arg].value->hash().push(codes);
  HASH(codes.data(), 8*codes.size(), 42, hashcode);
  return hashcode;
}

void Binding::future_completed(WorkQueue &queue) {
  --state;
  if (state == -nargs && finisher)
    future_finished(queue);
}

void Binding::future_finished(WorkQueue &queue) {
  if (state < 0) state = 0;

  while (state < nargs &&
    (future[state].value->type != Closure::type ||
     !reinterpret_cast<Closure*>(future[state].value.get())->binding)) {
    ++state;
  }

  if (state == nargs) {
    std::unique_ptr<Finisher> iter, iter_next;
    for (iter = std::move(finisher); iter; iter = std::move(iter_next)) {
      iter_next = std::move(iter->next);
      next->wait(queue, std::move(iter));
    }
  } else {
    Closure *closure = reinterpret_cast<Closure*>(future[state].value.get());
    closure->binding->wait(queue, std::unique_ptr<Finisher>(new ChildFinisher(this)));
  }
}
