#ifndef HEAP_H
#define HEAP_H

#include "hash.h"
#include <memory>
#include <vector>

struct Value;
struct ThunkQueue;
struct Expr;
struct Location;
struct Hasher;

struct Receiver {
  std::unique_ptr<Receiver> next; // for wait Q
  virtual ~Receiver();

  static void receiveM(ThunkQueue &queue, std::unique_ptr<Receiver> receiver, std::shared_ptr<Value> &&value) {
    receiver->receive(queue, std::move(value));
  }
  static void receiveC(ThunkQueue &queue, std::unique_ptr<Receiver> receiver, std::shared_ptr<Value> value) {
    receiver->receive(queue, std::move(value));
  }

protected:
  virtual void receive(ThunkQueue &queue, std::shared_ptr<Value> &&value) = 0;
};

struct Future {
  Future() { }

  void depend(ThunkQueue &queue, std::unique_ptr<Receiver> receiver) {
    if (value) {
      Receiver::receiveC(queue, std::move(receiver), value);
    } else {
      receiver->next = std::move(waiting);
      waiting = std::move(receiver);
    }
  }

  // use only if nothing has depended on this yet
  void assign(const std::shared_ptr<Value> &value_) {
    value = std::move(value_);
  }

  // use only after evaluation has completed
  std::shared_ptr<Value> output() { return value; }

  // Only for use by memoization:
  void hash(ThunkQueue &queue, std::unique_ptr<Hasher> hasher);
  std::unique_ptr<Receiver> make_completer();

private:
  std::shared_ptr<Value> value;
  std::unique_ptr<Receiver> waiting;
friend struct Completer;
friend struct Memoizer;
};

struct Binding {
  std::shared_ptr<Binding> next;
  std::shared_ptr<Binding> invoker;
  std::unique_ptr<Future[]> future;
  std::unique_ptr<Hasher> hasher;
  Expr *expr;
  Hash hashcode;
  int nargs;

  Binding(const std::shared_ptr<Binding> &next_, const std::shared_ptr<Binding> &invoker_, Expr *expr_, int nargs_);

  static std::unique_ptr<Receiver> make_completer(const std::shared_ptr<Binding> &binding, int arg);
  static std::vector<Location> stack_trace(const std::shared_ptr<Binding> &binding);
  static void hash(ThunkQueue &queue, const std::shared_ptr<Binding> &binding, std::unique_ptr<Hasher> hasher_);
};

#endif
