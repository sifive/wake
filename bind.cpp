#include "bind.h"
#include "expr.h"
#include "prim.h"
#include <iostream>
#include <vector>
#include <map>
#include <set>
#include <queue>
#include <list>
#include <cassert>

typedef std::map<std::string, int> NameIndex;

struct ResolveDef {
  std::string name;
  std::unique_ptr<Expr> expr;
  std::set<int> edges; // edges: things this name uses
};

struct ResolveBinding {
  ResolveBinding *parent;
  int current_index;
  int prefix;
  int depth;
  NameIndex index;
  std::vector<ResolveDef> defs;
};

struct RelaxedVertex {
  int v;
  int d;
  RelaxedVertex(int v_, int d_) : v(v_), d(d_) { }
};

static std::unique_ptr<Expr> fracture_binding(const Location &location, std::vector<ResolveDef> &defs, std::unique_ptr<Expr> body) {
  // Bellman-Ford algorithm, run for longest path
  // if f uses [yg], then d[f] must be <= d[yg]
  // if x uses [yg], then d[x] must be <= d[yg]+1
  // if we ever find a d[_] > n, there is an illegal loop

  std::vector<int> d(defs.size(), 0);
  std::queue<RelaxedVertex> q;

  for (int i = 0; i < (int)defs.size(); ++i)
    q.push(RelaxedVertex(i, 0));

  while (!q.empty()) {
    RelaxedVertex rv = q.front();
    int drv = d[rv.v];
    q.pop();
    if (rv.d < drv) continue;
    ResolveDef &def = defs[rv.v];
    if (drv > (int)defs.size()) {
      std::cerr << "Value definition cycle detected including "
        << def.name << " at "
        << def.expr->location << std::endl;
      for (int i = 0; i < (int)defs.size(); ++i) d[i] = 0;
      break;
    }
    int w = def.expr->type == Lambda::type ? 0 : 1;
    for (auto i : def.edges) {
      if (drv + w > d[i]) {
        d[i] = drv + w;
        q.push(RelaxedVertex(i, drv + w));
      }
    }
  }

  std::vector<std::list<int> > levels(defs.size());
  for (int i = 0; i < (int)defs.size(); ++i)
    levels[d[i]].push_back(i);

  std::unique_ptr<Expr> out(std::move(body));
  for (int i = 0; i < (int)defs.size(); ++i) {
    if (levels[i].empty()) continue;
    std::unique_ptr<DefBinding> bind(new DefBinding(location, std::move(out)));
    int vals = 0;
    for (auto j : levels[i]) {
      if (defs[j].expr->type != Lambda::type) ++vals;
    }
    for (auto j : levels[i]) {
      if (defs[j].expr->type == Lambda::type) {
        bind->order[defs[j].name] = bind->fun.size() + vals;
        bind->fun.emplace_back(reinterpret_cast<Lambda*>(defs[j].expr.release()));
      } else {
        bind->order[defs[j].name] = bind->val.size();
        bind->val.emplace_back(std::move(defs[j].expr));
      }
    }
    out = std::move(bind);
  }

  return out;
}

static bool reference_map(ResolveBinding *binding, const std::string &name) {
  NameIndex::iterator i;
  if ((i = binding->index.find(name)) != binding->index.end()) {
    if (binding->current_index != -1)
      binding->defs[binding->current_index].edges.insert(i->second);
    return true;
  } else {
    return false;
  }
}

static bool rebind_ref(ResolveBinding *binding, std::string &name) {
  ResolveBinding *iter;
  for (iter = binding; iter; iter = iter->parent) {
    if (iter->prefix >= 0) {
      std::string file_local = std::to_string(iter->prefix) + " " + name;
      if (reference_map(iter, file_local)) {
        name = file_local;
        return true;
      }
    }
    if (reference_map(iter, name)) return true;
  }
  return false;
}

Expr *rebind_subscribe(ResolveBinding *binding, const Location &location, const std::string &name) {
  ResolveBinding *iter;
  for (iter = binding; iter; iter = iter->parent) {
    std::string pub = "publish " + std::to_string(iter->depth) + " " + name;
    if (reference_map(iter, pub)) return new VarRef(location, pub);
  }
  // nil
  return new Lambda(location, "_", new Lambda(location, "_t", new Lambda(location, "_f", new VarRef(location, "_t"))));
}

static std::unique_ptr<Expr> fracture(std::unique_ptr<Expr> expr, ResolveBinding *binding) {
  if (expr->type == VarRef::type) {
    VarRef *ref = reinterpret_cast<VarRef*>(expr.get());
    // don't fail if unbound; leave that for the second pass
    rebind_ref(binding, ref->name);
    return expr;
  } else if (expr->type == Subscribe::type) {
    Subscribe *sub = reinterpret_cast<Subscribe*>(expr.get());
    return std::unique_ptr<Expr>(rebind_subscribe(binding, sub->location, sub->name));
  } else if (expr->type == App::type) {
    App *app = reinterpret_cast<App*>(expr.get());
    app->fn  = fracture(std::move(app->fn),  binding);
    app->val = fracture(std::move(app->val), binding);
    return expr;
  } else if (expr->type == Lambda::type) {
    Lambda *lambda = reinterpret_cast<Lambda*>(expr.get());
    ResolveBinding lbinding;
    lbinding.parent = binding;
    lbinding.current_index = 0;
    lbinding.prefix = -1;
    lbinding.depth = binding->depth + 1;
    lbinding.index[lambda->name] = 0;
    lbinding.defs.resize(lbinding.defs.size()+1); // don't care
    lambda->body = fracture(std::move(lambda->body), &lbinding);
    return expr;
  } else if (expr->type == DefMap::type) {
    DefMap *def = reinterpret_cast<DefMap*>(expr.get());
    ResolveBinding dbinding;
    dbinding.parent = binding;
    dbinding.prefix = -1;
    dbinding.depth = binding->depth + 1;
    for (auto &i : def->map) {
      dbinding.index[i.first] = dbinding.defs.size();
      dbinding.defs.resize(dbinding.defs.size()+1);
      ResolveDef &def = dbinding.defs.back();
      def.name = i.first;
      def.expr = std::move(i.second);
    }
    for (auto &i : def->publish) {
      std::string name = "publish " + std::to_string(dbinding.depth) + " " + i.first;
      dbinding.index[name] = dbinding.defs.size();
      dbinding.defs.resize(dbinding.defs.size()+1);
      ResolveDef &def = dbinding.defs.back();
      Location l = i.second->location;
      def.name = std::move(name);
      def.expr = std::unique_ptr<Expr>(new App(l, i.second.release(), rebind_subscribe(binding, l, i.first)));
    }
    dbinding.current_index = 0;
    for (auto &i : dbinding.defs) {
      // problem: publishes resolves to themselves !!!
      i.expr = fracture(std::move(i.expr), &dbinding);
      ++dbinding.current_index;
    }
    dbinding.current_index = -1;
    std::unique_ptr<Expr> body = fracture(std::move(def->body), &dbinding);
    return fracture_binding(def->location, dbinding.defs, std::move(body));
  } else if (expr->type == Top::type) {
    Top *top = reinterpret_cast<Top*>(expr.get());
    ResolveBinding tbinding;
    tbinding.parent = binding;
    tbinding.prefix = 0;
    tbinding.depth = binding ? binding->depth+1 : 0;
    int chain = 0;
    for (auto &b : top->defmaps) {
      for (auto &i : b.map) {
        std::string name;
        DefOrder::iterator glob;
        // If this file defines the global, put it at the global name; otherwise, localize the name
        if ((glob = top->globals.find(i.first)) != top->globals.end() && glob->second == tbinding.prefix) {
          name = i.first;
        } else {
          name = std::to_string(tbinding.prefix) + " " + i.first;
        }
        tbinding.index[name] = tbinding.defs.size();
        tbinding.defs.resize(tbinding.defs.size()+1);
        ResolveDef &def = tbinding.defs.back();
        def.name = name;
        def.expr = std::move(i.second);
      }
      for (auto &i : b.publish) {
        std::string name = "publish " + std::to_string(tbinding.depth) + " " + i.first;
        Location l = i.second->location;
        Expr *tail;
        NameIndex::iterator pub;
        if ((pub = tbinding.index.find(name)) == tbinding.index.end()) {
          tail = rebind_subscribe(binding, l, i.first);
        } else {
          std::string name = "chain " + std::to_string(++chain);
          tail = new VarRef(l, name);
          tbinding.index[name] = pub->second;
          tbinding.defs[pub->second].name = std::move(name);
        }
        tbinding.index[name] = tbinding.defs.size();
        tbinding.defs.resize(tbinding.defs.size()+1);
        ResolveDef &def = tbinding.defs.back();
        def.name = std::move(name);
        def.expr = std::unique_ptr<Expr>(new App(l, i.second.release(), tail));
      }
      ++tbinding.prefix;
    }

    tbinding.current_index = 0;
    tbinding.prefix = 0;
    for (auto &b : top->defmaps) {
      for (int i = 0; i < (int)b.map.size() + (int)b.publish.size(); ++i) {
        tbinding.defs[tbinding.current_index].expr =
          fracture(std::move(tbinding.defs[tbinding.current_index].expr), &tbinding);
        ++tbinding.current_index;
      }
      ++tbinding.prefix;
    }
    return fracture_binding(top->location, tbinding.defs, std::unique_ptr<Expr>(new VarRef(LOCATION, "main")));
  } else {
    // Literal/Prim
    return expr;
  }
}

struct NameRef {
  int depth;
  int offset;
};

struct NameBinding {
  NameBinding *next;
  DefOrder *map;
  std::string *name;
  bool open;

  NameBinding() : next(0), map(0), name(0), open(true) { }
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
    NameRef pos;
    if ((pos = binding->find(ref->name)).offset == -1) {
      std::cerr << "Variable reference "
        << ref->name << " is unbound at "
        << ref->location << std::endl;
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
  } else if (expr->type == DefBinding::type) {
    DefBinding *def = reinterpret_cast<DefBinding*>(expr);
    binding->open = false;
    NameBinding bind(binding, &def->order);
    bool ok = true;
    for (auto &i : def->val)
      ok = explore(i.get(), pmap, binding) && ok;
    for (auto &i : def->fun)
      ok = explore(i.get(), pmap, &bind) && ok;
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
        << prim->location << std::endl;
      return false;
    } else {
      prim->fn   = i->second.first;
      prim->data = i->second.second;
      return true;
    }
  } else {
    assert(0 /* unreachable */);
    return false;
  }
}

std::unique_ptr<Expr> bind_refs(std::unique_ptr<Top> top, const PrimMap &pmap) {
  NameBinding bottom;
  std::unique_ptr<Expr> out = fracture(std::move(top), 0);
  if (out && !explore(out.get(), pmap, &bottom)) out.reset();
  return out;
}
