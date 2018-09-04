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
  Action *input_action;
  Value  *input_value;

  Callback(const char *type_, Action *invoker_) : Action(type_, invoker_) { }
};

struct Thunk : public Action {
  Expr *expr;
  Binding *bindings;

  static const char *type;
  Thunk(Action *invoker_ = 0, Expr *expr_ = 0, Binding *bindings_ = 0)
   : Action(type, invoker_), expr(expr_), bindings(bindings_), return_action(0), return_value(0) { }

  void execute(ActionQueue &queue);
  void depend(ActionQueue &queue, Callback *callback);
  void broadcast(ActionQueue &queue, Action *return_action_, Value *return_value_);

  // To be used only after execution complete
  Value *output() { return return_value; }

private:
  Action *return_action;
  Value  *return_value;
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

struct Prim;
struct PrimArg : public Callback {
  static void chain(ActionQueue &queue, Action *invoker, Prim *prim, Binding *binding, int arg);
  Prim *prim;
  Binding *binding;
  int arg;
  void execute(ActionQueue &queue);
  static const char *type;
  PrimArg(Action *invoker_, Prim *prim_, Binding *binding_, int arg_)
   : Callback(type, invoker_), prim(prim_), binding(binding_), arg(arg_) { }
};

struct PrimRet : public Callback {
  void execute(ActionQueue &queue);
  static const char *type;
  PrimRet(Action *invoker_) : Callback(type, invoker_) { }
};

struct MapRet : public Callback {
  void execute(ActionQueue &queue);
  static const char *type;
  MapRet(Action *invoker_) : Callback(type, invoker_) { }
};

#endif
