#ifndef THUNK_H
#define THUNK_H

#include <queue>
#include <memory>

// We need ~Receiver
#include "heap.h"

struct Binding;
struct Expr;

struct Thunk {
  Expr *expr;
  std::shared_ptr<Binding> binding;
  std::unique_ptr<Receiver> receiver;

  Thunk(Expr *expr_, std::shared_ptr<Binding> &&binding_, std::unique_ptr<Receiver> receiver_)
   : expr(expr_), binding(std::move(binding_)), receiver(std::move(receiver_)) { }

  void eval(WorkQueue &queue);
};

struct Receive {
  std::unique_ptr<Receiver> receiver;
  std::shared_ptr<Value> value;

  Receive(std::unique_ptr<Receiver> receiver_, std::shared_ptr<Value> &&value_)
   : receiver(std::move(receiver_)), value(std::move(value_)) { }

  void eval(WorkQueue &queue);
};

struct WorkQueue {
  bool stack_trace;
  std::queue<Thunk> thunks;
  std::queue<Receive> receives;

  void run();

  void emplace(Expr *expr, std::shared_ptr<Binding> &&binding, std::unique_ptr<Receiver> receiver);
  void emplace(Expr *expr, const std::shared_ptr<Binding> &binding, std::unique_ptr<Receiver> receiver);
};

#endif
