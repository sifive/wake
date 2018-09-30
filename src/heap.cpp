#include "heap.h"
#include "location.h"
#include "value.h"
#include "expr.h"

Receiver::~Receiver() { }

struct Memoizer : public Receiver {
  Future *future;
  Memoizer(Future *future_) : future(future_) { }
  void receive(WorkQueue &queue, std::shared_ptr<Value> &&value);
};

void Memoizer::receive(WorkQueue &queue, std::shared_ptr<Value> &&value)
{
  future->value = std::move(value);
  std::unique_ptr<Receiver> iter, next;
  for (iter = std::move(future->waiting); iter; iter = std::move(next)) {
    next = std::move(iter->next);
    Receiver::receive(queue, std::move(iter), future->value);
  }
}

std::unique_ptr<Receiver> Future::make_completer() {
  return std::unique_ptr<Receiver>(new Memoizer(this));
}

struct Completer : public Receiver {
  std::shared_ptr<Future> future;
  void receive(WorkQueue &queue, std::shared_ptr<Value> &&value);
  Completer(std::shared_ptr<Future> &&future_) : future(future_) { }
};

void Completer::receive(WorkQueue &queue, std::shared_ptr<Value> &&value) {
  future->value = std::move(value);
  std::unique_ptr<Receiver> iter, next;
  for (iter = std::move(future->waiting); iter; iter = std::move(next)) {
    next = std::move(iter->next);
    Receiver::receive(queue, std::move(iter), future->value);
  }
}

std::unique_ptr<Receiver> Binding::make_completer(const std::shared_ptr<Binding> &binding, int arg) {
  return std::unique_ptr<Receiver>(new Completer(std::shared_ptr<Future>(binding, &binding->future[arg])));
}

std::vector<Location> Binding::stack_trace(const std::shared_ptr<Binding> &binding) {
  std::vector<Location> out;
  for (Binding *i = binding.get(); i; i = i->invoker.get())
    if (i->expr->type != DefBinding::type)
      out.emplace_back(i->expr->location);
  return out;
}

struct FutureReceiver : public Receiver {
  std::unique_ptr<Hasher> hasher;
  FutureReceiver(std::unique_ptr<Hasher> hasher_) : hasher(std::move(hasher_)) { }
  void receive(WorkQueue &queue, std::shared_ptr<Value> &&value);
};

void FutureReceiver::receive(WorkQueue &queue, std::shared_ptr<Value> &&value) {
  value->hash(queue, std::move(hasher));
}

void Future::hash(WorkQueue &queue, std::unique_ptr<Hasher> hasher) {
  if (value) {
    value->hash(queue, std::move(hasher));
  } else {
    std::unique_ptr<Receiver> wait(new FutureReceiver(std::move(hasher)));
    wait->next = std::move(waiting);
    waiting = std::move(wait);
  }
}

struct FutureHasher : public Hasher {
  std::shared_ptr<Binding> binding;
  std::vector<uint64_t> codes;
  int arg;
  FutureHasher(std::shared_ptr<Binding> &&binding_, std::vector<uint64_t> &&codes_, int arg_)
   : binding(std::move(binding_)), codes(std::move(codes_)), arg(arg_) { }
  void receive(WorkQueue &queue, Hash hash);
  static void chain(WorkQueue &queue, std::shared_ptr<Binding> &&binding, std::vector<uint64_t> &&codes, int arg);
};

void FutureHasher::receive(WorkQueue &queue, Hash hash) {
  hash.push(codes);
  chain(queue, std::move(binding), std::move(codes), arg+1);
}

void FutureHasher::chain(WorkQueue &queue, std::shared_ptr<Binding> &&binding, std::vector<uint64_t> &&codes, int arg) {
  if (arg == binding->nargs) {
    HASH(codes.data(), codes.size()*8, 42, binding->hashcode);
    std::unique_ptr<Hasher> iter, next;
    for (iter = std::move(binding->hasher); iter; iter = std::move(next)) {
      next = std::move(iter->next);
      Hasher::receive(queue, std::move(iter), binding->hashcode);
    }
    binding->hasher.reset();
  } else {
    Binding *bind = binding.get();
    bind->future[arg].hash(queue, std::unique_ptr<Hasher>(new FutureHasher(
      std::move(binding), std::move(codes), arg)));
  }
}

struct ParentHasher : public Hasher {
  std::shared_ptr<Binding> binding;
  ParentHasher(const std::shared_ptr<Binding> &binding_) : binding(binding_) { }
  void receive(WorkQueue &queue, Hash hash);
};

void ParentHasher::receive(WorkQueue &queue, Hash hash) {
  std::vector<uint64_t> codes;
  hash.push(codes);
  FutureHasher::chain(queue, std::move(binding), std::move(codes), 0);
}

Binding::Binding(const std::shared_ptr<Binding> &next_, const std::shared_ptr<Binding> &invoker_, Expr *expr_, int nargs_)
  : next(next_), invoker(invoker_), future(new Future[nargs_]), hasher(),
    expr(expr_), hashcode(), nargs(nargs_) { }

void Binding::hash(WorkQueue &queue, const std::shared_ptr<Binding> &binding, std::unique_ptr<Hasher> hasher) {
  if (!binding) {
    Hasher::receive(queue, std::move(hasher), Hash());
  } else if (binding->hashcode) {
    Hasher::receive(queue, std::move(hasher), binding->hashcode);
  } else {
    bool first = !binding->hasher;
    hasher->next = std::move(binding->hasher);
    binding->hasher = std::move(hasher);
    if (first) {
      if (binding->next) {
        Binding::hash(queue, binding->next, std::unique_ptr<Hasher>(new ParentHasher(binding)));
      } else {
        std::vector<uint64_t> codes;
        FutureHasher::chain(queue, std::shared_ptr<Binding>(binding), std::move(codes), 0);
      }
    }
  }
}
