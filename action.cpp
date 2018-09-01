#include "action.h"
#include "expr.h"
#include "value.h"
#include <iostream>
#include <cassert>

Action::~Action() { }

const char *Thunk ::type = "Thunk";
const char *VarRet::type = "VarRet";
const char *AppRet::type = "AppRet";
const char *AppFn ::type = "AppFn";
const char *MapRet::type = "MapRet";

void Thunk::depend(ActionQueue& queue, Callback *callback) {
  if (return_action) {
    callback->input_action = return_action;
    callback->input_value  = return_value;
    queue.push_back(callback);
  } else {
    wait.push_back(callback);
  }
}

void Thunk::broadcast(ActionQueue& queue, Action *return_action_, Value *return_value_) {
  return_action = return_action_;
  return_value  = return_value_;
  while (!wait.empty()) {
    Callback *callback = wait.front();
    wait.pop_front();
    callback->input_action = return_action;
    callback->input_value  = return_value;
    queue.push_back(callback);
  }
}

void VarRet::execute(ActionQueue &queue) {
  Thunk *thunk = reinterpret_cast<Thunk*>(invoker);
  thunk->broadcast(queue, this, input_value);
}

void AppRet::execute(ActionQueue &queue) {
  AppFn *appFn = reinterpret_cast<AppFn*>(invoker);
  Thunk *thunk = reinterpret_cast<Thunk*>(appFn->invoker);
  thunk->broadcast(queue, this, input_value);
}

void AppFn::execute(ActionQueue &queue) {
  if (input_value->type != Closure::type) {
    std::cerr << "Attempt to apply " << input_value << " which is not a Closure" << std::endl;
    for (Action *action = this; action; action = action->invoker) {
      if (action->type == Thunk::type) {
        Thunk *thunk = reinterpret_cast<Thunk*>(action);
        std::cerr << "  from " << thunk->expr->location.str() << std::endl;
      }
    }
    exit(1);
  }
  Closure *clo = reinterpret_cast<Closure*>(input_value);
  Thunk *thunk = new Thunk(this, clo->body, new Binding(arg, clo->bindings));
  queue.push_back(thunk);
  thunk->depend(queue, new AppRet(this));
}

void MapRet::execute(ActionQueue &queue) {
  Thunk *thunk = reinterpret_cast<Thunk*>(invoker);
  thunk->broadcast(queue, this, input_value);
}

void Thunk::execute(ActionQueue& queue) {
  if (expr->type == VarRef::type) {
    VarRef *ref = reinterpret_cast<VarRef*>(expr);
    Binding *iter = bindings;
    for (int depth = ref->depth; depth; --depth)
      iter = iter->next;
    iter->thunk[ref->offset].depend(queue, new VarRet(this));
  } else if (expr->type == App::type) {
    App *app = reinterpret_cast<App*>(expr);
    Thunk *fn  = new Thunk(this, app->fn.get(), bindings);
    Thunk *arg = new Thunk(this, app->val.get(), bindings);
    queue.push_back(fn);
    queue.push_back(arg);
    fn->depend(queue, new AppFn(this, arg));
  } else if (expr->type == Lambda::type) {
    Lambda *lambda = reinterpret_cast<Lambda*>(expr);
    Closure *closure = new Closure(lambda->body.get(), bindings);
    broadcast(queue, this, closure);
  } else if (expr->type == DefMap::type) {
    DefMap *def = reinterpret_cast<DefMap*>(expr);
    Thunk *thunk = new Thunk[def->map.size()]();
    Binding *defs = new Binding(thunk, bindings);
    int j = 0;
    for (auto i = def->map.begin(); i != def->map.end(); ++i, ++j) {
      thunk[j].invoker = this;
      thunk[j].expr = i->second.get();
      thunk[j].bindings = defs;
      queue.push_back(thunk + j);
    }
    Thunk *body = new Thunk(this, def->body.get(), defs);
    queue.push_back(body);
    body->depend(queue, new MapRet(this));
  } else if (expr->type == Literal::type) {
    Literal *lit = reinterpret_cast<Literal*>(expr);
    broadcast(queue, this, lit->value.get());
  } else {
    assert(0 /* unreachable */);
  }
}
