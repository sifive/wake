#ifndef ACTION_H
#define ACTION_H

#include <cstdint>
#include <vector>
#include <memory>

/* Evaluation */

struct Stack;
struct Callback;
struct Location;
struct ActionQueue;
struct Value;
struct Expr;
struct Binding;
struct Future;
struct Prim;

struct Future {
  void depend(ActionQueue &queue, std::unique_ptr<Callback> &&callback);
  void complete(ActionQueue &queue, std::shared_ptr<Value> &&value_, uint64_t action_serial_);
  void complete(ActionQueue &queue, const std::shared_ptr<Value> &value_, uint64_t action_serial_);

  // You may only invoke this after you were 'wake'd
  std::shared_ptr<Value> get_value() const { return value; }
  Value *get_raw_value() const { return value.get(); }
  uint64_t get_serial() const { return action_serial; }

  Future() { }
  Future(std::shared_ptr<Value>&& value_) : value(value_) { }
  Future(const std::shared_ptr<Value>& value_) : value(value_) { }

private:
  std::shared_ptr<Value> value;
  std::unique_ptr<Callback> waiting;
  uint64_t action_serial; // who supplied this value
};

struct Action {
  static uint64_t next_serial;
  const char *type;
  uint64_t serial; // database key
  uint64_t invoker_serial;
  std::shared_ptr<Stack>  stack;
  std::shared_ptr<Future> future_result;
  std::unique_ptr<Action> next; // for building lists of Actions

  virtual ~Action();
  Action(const char *type_, Action *invoker, std::shared_ptr<Future> &&future_result_);
  Action(const char *type_, Action *invoker, std::shared_ptr<Future> &&future_result_, const Location &location);
  Action(const char *type_, std::shared_ptr<Future> &&future_result_, const Location &location);

  virtual void execute(ActionQueue &queue) = 0;
};

struct Eval : public Action {
  Expr *expr;
  std::shared_ptr<Binding> bindings;

  static const char *type;
  Eval(Action *invoker, Expr *expr_, const std::shared_ptr<Binding> &bindings_);
  Eval(Action *invoker, Expr *expr_, std::shared_ptr<Binding> &&bindings_);
  Eval(Expr *expr_);

  void execute(ActionQueue &queue);
};

struct Callback : public Action {
  std::shared_ptr<Future> future_input;
  Callback(const char *type_, Action *invoker, const std::shared_ptr<Future> &future_input_);
};

struct AppFn : public Callback {
  std::shared_ptr<Future> arg;
  void execute(ActionQueue &queue);
  static const char *type;
  AppFn(Action *invoker, const std::shared_ptr<Future> &future_input_, const std::shared_ptr<Future> &arg_);
};

struct Return : public Callback {
  void execute(ActionQueue &queue);
  Return(const char *type_, Action *invoker, const std::shared_ptr<Future> &future_input_);
};

struct AppRet : public Return {
  static const char *type;
  AppRet(Action *invoker, const std::shared_ptr<Future> &future_input_) : Return(type, invoker, future_input_) { }
};

struct VarRet : public Return {
  static const char *type;
  VarRet(Action *invoker, const std::shared_ptr<Future> &future_input_) : Return(type, invoker, future_input_) { }
};

struct PrimArg : public Callback {
  static void chain(ActionQueue &queue, Action *invoker, Prim *prim, const std::shared_ptr<Binding> &binding,
    std::vector<std::shared_ptr<Value> > &&values);
  Prim *prim;
  std::shared_ptr<Binding> binding;
  std::vector<std::shared_ptr<Value> > values;
  void execute(ActionQueue &queue);

  static const char *type;
  PrimArg(Action *invoker, Prim *prim_, const std::shared_ptr<Binding> &binding_,
    std::vector<std::shared_ptr<Value> > &&values_, const std::shared_ptr<Future> &future_input_)
   : Callback(type, invoker, future_input_), prim(prim_), binding(binding_), values(std::move(values_)) { }
};

struct PrimRet : public Return {
  static const char *type;
  PrimRet(Action *invoker) : Return(type, invoker, std::shared_ptr<Future>(new Future)) { }
};

struct DefRet : public Return {
  static const char *type;
  DefRet(Action *invoker, const std::shared_ptr<Future> &future_input_) : Return(type, invoker, future_input_) { }
};

struct ActionQueue {
  std::unique_ptr<Action> top;
  Action *bottom;

  bool empty() const { return !top; }
  void push(std::unique_ptr<Action> &&action);
  std::unique_ptr<Action> pop() {
    std::unique_ptr<Action> out = std::move(top);
    if (out->next) {
      top = std::move(out->next);
    } else {
      bottom = 0;
    }
    return out;
  }

  ActionQueue() : top(), bottom(0) { }
};

#endif
