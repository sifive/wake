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
  const char *type;
  Action *invoker;

  virtual ~Action();
  virtual void execute(ActionQueue &queue) = 0;
  Action(const char *type_, Action *invoker_) : type(type_), invoker(invoker_) { }
};

struct Callback : public Action {
  Action *input;
  Value *value;

  Callback(const char *type_, Action *invoker_) : Action(type_, invoker_) { }
};

struct Thunk : public Action {
  Expr *expr;
  Binding *bindings;

  static const char *type;
  Thunk(Action *invoker_, Expr *expr_, Binding *bindings_) : Action(type, invoker_), expr(expr_), bindings(bindings_), result(0), value(0) { }
  Thunk() : Action(type, 0), expr(0), bindings(0), result(0), value(0) { }

  void execute(ActionQueue &queue);
  void depend(ActionQueue& queue, Callback *callback);
  void broadcast(ActionQueue& queue, Action *result_, Value *value_);

  // To be used only after execution complete
  Value *output() { return value; }

private:
  Action *result;
  Value *value;
  std::list<Callback*> wait;
};

struct VarRet : public Callback {
  void execute(ActionQueue &queue);
  static const char *type;
  VarRet(Action *invoker_) : Callback(type, invoker_) { }
};

struct AppRet : public Callback {
  void execute(ActionQueue &queue);
  static const char *type;
  AppRet(Action *invoker_) : Callback(type, invoker_) { }
};

struct AppFn : public Callback {
  Thunk *arg;
  void execute(ActionQueue &queue);
  static const char *type;
  AppFn(Action *invoker_, Thunk *arg_) : Callback(type, invoker_), arg(arg_) { }
};

struct MapRet : public Callback {
  void execute(ActionQueue &queue);
  static const char *type;
  MapRet(Action *invoker_) : Callback(type, invoker_) { }
};

#endif
