#include "action.h"
#include "stack.h"
#include "expr.h"
#include "value.h"
#include "prim.h"
#include <algorithm>
#include <cassert>

Action::~Action() { }

const char *Eval   ::type = "Eval";
const char *PrimArg::type = "PrimArg";
const char *PrimRet::type = "PrimFn";
const char *VarRet ::type = "VarRet";
const char *AppRet ::type = "AppRet";
const char *AppFn  ::type = "AppFn";
const char *DefRet ::type = "DefRet";

uint64_t Action::next_serial = 0;

void Future::depend(ActionQueue &queue, std::unique_ptr<Callback> &&callback) {
  if (value) {
    queue.push(std::move(callback));
  } else {
    callback->next = std::move(waiting);
    waiting = std::move(callback);
  }
}

void Future::complete(ActionQueue &queue, std::shared_ptr<Value> &&value_, uint64_t action_serial_) {
  value = std::move(value_);
  action_serial = action_serial_;

  assert (value);

  std::unique_ptr<Action> cb, next;
  for (cb = std::move(waiting); cb; cb = std::move(next)) {
    next = std::move(cb->next);
    queue.push(std::move(cb));
  }
}

void Future::complete(ActionQueue &queue, const std::shared_ptr<Value> &value_, uint64_t action_serial_) {
  return complete(queue, std::shared_ptr<Value>(value_), action_serial_);
}

Action::Action(const char *type_, Action *invoker, std::shared_ptr<Future> &&future_result_)
 : type(type_), serial(++next_serial), invoker_serial(invoker->serial), stack(invoker->stack), future_result(std::move(future_result_)) { }

Action::Action(const char *type_, Action *invoker, std::shared_ptr<Future> &&future_result_, const Location &location)
 : type(type_), serial(++next_serial), invoker_serial(invoker->serial), stack(Stack::grow(invoker->stack, location)), future_result(future_result_) { }

Action::Action(const char *type_, std::shared_ptr<Future> &&future_result_, const Location &location)
 : type(type_), serial(++next_serial), invoker_serial(0), stack(new Stack(location)), future_result(future_result_) { }

Eval::Eval(Action *invoker, Expr *expr_, const std::shared_ptr<Binding> &bindings_)
 : Action(type, invoker, std::shared_ptr<Future>(new Future), expr_->location), expr(expr_), bindings(bindings_) { }

Eval::Eval(Action *invoker, Expr *expr_, std::shared_ptr<Binding> &&bindings_)
 : Action(type, invoker, std::shared_ptr<Future>(new Future), expr_->location), expr(expr_), bindings(bindings_) { }

Eval::Eval(Expr *expr_)
 : Action(type, std::shared_ptr<Future>(new Future), expr_->location), expr(expr_), bindings() { }

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
  cb->future_input->depend(queue, std::unique_ptr<Callback>(cb));
}

void AppFn::execute(ActionQueue &queue) {
  Value *value = future_input->get_raw_value();
  if (value->type == Exception::type) {
    future_result->complete(queue, future_input->get_value(), serial);
  } else if (value->type != Closure::type) {
    std::shared_ptr<Value> exception(new Exception("Attempt to apply " + value->to_str() + " which is not a Closure"));
    future_result->complete(queue, std::move(exception), serial);
  } else {
    Closure *clo = reinterpret_cast<Closure*>(value);
    std::unique_ptr<Eval> eval(new Eval(this, clo->body, std::shared_ptr<Binding>(new Binding(clo->bindings, std::move(arg)))));
    hook(queue, new AppRet(this, eval->future_result));
    queue.push(std::move(eval));
  }
}

void PrimArg::chain(
    ActionQueue &queue,
    Action *invoker,
    Prim *prim,
    const std::shared_ptr<Binding> &binding,
    std::vector<std::shared_ptr<Value> > &&values) {
  if ((size_t)prim->args == values.size()) {
    std::reverse(values.begin(), values.end());
    prim->fn(prim->data, std::move(values), std::unique_ptr<Action>(new PrimRet(invoker)));
  } else {
    hook(queue, new PrimArg(invoker, prim, binding, std::move(values), binding->future[0]));
  }
}

void PrimArg::execute(ActionQueue &queue) {
  values.push_back(future_input->get_value());
  chain(queue, this, prim, binding->next, std::move(values));
}

void Eval::execute(ActionQueue &queue) {
  if (expr->type == VarRef::type) {
    VarRef *ref = reinterpret_cast<VarRef*>(expr);
    std::shared_ptr<Binding> *iter = &bindings;
    for (int depth = ref->depth; depth; --depth)
      iter = &(*iter)->next;
    int vals = (*iter)->future.size();
    if (ref->offset >= vals) {
      std::shared_ptr<Value> closure(new Closure((*iter)->binding->fun[ref->offset-vals]->body.get(), *iter));
      future_result->complete(queue, std::move(closure), serial);
    } else {
      hook(queue, new VarRet(this, (*iter)->future[ref->offset]));
    }
  } else if (expr->type == App::type) {
    App *app = reinterpret_cast<App*>(expr);
    std::unique_ptr<Eval> fn (new Eval(this, app->fn.get(), bindings));
    std::unique_ptr<Eval> arg(new Eval(this, app->val.get(), bindings));
    hook(queue, new AppFn(this, fn->future_result, arg->future_result));
    queue.push(std::move(fn));
    queue.push(std::move(arg));
  } else if (expr->type == Lambda::type) {
    Lambda *lambda = reinterpret_cast<Lambda*>(expr);
    std::shared_ptr<Value> closure(new Closure(lambda->body.get(), bindings));
    future_result->complete(queue, std::move(closure), serial);
  } else if (expr->type == DefBinding::type) {
    DefBinding *defbinding = reinterpret_cast<DefBinding*>(expr);
    std::shared_ptr<Binding> defs(new Binding(bindings, defbinding));
    for (auto &i : defbinding->val) {
      std::unique_ptr<Eval> eval(new Eval(this, i.get(), bindings));
      defs->future.push_back(eval->future_result);
      queue.push(std::move(eval));
    }
    std::unique_ptr<Eval> body(new Eval(this, defbinding->body.get(), std::move(defs)));
    hook(queue, new DefRet(this, body->future_result));
    queue.push(std::move(body));
  } else if (expr->type == Literal::type) {
    Literal *lit = reinterpret_cast<Literal*>(expr);
    future_result->complete(queue, lit->value, serial);
  } else if (expr->type == Prim::type) {
    Prim *prim = reinterpret_cast<Prim*>(expr);
    std::vector<std::shared_ptr<Value> > values;
    PrimArg::chain(queue, this, prim, bindings, std::move(values));
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
