#include "action.h"
#include "expr.h"
#include "value.h"
#include "prim.h"
#include <iostream>
#include <cassert>

Action::~Action() { }

const char *Thunk  ::type = "Thunk";
const char *PrimArg::type = "PrimArg";
const char *PrimRet::type = "PrimFn";
const char *VarRet ::type = "VarRet";
const char *AppRet ::type = "AppRet";
const char *AppFn  ::type = "AppFn";
const char *MapRet ::type = "MapRet";
const char *TopRet ::type = "TopRet";

void Thunk::depend(ActionQueue &queue, Callback *callback) {
  if (return_action) {
    callback->input_action = return_action;
    callback->input_value  = return_value;
    queue.push_back(callback);
  } else {
    wait.push_back(callback);
  }
}

void Thunk::broadcast(ActionQueue &queue, Action *return_action_, Value *return_value_) {
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
    stack_trace(this);
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

void TopRet::execute(ActionQueue &queue) {
  Thunk *thunk = reinterpret_cast<Thunk*>(invoker);
  thunk->broadcast(queue, this, input_value);
}

void PrimArg::chain(ActionQueue &queue, Action *invoker, Prim *prim, Binding *binding, int arg) {
  if (prim->args == arg) {
    std::vector<Value*> args;
    Action *iter = invoker;
    for (int index = 0; index < prim->args; ++index) {
      PrimArg *prim = reinterpret_cast<PrimArg*>(iter);
      args.push_back(prim->input_value);
      iter = iter->invoker;
    }
    PrimRet *completion = new PrimRet(invoker);
    completion->input_action = invoker;
    prim->fn(prim->data, args, completion);
  } else {
    binding->thunk->depend(queue, new PrimArg(invoker, prim, binding, arg));
  }
}

void PrimArg::execute(ActionQueue &queue) {
  chain(queue, this, prim, binding->next, arg+1);
}

void PrimRet::execute(ActionQueue &queue) {
  Action *iter;
  for (iter = invoker; iter->type != Thunk::type; iter = iter->invoker) { }
  Thunk *thunk = reinterpret_cast<Thunk*>(iter);
  thunk->broadcast(queue, this, input_value);
}

static Binding *bind_defmap(ActionQueue &queue, Action *invoker, DefMap *def, Binding *bindings) {
  Thunk *thunk = new Thunk[def->map.size()]();
  Binding *defs = new Binding(thunk, bindings);
  int j = 0;
  for (auto &i : def->map) {
    thunk[j].invoker = invoker;
    thunk[j].expr = i.second.get();
    thunk[j].bindings = defs;
    queue.push_back(thunk + j++);
  }
  return defs;
}

void Thunk::execute(ActionQueue &queue) {
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
    DefMap *defmap = reinterpret_cast<DefMap*>(expr);
    Binding *defs = bind_defmap(queue, this, defmap, bindings);
    Thunk *body = new Thunk(this, defmap->body.get(), defs);
    queue.push_back(body);
    body->depend(queue, new MapRet(this));
  } else if (expr->type == Literal::type) {
    Literal *lit = reinterpret_cast<Literal*>(expr);
    broadcast(queue, this, lit->value.get());
  } else if (expr->type == Prim::type) {
    Prim *prim = reinterpret_cast<Prim*>(expr);
    PrimArg::chain(queue, this, prim, bindings, 0);
  } else if (expr->type == Top::type) {
    Top *top = reinterpret_cast<Top*>(expr);
    Thunk *globals = new Thunk[top->globals.size()]();
    Binding *global = new Binding(globals, bindings);
    std::vector<Binding*> defmaps;
    for (auto &i : top->defmaps)
      defmaps.push_back(bind_defmap(queue, this, &i, global));
    int j = 0;
    for (auto &i : top->globals) {
      globals[j].invoker  = this;
      globals[j].expr     = i.second.var.get();
      globals[j].bindings = defmaps[i.second.defmap];
      queue.push_back(globals + j++);
    }
    Thunk *main = new Thunk(this, top->main.get(), global);
    queue.push_back(main);
    main->depend(queue, new TopRet(this));
  } else {
    assert(0 /* unreachable */);
  }
}
