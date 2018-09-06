#include "action.h"
#include "stack.h"
#include "expr.h"
#include "value.h"
#include "prim.h"
#include <iostream>
#include <cassert>

Action::~Action() { }

const char *Eval   ::type = "Eval";
const char *PrimArg::type = "PrimArg";
const char *PrimRet::type = "PrimFn";
const char *VarRet ::type = "VarRet";
const char *AppRet ::type = "AppRet";
const char *AppFn  ::type = "AppFn";
const char *MapRet ::type = "MapRet";
const char *TopRet ::type = "TopRet";

static uint64_t serial_gen = 0;

void Future::depend(ActionQueue &queue, Callback *callback) {
  if (value) {
    queue.push(std::unique_ptr<Callback>(callback));
  } else {
    callback->next = std::move(waiting);
    waiting = std::unique_ptr<Callback>(callback);
  }
}

void Future::complete(ActionQueue &queue, std::shared_ptr<Value> &&value_, uint64_t action_serial_) {
  value = std::move(value_);
  action_serial = action_serial_;

  std::unique_ptr<Callback> cb, next;
  for (cb = std::move(waiting); cb; cb = std::move(next)) {
    next = std::unique_ptr<Callback>(reinterpret_cast<Callback*>(cb->next.release()));
    queue.push(std::move(cb));
  }
}

void Future::complete(ActionQueue &queue, Value *value_, uint64_t action_serial_) {
  return complete(queue, std::shared_ptr<Value>(value), action_serial_);
}

void Future::complete(ActionQueue &queue, const std::shared_ptr<Value> &value_, uint64_t action_serial_) {
  return complete(queue, std::shared_ptr<Value>(value), action_serial_);
}

Action::Action(const char *type_, Action *invoker, std::shared_ptr<Future> &&future_result_)
 : serial(++serial_gen), invoker_serial(invoker->serial), stack(invoker->stack), future_result(std::move(future_result_)) { }

Action::Action(const char *type_, Action *invoker, Future *future_result_, const Location &location)
 : serial(++serial_gen), invoker_serial(invoker->serial), stack(Stack::grow(invoker->stack, location)), future_result(future_result_) { }

Eval::Eval(Action *invoker, Expr *expr_, const std::shared_ptr<Binding> &bindings_)
 : Action(type, invoker, new Future, expr_->location), expr(expr_), bindings(bindings_) { }

Eval::Eval(Action *invoker, Expr *expr_, Binding *bindings_)
 : Action(type, invoker, new Future, expr_->location), expr(expr_), bindings(bindings_) { }

Callback::Callback(const char *type_, Action *invoker, const std::shared_ptr<Future> &future_input_)
 : Action(type_, invoker, std::move(invoker->future_result)), future_input(future_input_) { }

AppFn::AppFn(Action *invoker, const std::shared_ptr<Future> &future_input_, const std::shared_ptr<Future> &arg_)
 : Callback(type, invoker, future_input_), arg(arg_) { }

Return::Return(const char *type_, Action *invoker, const std::shared_ptr<Future> &future_input_)
 : Callback(type_, invoker, future_input_) { }

void Return::execute(ActionQueue &queue) {
  future_result->complete(queue, future_input->get_value(), serial);
}

static void hook(ActionQueue &queue, Callback *cb) {
  cb->future_input->depend(queue, cb);
}

void AppFn::execute(ActionQueue &queue) {
  Value *value = future_input->get_raw_value();
  if (value->type != Closure::type) {
    std::cerr << "Attempt to apply " << value << " which is not a Closure" << std::endl;
    exit(1);
  }
  Closure *clo = reinterpret_cast<Closure*>(value);
  Eval *eval = new Eval(this, clo->body, new Binding(clo->bindings, std::move(arg)));
  eval->future_result->depend(queue, new AppRet(this, eval->future_result));
  queue.push(std::unique_ptr<Action>(eval));
}

void PrimArg::chain(
    ActionQueue &queue,
    Action *invoker,
    Prim *prim,
    const std::shared_ptr<Binding> &binding,
    std::vector<std::shared_ptr<Value> > &&values) {
  if (prim->args == values.size()) {
    std::vector<Value*> args;
    for (auto &i : values) args.push_back(i.get());
    prim->fn(prim->data, args, new PrimRet(invoker));
  } else {
    hook(queue, new PrimArg(invoker, prim, binding, std::move(values), binding->future[0]));
  }
}

void PrimArg::execute(ActionQueue &queue) {
  values.push_back(future_input->get_value());
  chain(queue, this, prim, binding->next, std::move(values));
}

static Binding *bind_defmap(ActionQueue &queue, Action *invoker, DefMap *def, const std::shared_ptr<Binding> &bindings) {
  Binding *defs = new Binding(bindings);
  for (auto &i : def->map) {
    Eval *eval = new Eval(invoker, i.second.get(), defs);
    queue.push(eval);
    defs->future.push_back(eval->future_result);
  }
  return defs;
}

void Eval::execute(ActionQueue &queue) {
  if (expr->type == VarRef::type) {
    VarRef *ref = reinterpret_cast<VarRef*>(expr);
    Binding *iter = bindings.get();
    for (int depth = ref->depth; depth; --depth)
      iter = iter->next.get();
    hook(queue, new VarRet(this, iter->future[ref->offset]));
  } else if (expr->type == App::type) {
    App *app = reinterpret_cast<App*>(expr);
    Eval *fn  = new Eval(this, app->fn.get(), bindings);
    Eval *arg = new Eval(this, app->val.get(), bindings);
    queue.push(fn);
    queue.push(arg);
    hook(queue, new AppFn(this, fn->future_result, arg->future_result));
  } else if (expr->type == Lambda::type) {
    Lambda *lambda = reinterpret_cast<Lambda*>(expr);
    Closure *closure = new Closure(lambda->body.get(), bindings);
    future_result->complete(queue, closure, serial);
  } else if (expr->type == DefMap::type) {
    DefMap *defmap = reinterpret_cast<DefMap*>(expr);
    Binding *defs = bind_defmap(queue, this, defmap, bindings);
    Eval *body = new Eval(this, defmap->body.get(), defs);
    queue.push(body);
    hook(queue, new MapRet(this, body->future_result));
  } else if (expr->type == Literal::type) {
    Literal *lit = reinterpret_cast<Literal*>(expr);
    future_result->complete(queue, lit->value, serial);
  } else if (expr->type == Prim::type) {
    Prim *prim = reinterpret_cast<Prim*>(expr);
    std::vector<std::shared_ptr<Value> > values;
    PrimArg::chain(queue, this, prim, bindings, std::move(values));
  } else if (expr->type == Top::type) {
    Top *top = reinterpret_cast<Top*>(expr);
    std::shared_ptr<Binding> global(new Binding(bindings));
    std::vector<Binding*> defmaps;
    for (auto &i : top->defmaps)
      defmaps.push_back(bind_defmap(queue, this, &i, global));
    for (auto &i : top->globals) {
      Eval *eval = new Eval(this, i.second.var.get(), defmaps[i.second.defmap]);
      queue.push(eval);
      global->future.push_back(eval->future_result);
    }
    Eval *main = new Eval(this, top->main.get(), global);
    queue.push(main);
    hook(queue, new TopRet(this, main->future_result));
  } else {
    assert(0 /* unreachable */);
  }
}

void ActionQueue::push(std::unique_ptr<Action> &&action) {
  if (bottom) {
    bottom->next = std::move(action);
    bottom = bottom->next.get();
  } else {
    top = std::move(action);
    bottom = top.get();
  }
  bottom->next.reset();
}

void ActionQueue::push(Action *action) {
  return push(std::unique_ptr<Action>(action));
}
