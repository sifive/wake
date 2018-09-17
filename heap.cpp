#include "heap.h"
#include "location.h"
#include "value.h"
#include "MurmurHash3.h"

Receiver::~Receiver() { }

struct Completer : public Receiver {
  std::shared_ptr<Future> future;
  void receive(ThunkQueue &queue, std::shared_ptr<Value> &&value);
  Completer(std::shared_ptr<Future> &&future_) : future(future_) { }
};

void Completer::receive(ThunkQueue &queue, std::shared_ptr<Value> &&value) {
  future->value = std::move(value);
  std::unique_ptr<Receiver> iter, next;
  for (iter = std::move(future->waiting); iter; iter = std::move(next)) {
    next = std::move(iter->next);
    Receiver::receiveC(queue, std::move(iter), future->value);
  }
}

std::unique_ptr<Receiver> Binding::make_completer(const std::shared_ptr<Binding> &binding, int arg) {
  return std::unique_ptr<Receiver>(new Completer(std::shared_ptr<Future>(binding, &binding->future[arg])));
}

std::vector<Location> Binding::stack_trace(const std::shared_ptr<Binding> &binding) {
  std::vector<Location> out;
  for (Binding *i = binding.get(); i; i = i->invoker.get())
    if (!i->binding)
      out.emplace_back(*i->location);
  return out;
}

struct FutureReceiver : public Receiver {
  std::unique_ptr<Hasher> hasher;
  FutureReceiver(std::unique_ptr<Hasher> hasher_) : hasher(std::move(hasher_)) { }
  void receive(ThunkQueue &queue, std::shared_ptr<Value> &&value) {
    value->hash(std::move(hasher));
  }
};

void Future::hash(std::unique_ptr<Hasher> hasher) {
  if (value) {
    value->hash(std::move(hasher));
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
  void receive(uint64_t hash[2]) {
    codes.push_back(hash[0]);
    codes.push_back(hash[1]);
    chain(std::move(binding), std::move(codes), arg+1);
  }
  static void chain(std::shared_ptr<Binding> &&binding, std::vector<uint64_t> &&codes, int arg) {
    if (arg == binding->nargs) {
      MurmurHash3_x64_128(codes.data(), codes.size()*8, 42, binding->hashcode);
      std::unique_ptr<Hasher> iter, next;
      for (iter = std::move(binding->hasher); iter; iter = std::move(next)) {
        next = std::move(iter->next);
        iter->receive(binding->hashcode);
      }
      binding->hasher.reset();
    } else {
      binding->future[arg].hash(std::unique_ptr<Hasher>(new FutureHasher(
        std::move(binding), std::move(codes), arg)));
    }
  }
};

struct ParentHasher : public Hasher {
  std::shared_ptr<Binding> binding;
  ParentHasher(const std::shared_ptr<Binding> &binding_) : binding(binding_) { }
  void receive(uint64_t hash[2]) {
    std::vector<uint64_t> codes;
    codes.push_back(hash[0]);
    codes.push_back(hash[1]);
    FutureHasher::chain(std::move(binding), std::move(codes), 0);
  }
};

void Binding::hash(const std::shared_ptr<Binding> &binding, std::unique_ptr<Hasher> hasher) {
  if (binding->hashcode[0] || binding->hashcode[1]) {
    hasher->receive(binding->hashcode);
  } else {
    if (!binding->hasher) {
      if (binding->next) {
        Binding::hash(binding->next, std::unique_ptr<Hasher>(new ParentHasher(binding)));
      } else {
        std::vector<uint64_t> codes;
        FutureHasher::chain(std::shared_ptr<Binding>(binding), std::move(codes), 0);
      }
    }
    hasher->next = std::move(binding->hasher);
    binding->hasher = std::move(hasher);
  }
}
