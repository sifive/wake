#include "bind.h"
#include "expr.h"
#include <iostream>
#include <cassert>

struct NameRef {
  int depth;
  int offset;
};

struct NameBinding {
  NameBinding *next;
  std::map<std::string, int> *map;
  std::string *name;
  bool open;

  NameBinding(NameBinding *next_, std::string *name_) : next(next_), map(0), name(name_), open(true) { }
  NameBinding(NameBinding *next_, std::map<std::string, int> *map_) : next(next_), map(map_), name(0), open(true) { }

  NameRef find(const std::string &x) {
    NameRef out;
    std::map<std::string, int>::iterator i;
    if (name && *name == x) {
      out.depth = 0;
      out.offset = 0;
    } else if (map && (i = map->find(x)) != map->end()) {
      out.depth = 0;
      out.offset = i->second;
    } else if (next) {
      out = next->find(x);
      ++out.depth;
    } else {
      out.depth = 0;
      out.offset = -1;
    }
    return out;
  }
};

static bool explore(Expr *expr, const PrimMap &pmap, NameBinding *binding) {
  if (expr->type == VarRef::type) {
    VarRef *ref = reinterpret_cast<VarRef*>(expr);
    NameRef pos = binding->find(ref->name);
    if (pos.offset == -1) {
      std::cerr << "Variable reference "
        << ref->name << " is unbound at "
        << ref->location.str() << std::endl;
      return false;
    }
    ref->depth = pos.depth;
    ref->offset = pos.offset;
    return true;
  } else if (expr->type == App::type) {
    App *app = reinterpret_cast<App*>(expr);
    binding->open = false;
    bool f = explore(app->fn .get(), pmap, binding);
    bool a = explore(app->val.get(), pmap, binding);
    return f && a;
  } else if (expr->type == Lambda::type) {
    Lambda *lambda = reinterpret_cast<Lambda*>(expr);
    NameBinding bind(binding, &lambda->name);
    return explore(lambda->body.get(), pmap, &bind);
  } else if (expr->type == DefMap::type) {
    DefMap *def = reinterpret_cast<DefMap*>(expr);
    std::map<std::string, int> offsets;
    NameBinding bind(binding, &offsets);
    int j = 0;
    for (auto &i : def->map) offsets[i.first] = j++;
    bool ok = true;
    for (auto &i : def->map)
      ok = explore(i.second.get(), pmap, &bind) && ok;
    ok = explore(def->body.get(), pmap, &bind) && ok;
    return ok;
  } else if (expr->type == Literal::type) {
    return true;
  } else if (expr->type == Prim::type) {
    Prim *prim = reinterpret_cast<Prim*>(expr);
    int args = 0;
    for (NameBinding *iter = binding; iter && iter->open && iter->name; iter = iter->next) ++args;
    prim->args = args;
    PrimMap::const_iterator i = pmap.find(prim->name);
    if (i == pmap.end()) {
      std::cerr << "Primitive reference "
        << prim->name << " is unbound at "
        << prim->location.str() << std::endl;
      return false;
    } else {
      prim->fn   = i->second.first;
      prim->data = i->second.second;
      return true;
    }
  } else if (expr->type == Top::type) {
    Top *top = reinterpret_cast<Top*>(expr);
    std::map<std::string, int> offsets;
    NameBinding bind(binding, &offsets);
    int j = 0;
    for (auto &i : top->globals) offsets[i.first] = j++;
    bool ok = true;
    for (auto &i : top->defmaps)
      ok = explore(&i, pmap, &bind) && ok;
    return ok;
  } else {
    assert(0 /* unreachable */);
    return false;
  }
}

bool bind_refs(Expr *expr, const PrimMap &pmap) {
  return explore(expr, pmap, 0);
}
