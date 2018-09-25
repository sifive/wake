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

  Thunk(Expr *expr_, std::shared_ptr<Binding>      &&binding_, std::unique_ptr<Receiver> receiver_) : expr(expr_), binding(binding_), receiver(std::move(receiver_)) { }
  Thunk(Expr *expr_, const std::shared_ptr<Binding> &binding_, std::unique_ptr<Receiver> receiver_) : expr(expr_), binding(binding_), receiver(std::move(receiver_)) { }

  void eval(ThunkQueue &queue);
};

struct ThunkQueue {
  bool stack_trace;
  std::queue<Thunk> queue;
  void run();
};

#endif
