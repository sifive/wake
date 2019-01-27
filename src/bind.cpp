#include "bind.h"
#include "expr.h"
#include "prim.h"
#include "value.h"
#include "symbol.h"
#include <iostream>
#include <algorithm>
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
  int index, lowlink, onstack; // Tarjan SCC variables
};

struct SCCState {
  std::vector<ResolveDef> *defs;
  std::vector<int> *levelmap;
  std::vector<int> S;
  DefBinding *binding;
  int index;
  int level;
};

static void SCC(SCCState &state, int vi) {
  ResolveDef &v = (*state.defs)[vi];

  v.index = state.index;
  v.lowlink = state.index;
  ++state.index;

  state.S.push_back(vi);
  v.onstack = 1;

  for (auto wi : v.edges) {
    ResolveDef &w = (*state.defs)[wi];
    if ((*state.levelmap)[wi] != state.level) continue;

    if (w.index == -1 && w.expr->type == Lambda::type) {
      SCC(state, wi);
      v.lowlink = std::min(v.lowlink, w.lowlink);
    } else if (w.onstack) {
      v.lowlink = std::min(v.lowlink, w.index);
    }
  }

  if (v.lowlink == v.index) {
    int wi, scc_id = state.binding->fun.size();
    do {
      wi = state.S.back();
      ResolveDef &w = (*state.defs)[wi];
      state.S.pop_back();
      w.onstack = 0;
      state.binding->order[w.name] = state.binding->fun.size() + state.binding->val.size();
      state.binding->fun.emplace_back(reinterpret_cast<Lambda*>(w.expr.release()));
      state.binding->scc.push_back(scc_id);
    } while (wi != vi);
  }
}

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

  std::vector<int> d(defs.size(), 0), p(defs.size(), -1);
  std::queue<RelaxedVertex> q;

  for (int i = 0; i < (int)defs.size(); ++i)
    q.push(RelaxedVertex(i, 0));

  while (!q.empty()) {
    RelaxedVertex rv = q.front();
    int drv = d[rv.v];
    q.pop();
    if (rv.d < drv) continue;
    ResolveDef &def = defs[rv.v];
    if (drv >= (int)defs.size()) {
      int j = rv.v;
      for (int i = 0; i < (int)defs.size(); ++i) {
        d[i] = 0;
        j = p[j];
      }
      // j is now inside the cycle
      std::cerr << "Value definition cycle detected including:" << std::endl;
      int i = j;
      do {
        std::cerr << "  " << defs[i].name << " at " << defs[i].expr->location << std::endl;
        i = p[i];
      } while (i != j);
      return 0;
    }
    int w = def.expr->type == Lambda::type ? 0 : 1;
    for (auto i : def.edges) {
      if (drv + w > d[i]) {
        d[i] = drv + w;
        p[i] = rv.v;
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
    for (auto j : levels[i]) {
      if (defs[j].expr->type != Lambda::type) {
        bind->order[defs[j].name] = bind->val.size();
        bind->val.emplace_back(std::move(defs[j].expr));
        defs[j].index = 0;
      } else {
        defs[j].index = -1;
        defs[j].lowlink = -1;
        defs[j].onstack = 0;
      }
    }
    SCCState state;
    state.defs = &defs;
    state.levelmap = &d;
    state.binding = bind.get();
    state.level = i;
    state.index = 0;
    for (auto j : levels[i])
      if (defs[j].index == -1 && defs[j].expr->type == Lambda::type)
        SCC(state, j);
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
  return new VarRef(location, "Nil");
}

struct PatternTree {
  Sum *sum; // nullptr if unexpanded
  int cons;
  int var; // -1 if unbound/_
  std::vector<PatternTree> children;
  PatternTree(int var_ = -1) : sum(0), cons(0), var(var_) { }
  void format(std::ostream &os, int p) const;
};

std::ostream & operator << (std::ostream &os, const PatternTree &pt) {
  pt.format(os, 0);
  return os;
}

void PatternTree::format(std::ostream &os, int p) const
{
  if (!sum) {
    os << "_";
    return;
  }

  const std::string &name = sum->members[cons].ast.name;
  if (name.substr(0, 7) == "binary ") {
    op_type q = op_precedence(name.c_str() + 7);
    if (q.p < p) os << "(";
    children[0].format(os, q.p + !q.l);
    if (name[7] != ',') os << " ";
    os << name.c_str() + 7 << " ";
    children[1].format(os, q.p + q.l);
    if (q.p < p) os << ")";
  } else if (name.substr(0, 6) == "unary ") {
    op_type q = op_precedence(name.c_str() + 6);
    if (q.p < p) os << "(";
    os << name.c_str() + 6;
    children[0].format(os, q.p);
    if (q.p < p) os << ")";
  } else {
    op_type q = op_precedence("a");
    if (q.p < p && !children.empty()) os << "(";
    os << name;
    for (auto &v : children) {
      os << " ";
      v.format(os, q.p + q.l);
    }
    if (q.p < p && !children.empty()) os << ")";
  }
}

struct PatternRef {
  Location location;
  PatternTree tree;
  int index; // for prototype: next var name, for patterns: function index
  int uses;

  PatternRef(const Location &location_) : location(location_), uses(0) { }
  PatternRef(PatternRef &&other) = default;
  PatternRef & operator = (PatternRef &&other) = default;
};

// assumes a detail <= b
static Sum *find_mismatch(std::vector<int> &path, const PatternTree &a, const PatternTree &b) {
  if (!a.sum) return b.sum;
  for (size_t i = 0; i < a.children.size(); ++i) {
    path.push_back(i);
    Sum *out = find_mismatch(path, a.children[i], b.children[i]);
    if (out) return out;
    path.pop_back();
  }
  return nullptr;
}

static Expr *fill_pattern(Expr *expr, const PatternTree &a, const PatternTree &b) {
  if (b.var >= 0) {
    expr = new App(expr->location,
      expr,
      new VarRef(expr->location, "_a" + std::to_string(a.var)));
  } else {
    for (size_t i = 0; i < a.children.size(); ++i)
      expr = fill_pattern(expr, a.children[i], b.children[i]);
  }
  return expr;
}

static PatternTree *get_expansion(PatternTree *t, const std::vector<int> &path) {
  for (auto i : path) t = &t->children[i];
  return t;
}

// invariants: !patterns.empty(); patterns have detail >= patterns[0]
// post-condition: patterns unchanged (internal mutation is reversed)
static std::unique_ptr<Expr> expand_patterns(std::vector<PatternRef> &patterns) {
  PatternRef &prototype = patterns[0];
  if (patterns.size() == 1) {
    std::cerr << "Non-exhaustive match at " << prototype.location
      << "; missing: " << prototype.tree << std::endl;
    return nullptr;
  }
  std::vector<int> expand;
  Sum *sum = find_mismatch(expand, prototype.tree, patterns[1].tree);
  if (sum) {
    std::unique_ptr<DefMap> map(new DefMap(prototype.location));
    map->body = std::unique_ptr<Expr>(new VarRef(prototype.location, "destruct " + sum->name));
    for (size_t c = 0; c < sum->members.size(); ++c) {
      std::string cname = "_c" + std::to_string(c);
      map->body = std::unique_ptr<Expr>(new App(prototype.location,
        map->body.release(),
        new VarRef(prototype.location, cname)));
      std::vector<PatternRef> bucket;
      std::vector<int> undo;
      int args = sum->members[c].ast.args.size();
      int var = prototype.index;
      prototype.index += args;
      for (auto p = patterns.begin(); p != patterns.end(); ++p) {
        PatternTree *t = get_expansion(&p->tree, expand);
        if (!t->sum) {
          t->sum = sum;
          t->cons = c;
          t->children.resize(args);
          if (p == patterns.begin())
            for (auto &c : t->children)
              c.var = var++;
          bucket.emplace_back(std::move(*p));
          p->index = -1;
        } else if (t->sum != sum) {
          std::cerr << "Constructor " << t->sum->members[t->cons].ast.name
            << " is not a member of " << sum->name
            << " but is used in pattern at " << p->location
            << "." << std::endl;
          return nullptr;
        } else if (t->sum && t->cons == (int)c) {
          bucket.emplace_back(std::move(*p));
          p->index = -2;
        }
      }
      std::unique_ptr<Expr> &exp = map->map[cname];
      exp = expand_patterns(bucket);
      if (!exp) return nullptr;
      for (int i = 0; i < args; ++i)
        exp = std::unique_ptr<Expr>(new Lambda(prototype.location,
          "_a" + std::to_string(--var), exp.release()));
      exp = std::unique_ptr<Expr>(new Lambda(prototype.location, "_", exp.release()));
      for (auto p = patterns.rbegin(); p != patterns.rend(); ++p) {
        if (p->index == -1) {
          *p = std::move(bucket.back());
          bucket.pop_back();
          PatternTree *t = get_expansion(&p->tree, expand);
          t->sum = nullptr;
          t->children.clear();
        } else if (p->index == -2) {
          *p = std::move(bucket.back());
          bucket.pop_back();
        }
      }
    }
    map->body = std::unique_ptr<Expr>(new App(prototype.location,
      map->body.release(),
      new VarRef(prototype.location, "_a" + std::to_string(
        get_expansion(&prototype.tree, expand)->var))));
    return std::unique_ptr<Expr>(map.release());
  } else {
    PatternRef &p = patterns[1];
    ++p.uses;
    return std::unique_ptr<Expr>(fill_pattern(
      new App(p.location,
        new VarRef(p.location, "_f" + std::to_string(p.index)),
        new VarRef(p.location, "_a0")),
      prototype.tree, p.tree));
  }
}

static PatternTree cons_lookup(ResolveBinding *binding, std::unique_ptr<Expr> &expr, const AST &ast, Sum *multiarg) {
  PatternTree out;
  if (ast.name == "_") {
    // no-op; unbound
  } else if (!ast.name.empty() && Lexer::isLower(ast.name.c_str())) {
    expr = std::unique_ptr<Expr>(new Lambda(expr->location, ast.name, expr.release()));
    out.var = 0; // bound
  } else {
    for (ResolveBinding *iter = binding; iter; iter = iter->parent) {
      NameIndex::iterator it = iter->index.end();
      if (iter->prefix >= 0)
        it = iter->index.find(std::to_string(iter->prefix) + " " + ast.name);
      if (it == iter->index.end())
        it = iter->index.find(ast.name);
      if (it != iter->index.end()) {
        Expr *cons = iter->defs[it->second].expr.get();
        if (cons) {
          while (cons->type == Lambda::type)
            cons = reinterpret_cast<Lambda*>(cons)->body.get();
          if (cons->type == Construct::type) {
            Construct *c = reinterpret_cast<Construct*>(cons);
            out.sum = c->sum;
            out.cons = c->cons->index;
          }
        }
      }
    }
    if (ast.name.empty()) out.sum = multiarg;
    if (!out.sum) {
      std::cerr << "Constructor " << ast.name
        << " in pattern match not found at " << ast.location
        << "." << std::endl;
      out.var = 0;
    } else if (out.sum->members[out.cons].ast.args.size() != ast.args.size()) {
      if (ast.name.empty()) {
        std::cerr << "Case";
      } else {
        std::cerr << "Constructor " << ast.name;
      }
      std::cerr  << " in pattern match has " << ast.args.size()
        << " parameters, but must have " << out.sum->members[out.cons].ast.args.size()
        << " at " << ast.location
        << "." << std::endl;
      out.sum = 0;
      out.var = 0;
    } else {
      for (auto a = ast.args.rbegin(); a != ast.args.rend(); ++a)
        out.children.push_back(cons_lookup(binding, expr, *a, 0));
      std::reverse(out.children.begin(), out.children.end());
    }
  }
  return out;
}

static std::unique_ptr<Expr> rebind_match(ResolveBinding *binding, std::unique_ptr<Match> match) {
  std::unique_ptr<DefMap> map(new DefMap(match->location));
  std::vector<PatternRef> patterns;
  Sum multiarg(AST(LOCATION));
  multiarg.members.emplace_back(AST(LOCATION));

  int index = 0;
  std::vector<PatternTree> children;
  for (auto &a : match->args) {
    map->map["_a" + std::to_string(index)] = std::move(a);
    children.emplace_back(index);
    multiarg.members.front().ast.args.emplace_back(AST(LOCATION));
    ++index;
  }

  patterns.emplace_back(match->location);
  PatternRef &prototype = patterns.front();
  prototype.uses = 1;
  prototype.index = index;

  if (index == 1) {
    prototype.tree = std::move(children.front());
  } else {
    prototype.tree.children = std::move(children);
    prototype.tree.sum = &multiarg;
  }

  int f = 0;
  bool ok = true;
  for (auto &p : match->patterns) {
    std::unique_ptr<Expr> &expr = map->map["_f" + std::to_string(f)];
    expr = std::move(p.expr);
    patterns.emplace_back(expr->location);
    patterns.back().index = f;
    patterns.back().tree = cons_lookup(binding, expr, p.pattern, &multiarg);
    ok &= !patterns.front().tree.sum || patterns.back().tree.sum;
    expr = std::unique_ptr<Expr>(new Lambda(expr->location, "_", expr.release()));
    ++f;
  }
  if (!ok) return nullptr;
  map->body = expand_patterns(patterns);
  if (!map->body) return nullptr;
  for (auto &p : patterns) {
    if (!p.uses) {
      std::cerr << "Pattern unreachable in match at " << p.location << std::endl;
      return nullptr;
    }
  }
  return std::unique_ptr<Expr>(map.release());
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
  } else if (expr->type == Match::type) {
    std::unique_ptr<Match> m(reinterpret_cast<Match*>(expr.release()));
    auto out = rebind_match(binding, std::move(m));
    if (!out) return out;
    return fracture(std::move(out), binding);
  } else if (expr->type == Memoize::type) {
    Memoize *memoize = reinterpret_cast<Memoize*>(expr.get());
    memoize->body = fracture(std::move(memoize->body), binding);
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
      for (auto &i : b->map) {
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
      for (auto &i : b->publish) {
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
      for (int i = 0; i < (int)b->map.size() + (int)b->publish.size(); ++i) {
        tbinding.defs[tbinding.current_index].expr =
          fracture(std::move(tbinding.defs[tbinding.current_index].expr), &tbinding);
        ++tbinding.current_index;
      }
      ++tbinding.prefix;
    }
    tbinding.current_index = -1;
    std::unique_ptr<Expr> body = fracture(std::move(top->body), &tbinding);
    return fracture_binding(top->location, tbinding.defs, std::move(body));
  } else {
    // Literal/Prim/Construct/Destruct
    return expr;
  }
}

struct NameRef {
  int depth;
  int offset;
  int def;
  TypeVar *var;
};

struct NameBinding {
  NameBinding *next;
  DefBinding *binding;
  Lambda *lambda;
  bool open;
  int generalized;

  NameBinding() : next(0), binding(0), lambda(0), open(true), generalized(0) { }
  NameBinding(NameBinding *next_, Lambda *lambda_) : next(next_), binding(0), lambda(lambda_), open(true), generalized(0) { }
  NameBinding(NameBinding *next_, DefBinding *binding_) : next(next_), binding(binding_), lambda(0), open(true), generalized(0) { }

  NameRef find(const std::string &x) {
    NameRef out;
    std::map<std::string, int>::iterator i;
    if (lambda && lambda->name == x) {
      out.depth = 0;
      out.offset = 0;
      out.def = 0;
      out.var = &lambda->typeVar[0];
    } else if (binding && (i = binding->order.find(x)) != binding->order.end()) {
      out.depth = 0;
      out.offset = i->second;
      out.def = i->second < generalized;
      if (i->second < (int)binding->val.size()) {
        auto x = binding->val[i->second].get();
        out.var = x?&x->typeVar:0;
      } else {
        auto x = binding->fun[i->second-binding->val.size()].get();
        out.var = x?&x->typeVar:0;
      }
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
  if (!expr) return false; // failed fracture
  expr->typeVar.setDOB();
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
    if (!pos.var) return true;
    if (pos.def) {
      TypeVar temp;
      pos.var->clone(temp);
      return ref->typeVar.unify(temp, &ref->location);
    } else {
      return ref->typeVar.unify(*pos.var, &ref->location);
    }
  } else if (expr->type == App::type) {
    App *app = reinterpret_cast<App*>(expr);
    binding->open = false;
    bool f = explore(app->fn .get(), pmap, binding);
    bool a = explore(app->val.get(), pmap, binding);
    bool t = f && app->fn->typeVar.unify(TypeVar(FN, 2), &app->location);
    bool ta = t && a && app->fn->typeVar[0].unify(app->val->typeVar, &app->location);
    bool tr = t && app->fn->typeVar[1].unify(app->typeVar, &app->location);
    return f && a && t && ta && tr;
  } else if (expr->type == Lambda::type) {
    Lambda *lambda = reinterpret_cast<Lambda*>(expr);
    bool t = lambda->typeVar.unify(TypeVar(FN, 2), &lambda->location);
    NameBinding bind(binding, lambda);
    bool out = explore(lambda->body.get(), pmap, &bind);
    bool tr = t && out && lambda->typeVar[1].unify(lambda->body->typeVar, &lambda->location);
    return out && t && tr;
  } else if (expr->type == Memoize::type) {
    Memoize *memoize = reinterpret_cast<Memoize*>(expr);
    bool out = explore(memoize->body.get(), pmap, binding);
    bool t = out && memoize->typeVar.unify(memoize->body->typeVar, &memoize->location);
    return out && t;
  } else if (expr->type == DefBinding::type) {
    DefBinding *def = reinterpret_cast<DefBinding*>(expr);
    binding->open = false;
    NameBinding bind(binding, def);
    bool ok = true;
    for (auto &i : def->val)
      ok = explore(i.get(), pmap, binding) && ok;
    for (int i = 0; i < (int)def->fun.size(); ++i) {
      for (int j = i; j < (int)def->fun.size() && i == def->scc[j]; ++j)
        if (def->fun[j]) def->fun[j]->typeVar.setDOB();
      bind.generalized = def->val.size() + def->scc[i];
      ok = explore(def->fun[i].get(), pmap, &bind) && ok;
    }
    bind.generalized = def->val.size() + def->fun.size();
    ok = explore(def->body.get(), pmap, &bind) && ok;
    ok = ok && def->typeVar.unify(def->body->typeVar, &def->location) && ok;
    return ok;
  } else if (expr->type == Literal::type) {
    Literal *lit = reinterpret_cast<Literal*>(expr);
    return lit->typeVar.unify(lit->value->getType(), &lit->location);
  } else if (expr->type == Construct::type) {
    Construct *cons = reinterpret_cast<Construct*>(expr);
    bool ok = cons->typeVar.unify(
      TypeVar(cons->sum->name.c_str(), cons->sum->args.size()));
    std::map<std::string, TypeVar> ids;
    for (size_t i = 0; i < cons->sum->args.size(); ++i) {
      TypeVar &var = ids[cons->sum->args[i]];
      var.setDOB();
      ok = cons->typeVar[i].unify(var) && ok;
    }
    NameBinding *iter = binding;
    for (size_t i = cons->cons->ast.args.size(); i; --i) {
      ok = cons->cons->ast.args[i-1].unify(iter->lambda->typeVar[0], ids) && ok;
      iter = iter->next;
    }
    return ok;
  } else if (expr->type == Destruct::type) {
    Destruct *des = reinterpret_cast<Destruct*>(expr);
    // (typ => cons0 => b) => (typ => cons1 => b) => typ => b
    TypeVar &typ = binding->lambda->typeVar[0];
    bool ok = typ.unify(
      TypeVar(des->sum.name.c_str(), des->sum.args.size()));
    std::map<std::string, TypeVar> ids;
    for (size_t i = 0; i < des->sum.args.size(); ++i) {
      TypeVar &var = ids[des->sum.args[i]];
      var.setDOB();
      ok = typ[i].unify(var) && ok;
    }
    NameBinding *iter = binding;
    for (size_t i = des->sum.members.size(); i; --i) {
      iter = iter->next;
      TypeVar *tail = &iter->lambda->typeVar[0];
      Constructor &cons = des->sum.members[i-1];
      if (!tail->unify(TypeVar(FN, 2))) { ok = false; break; }
      ok = (*tail)[0].unify(typ) && ok;
      tail = &(*tail)[1];
      size_t j;
      for (j = 0; j < cons.ast.args.size(); ++j) {
        if (!tail->unify(TypeVar(FN, 2))) { ok = false; break; }
        ok = cons.ast.args[j].unify((*tail)[0], ids) && ok;
        tail = &(*tail)[1];
      }
      if (j == cons.ast.args.size()) {
        ok = des->typeVar.unify(*tail) && ok;
      }
    }
    return ok;
  } else if (expr->type == Prim::type) {
    Prim *prim = reinterpret_cast<Prim*>(expr);
    std::vector<TypeVar*> args;
    for (NameBinding *iter = binding; iter && iter->open && iter->lambda; iter = iter->next)
      args.push_back(&iter->lambda->typeVar[0]);
    std::reverse(args.begin(), args.end());
    prim->args = args.size();
    PrimMap::const_iterator i = pmap.find(prim->name);
    if (i == pmap.end()) {
      std::cerr << "Primitive reference "
        << prim->name << " is unbound at "
        << prim->location << std::endl;
      return false;
    } else {
      prim->flags = i->second.flags;
      prim->fn   = i->second.fn;
      prim->data = i->second.data;
      bool ok = i->second.type(args, &prim->typeVar);
      if (!ok) std::cerr << "Primitive reference "
        << prim->name << " has wrong type signature at "
        << prim->location << std::endl;
      return ok;
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
