#ifndef ACTION_H
#define ACTION_H

#include <list>

/* Evaluation */

struct Action;
struct Value;
struct Expr;
struct Binding;

typedef std::list<Action*> ActionQueue;

struct Action {
  Action *invoker;
  virtual ~Action();
  virtual void execute(ActionQueue &queue) = 0;
  Action(Action *invoker_) : invoker(invoker_) { }
};

struct Callback : public Action {
  Action *input;
  Value *value;

  Callback(Action *invoker_) : Action(invoker_) { }
};

struct Thunk : public Action {
  Expr *expr;
  Binding *bindings;

  Thunk(Action *invoker_, Expr *expr_, Binding *bindings_) : Action(invoker_), expr(expr_), bindings(bindings_), result(0), value(0) { }
  Thunk() : Action(0), expr(0), bindings(0) { }

  void execute(ActionQueue &queue);
  void depend(ActionQueue& queue, Callback *callback);
  void broadcast(ActionQueue& queue, Action *result_, Value *value_);

private:
  Action *result;
  Value *value;
  std::list<Callback*> wait;
};

struct VarRet : public Callback {
  void execute(ActionQueue &queue);
  VarRet(Action *invoker_) : Callback(invoker_) { }
};

struct AppRet : public Callback {
  void execute(ActionQueue &queue);
  AppRet(Action *invoker_) : Callback(invoker_) { }
};

struct AppFn : public Callback {
  Thunk *arg;
  void execute(ActionQueue &queue);
  AppFn(Action *invoker_, Thunk *arg_) : Callback(invoker_), arg(arg_) { }
};

struct MapRet : public Callback {
  void execute(ActionQueue &queue);
  MapRet(Action *invoker_) : Callback(invoker_) { }
};

#endif
