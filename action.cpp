#include "expr.h"
#include "value.h"
#include "action.h"

Action::~Action() { }

void Thunk::depend(ActionQueue& queue, Callback *callback) {
  if (result) {
    callback->input = result;
    callback->value = value;
    queue.push_back(callback);
  } else {
    wait.push_back(callback);
  }
}

void Thunk::broadcast(ActionQueue& queue, Action *result_, Value *value_) {
  result = result_;
  value = value_;
  while (!wait.empty()) {
    Callback *callback = wait.front();
    wait.pop_front();
    callback->input = result;
    callback->value = value;
    queue.push_back(callback);
  }
}

void VarRet::execute(ActionQueue &queue) {
  Thunk *thunk = reinterpret_cast<Thunk*>(invoker);
  thunk->broadcast(queue, this, value);
}

void AppRet::execute(ActionQueue &queue) {
  AppFn *appFn = reinterpret_cast<AppFn*>(invoker);
  Thunk *thunk = reinterpret_cast<Thunk*>(appFn->invoker);
  thunk->broadcast(queue, this, value);
}

void AppFn::execute(ActionQueue &queue) {
  assert (value->type == Closure::type); // bad program
  Closure *clo = reinterpret_cast<Closure*>(input);
  Thunk *thunk = new Thunk(this, clo->body, new Binding(arg, clo->bindings));
  queue.push_back(thunk);
  thunk->depend(queue, new AppRet(this));
}

void Thunk::execute(ActionQueue& queue) {
  if (expr->type == VarRef::type) { // this is a 'var' thunk node
    VarRef *ref = reinterpret_cast<VarRef*>(expr);
    if (ref->index >= 0) {
      // !!! return glob[ref->index];
    } else {
      Binding *iter = bindings;
      for (int index = ++ref->index; index; ++index)
        iter = iter->next;
      iter->thunk->depend(queue, new VarRet(this));
    }
  } else if (expr->type == App::type) {
    App *app = reinterpret_cast<App*>(expr);
    Thunk *fn  = new Thunk(this, app->fn.get(), bindings);
    Thunk *arg = new Thunk(this, app->val.get(), bindings);
    queue.push_back(fn);
    queue.push_back(arg);
    fn->depend(queue, new AppRet(this));
  } else if (expr->type == Lambda::type) {
    Lambda *lambda = reinterpret_cast<Lambda*>(expr);
    Closure *closure = new Closure(lambda->body.get(), bindings);
    broadcast(queue, this, closure);
  } else {
    assert(0);
  }
}

int main() {
  ActionQueue queue;
  while (!queue.empty()) {
    Action *doit = queue.front();
    queue.pop_front();
    doit->execute(queue);
  }
}

