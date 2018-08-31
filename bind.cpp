#include "bind.h"
#include "expr.h"

struct NameRef {
  int depth;
  int offset;
};

struct NameBinding {
  NameBinding *next;
  std::map<std::string, int> *map;
  std::string *name;

  NameBinding(NameBinding *next_) : next(next_), map(0), name(0) { }
  NameRef find(const std::string& x) {
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

static bool explore(Expr *expr, NameBinding *binding) {
  if (expr->type == VarRef::type) {
    VarRef *ref = reinterpret_cast<VarRef*>(expr);
    NameRef pos = binding->find(ref->name);
    if (pos.offset == -1) {
      fprintf(stderr, "Variable reference %s is unbound at %s\n", ref->name.c_str(), ref->location.c_str());
      return false;
    }
    ref->depth = pos.depth;
    ref->offset = pos.offset;
    return true;
  } else if (expr->type == App::type) {
    App *app = reinterpret_cast<App*>(expr);
    bool f = explore(app->fn.get(), binding);
    bool a = explore(app->val.get(), binding);
    return f && a;
  } else if (expr->type == Lambda::type) {
    Lambda *lambda = reinterpret_cast<Lambda*>(expr);
    NameBinding bind(binding);
    bind.name = &lambda->name;
    return explore(lambda->body.get(), &bind);
  } else if (expr->type == DefMap::type) {
    DefMap *def = reinterpret_cast<DefMap*>(expr);
    NameBinding bind(binding);
    std::map<std::string, int> offsets;
    int j = 0;
    for (auto i = def->map.begin(); i != def->map.end(); ++i, ++j)
      offsets[i->first] = j;
    bind.map = &offsets;
    bool ok = true;
    for (auto i = def->map.begin(); i != def->map.end(); ++i)
      ok = explore(i->second.get(), &bind) && ok;
    ok = explore(def->body.get(), &bind) && ok;
    return ok;
  } else if (expr->type == Literal::type) {
    return true;
  } else {
    assert(0 /* unreachable */);
    return false;
  }
}

bool bind_refs(Expr *expr) {
  return explore(expr, 0);
}
