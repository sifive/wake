#ifndef HEAP_H
#define HEAP_H

#include <memory>

struct Value;
struct ThunkQueue;
struct DefBinding;

struct Receiver {
  virtual ~Receiver();

  static void receiveM(ThunkQueue &queue, std::unique_ptr<Receiver> receiver, std::shared_ptr<Value> &&value) {
    receiver->receive(queue, std::move(value));
  }
  static void receiveC(ThunkQueue &queue, std::unique_ptr<Receiver> receiver, std::shared_ptr<Value> value) {
    receiver->receive(queue, std::move(value));
  }

protected:
  virtual void receive(ThunkQueue &queue, std::shared_ptr<Value> &&value) = 0;

private:
  std::unique_ptr<Receiver> next; // for wait Q
friend struct Future;
friend struct Completer;
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

private:
  std::shared_ptr<Value> value;
  std::unique_ptr<Receiver> waiting;
friend struct Completer;
};

struct Binding {
  std::shared_ptr<Binding> next;
  DefBinding *binding;
  int nargs;
  std::unique_ptr<Future[]> future;

  Binding(const std::shared_ptr<Binding> &next_, DefBinding *binding_, int nargs_) : next(next_), binding(binding_), nargs(nargs_), future(new Future[nargs]) { }
  static std::unique_ptr<Receiver> make_completer(const std::shared_ptr<Binding> &binding, int arg);
};

#endif
