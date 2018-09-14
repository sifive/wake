#ifndef HEAP_H
#define HEAP_H

#include <memory>
#include <vector>

struct Value;
struct ThunkQueue;
struct DefBinding;
struct Location;

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
  std::shared_ptr<Binding> invoker; // !!! want weak, but contracting
  std::unique_ptr<Future[]> future;
  Location *location;
  DefBinding *binding;
  int nargs;

  Binding(const std::shared_ptr<Binding> &next_, const std::shared_ptr<Binding> &invoker_, Location *location_, DefBinding *binding_, int nargs_)
    : next(next_), invoker(invoker_), future(new Future[nargs_]), location(location_), binding(binding_), nargs(nargs_) { }

  static std::unique_ptr<Receiver> make_completer(const std::shared_ptr<Binding> &binding, int arg);
  static std::vector<Location> stack_trace(const std::shared_ptr<Binding> &binding);
};

#endif
