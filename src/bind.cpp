/*
 * Copyright 2019 SiFive, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You should have received a copy of LICENSE.Apache2 along with
 * this software. If not, you may obtain a copy at
 *
 *    https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "bind.h"
#include "expr.h"
#include "prim.h"
#include "symbol.h"
#include <iostream>
#include <vector>
#include <map>
#include <set>
#include <queue>
#include <list>
#include <cassert>
#include <algorithm>

typedef std::map<std::string, int> NameIndex;

struct ResolveDef {
  std::string name;
  Location location;
  std::unique_ptr<Expr> expr;
  std::set<int> edges; // edges: things this name uses
  int index, lowlink, onstack; // Tarjan SCC variables
  ResolveDef(const std::string &name_, const Location &location_, std::unique_ptr<Expr>&& expr_)
   : name(name_), location(location_), expr(std::move(expr_)) { }
};

struct SCCState {
  std::vector<ResolveDef> *defs;
  std::vector<int> *levelmap;
  std::vector<int> S;
  DefBinding *binding;
  int index;
  int level;
};

static void SCC(SCCState &state, unsigned vi) {
  ResolveDef &v = (*state.defs)[vi];

  v.index = state.index;
  v.lowlink = state.index;
  ++state.index;

  state.S.push_back(vi);
  v.onstack = 1;

  for (auto wi : v.edges) {
    ResolveDef &w = (*state.defs)[wi];
    if ((*state.levelmap)[wi] != state.level) continue;

    if (w.index == -1 && w.expr->type == &Lambda::type) {
      SCC(state, wi);
      v.lowlink = std::min(v.lowlink, w.lowlink);
    } else if (w.onstack) {
      v.lowlink = std::min(v.lowlink, w.index);
    }
  }

  if (v.lowlink == v.index) {
    unsigned wi, scc_id = state.binding->fun.size();
    do {
      wi = state.S.back();
      ResolveDef &w = (*state.defs)[wi];
      state.S.pop_back();
      w.onstack = 0;
      auto out = state.binding->order.insert(std::make_pair(w.name, DefBinding::OrderValue(w.location, state.binding->fun.size() + state.binding->val.size())));
      assert (out.second);
      state.binding->fun.emplace_back(static_cast<Lambda*>(w.expr.release()));
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

static std::string addanon(const std::string &x, bool anon) {
  if (anon) {
    return x + ".anon";
  } else {
    return x;
  }
}

static std::string trim(const std::string &x) {
  size_t space = x.find(' ');
  bool keep = space == std::string::npos;
  return keep ? x : x.substr(space+1);
}

static std::unique_ptr<Expr> fracture_binding(const Location &location, std::vector<ResolveDef> &defs, std::unique_ptr<Expr> body) {
  // Bellman-Ford algorithm, run for longest path
  // if f uses [yg], then d[f] must be <= d[yg]
  // if x uses [yg], then d[x] must be <= d[yg]+1
  // if we ever find a d[_] > n, there is an illegal loop

  std::vector<int> d(defs.size(), 0), p(defs.size(), -1);
  std::queue<RelaxedVertex> q;

  for (int i = 0; i < (int)defs.size(); ++i) {
    if (!defs[i].expr) return nullptr;
    q.push(RelaxedVertex(i, 0));
  }

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
        std::cerr << "  " << defs[i].name << " at " << defs[i].expr->location.file() << std::endl;
        i = p[i];
      } while (i != j);
      return nullptr;
    }
    int w = def.expr->type == &Lambda::type ? 0 : 1;
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
      if (defs[j].expr->type != &Lambda::type) {
        auto out = bind->order.insert(std::make_pair(defs[j].name, DefBinding::OrderValue(defs[j].location, bind->val.size())));
        assert (out.second);
        bind->val.emplace_back(std::move(defs[j].expr));
        defs[j].index = 0;
      } else {
        defs[j].index = -1;
      }
      defs[j].onstack = 0;
    }
    SCCState state;
    state.defs = &defs;
    state.levelmap = &d;
    state.binding = bind.get();
    state.level = i;
    state.index = 0;
    for (auto j : levels[i])
      if (defs[j].index == -1 && defs[j].expr->type == &Lambda::type)
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

static VarRef *rebind_subscribe(ResolveBinding *binding, const Location &location, const std::string &name) {
  ResolveBinding *iter;
  for (iter = binding; iter; iter = iter->parent) {
    std::string pub = "publish " + std::to_string(iter->depth) + " " + name;
    if (reference_map(iter, pub)) return new VarRef(location, pub);
  }
  // nil
  return new VarRef(location, "Nil");
}

static void chain_publish(ResolveBinding *binding, DefMap::Pubs &pubs, int &chain) {
  for (auto &i : pubs) {
    std::string name = "publish " + std::to_string(binding->depth) + " " + i.first;
    for (auto j = i.second.rbegin(); j != i.second.rend(); ++j) {
      Location l = j->body->location;
      Expr *tail;
      NameIndex::iterator pub;
      if ((pub = binding->index.find(name)) == binding->index.end()) {
        tail = rebind_subscribe(binding, l, i.first);
      } else {
        std::string name = "publish " + std::to_string(binding->depth) + " " + std::to_string(++chain) + " " + i.first;
        tail = new VarRef(l, name);
        binding->index[name] = pub->second;
        binding->defs[pub->second].name = std::move(name);
      }
      binding->index[name] = binding->defs.size();
      binding->defs.emplace_back(name, j->location,
        std::unique_ptr<Expr>(new App(l, new App(l,
          new VarRef(l, "binary ++"),
          j->body.release()), tail)));
    }
  }
}

struct PatternTree {
  std::shared_ptr<Sum> sum; // nullptr if unexpanded
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
  if (name.compare(0, 7, "binary ") == 0) {
    op_type q = op_precedence(name.c_str() + 7);
    if (q.p < p) os << "(";
    children[0].format(os, q.p + !q.l);
    if (name[7] != ',') os << " ";
    os << name.c_str() + 7 << " ";
    children[1].format(os, q.p + q.l);
    if (q.p < p) os << ")";
  } else if (name.compare(0, 6, "unary ") == 0) {
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
  bool guard;

  PatternRef(const Location &location_) : location(location_), uses(0) { }
  PatternRef(PatternRef &&other) = default;
  PatternRef & operator = (PatternRef &&other) = default;
};

// assumes a detail <= b
static std::shared_ptr<Sum> find_mismatch(std::vector<int> &path, const PatternTree &a, const PatternTree &b) {
  if (!a.sum) return b.sum;
  for (size_t i = 0; i < a.children.size(); ++i) {
    path.push_back(i);
    std::shared_ptr<Sum> out = find_mismatch(path, a.children[i], b.children[i]);
    if (out) return out;
    path.pop_back();
  }
  return nullptr;
}

static Expr *fill_pattern(Expr *expr, const PatternTree &a, const PatternTree &b) {
  if (b.var >= 0) {
    expr = new App(expr->location,
      expr,
      new VarRef(expr->location, "_ a" + std::to_string(a.var)));
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
static std::unique_ptr<Expr> expand_patterns(const Location &location, const std::string &fnname, std::vector<PatternRef> &patterns) {
  PatternRef &prototype = patterns[0];
  if (patterns.size() == 1) {
    std::cerr << "Non-exhaustive match at " << prototype.location.file()
      << "; missing: " << prototype.tree << std::endl;
    return nullptr;
  }
  std::vector<int> expand;
  std::shared_ptr<Sum> sum = find_mismatch(expand, prototype.tree, patterns[1].tree);
  if (sum) {
    std::unique_ptr<DefMap> map(new DefMap(location));
    Expr *body = new Lambda(location, "_", new Destruct(location, sum));
    for (size_t i = 0; i < sum->members.size(); ++i) body = new Lambda(location, "_", body);
    map->body = std::unique_ptr<Expr>(body);
    for (size_t c = 0; c < sum->members.size(); ++c) {
      Constructor &cons = sum->members[c];
      std::string cname = "_ c" + std::to_string(c);
      map->body = std::unique_ptr<Expr>(new App(location,
        map->body.release(),
        new VarRef(location, cname)));
      std::vector<PatternRef> bucket;
      int args = cons.ast.args.size();
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
            << " but is used in pattern at " << p->location.file()
            << "." << std::endl;
          return nullptr;
        } else if (t->sum && t->cons == (int)c) {
          bucket.emplace_back(std::move(*p));
          p->index = -2;
        }
      }
      std::unique_ptr<DefMap> rmap(new DefMap(location));
      rmap->body = expand_patterns(location, fnname, bucket);
      if (!rmap->body) return nullptr;
      for (size_t i = args; i > 0; --i) {
        auto out = rmap->map.insert(std::make_pair("_ a" + std::to_string(--var),
          DefMap::Value(LOCATION, std::unique_ptr<Expr>(new Get(LOCATION, sum, &cons, i-1)))));
        assert (out.second);
      }
      Lambda *lam = new Lambda(location, "_", rmap.release());
      lam->fnname = fnname;
      auto out = map->map.insert(std::make_pair(cname, DefMap::Value(LOCATION, std::unique_ptr<Expr>(lam))));
      assert (out.second);
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
    map->body = std::unique_ptr<Expr>(new App(location,
      map->body.release(),
      new VarRef(location, "_ a" + std::to_string(
        get_expansion(&prototype.tree, expand)->var))));
    return std::unique_ptr<Expr>(map.release());
  } else {
    PatternRef &p = patterns[1];
    ++p.uses;
    std::unique_ptr<Expr> guard_true(fill_pattern(
      new App(location,
        new VarRef(location, "_ f" + std::to_string(p.index)),
        new VarRef(location, "_ a0")),
      prototype.tree, p.tree));
    if (!p.guard) {
      return guard_true;
    } else {
      PatternRef save(std::move(patterns[1]));
      patterns.erase(patterns.begin()+1);
      std::unique_ptr<Expr> guard_false(expand_patterns(location, fnname, patterns));
      patterns.emplace(patterns.begin()+1, std::move(save));
      if (!guard_false) return nullptr;
      std::unique_ptr<Expr> guard(fill_pattern(
        new App(location,
          new VarRef(location, "_ g" + std::to_string(p.index)),
          new VarRef(location, "_ a0")),
        prototype.tree, p.tree));
      Match *match = new Match(location);
      match->args.emplace_back(std::move(guard));
      match->patterns.emplace_back(AST(location, "True"),  guard_true .release(), nullptr);
      match->patterns.emplace_back(AST(location, "False"), guard_false.release(), nullptr);
      return std::unique_ptr<Expr>(match);
    }
  }
}

static PatternTree cons_lookup(ResolveBinding *binding, std::unique_ptr<Expr> &expr, std::unique_ptr<Expr> &guard, const AST &ast, std::shared_ptr<Sum> multiarg) {
  PatternTree out;
  if (ast.name == "_") {
    // no-op; unbound
  } else if (!ast.name.empty() && Lexer::isLower(ast.name.c_str())) {
    Lambda *lambda = new Lambda(expr->location, ast.name, expr.release());
    if (ast.name.compare(0, 3, "_ k") != 0) lambda->token = ast.token;
    expr = std::unique_ptr<Expr>(lambda);
    guard = std::unique_ptr<Expr>(new Lambda(expr->location, ast.name, guard.release()));
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
          while (cons->type == &Lambda::type)
            cons = static_cast<Lambda*>(cons)->body.get();
          if (cons->type == &Construct::type) {
            Construct *c = static_cast<Construct*>(cons);
            out.sum = c->sum;
            out.cons = c->cons->index;
          }
        }
      }
    }
    if (ast.name.empty()) out.sum = multiarg;
    if (!out.sum) {
      std::cerr << "Constructor " << ast.name
        << " in pattern match not found at " << ast.token.file()
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
        << " at " << ast.region.text()
        << "." << std::endl;
      out.sum = 0;
      out.var = 0;
    } else {
      for (auto a = ast.args.rbegin(); a != ast.args.rend(); ++a)
        out.children.push_back(cons_lookup(binding, expr, guard, *a, 0));
      std::reverse(out.children.begin(), out.children.end());
    }
  }
  return out;
}

static std::unique_ptr<Expr> flat_def1(const Location &location, const std::string &prefix, const std::vector<std::unique_ptr<Expr> >& args, std::unique_ptr<Expr>&& out) {
  for (unsigned i = 0; i < args.size(); ++i) {
    if (!args[i]) continue;
    out = std::unique_ptr<Expr>(new Lambda(location, prefix + std::to_string(i), out.release()));
  }
  return std::move(out);
}

static std::unique_ptr<Expr> flat_def2(const Location &location, std::vector<std::unique_ptr<Expr> >&& args, std::unique_ptr<Expr>&& out) {
  for (auto it = args.rbegin(); it != args.rend(); ++it) {
    if (!*it) continue;
    out = std::unique_ptr<Expr>(new App(location, out.release(), it->release()));
  }
  return std::move(out);
}

static Lambda *case_name(Lambda *l, const std::string &fnname) {
  Lambda *x;
  for (x = l; x->body; x = static_cast<Lambda*>(x->body.get()))
    if (x->body->type != &Lambda::type) break;
  x->fnname = fnname;
  return l;
}

static std::unique_ptr<Expr> rebind_match(const std::string &fnname, ResolveBinding *binding, std::unique_ptr<Match> match) {
  std::vector<PatternRef> patterns;
  std::shared_ptr<Sum> multiarg = std::make_shared<Sum>(AST(LOCATION));
  multiarg->members.emplace_back(AST(LOCATION));

  std::vector<PatternTree> children;
  for (int index = 0; index < (int)match->args.size(); ++index) {
    children.emplace_back(index);
    multiarg->members.front().ast.args.emplace_back(AST(LOCATION));
  }

  patterns.emplace_back(match->location);
  PatternRef &prototype = patterns.front();
  prototype.uses = 1;
  prototype.index = match->args.size();
  prototype.guard = false;

  if (prototype.index == 1) {
    prototype.tree = std::move(children.front());
  } else {
    prototype.tree.children = std::move(children);
    prototype.tree.sum = multiarg;
  }

  int f = 0;
  bool ok = true;
  std::vector<std::unique_ptr<Expr> > cases, guards;
  cases.resize(match->patterns.size());
  guards.resize(match->patterns.size());
  for (auto &p : match->patterns) {
    bool guard = static_cast<bool>(p.guard);
    patterns.emplace_back(p.expr->location);
    patterns.back().index = f;
    patterns.back().guard = static_cast<bool>(guard);
    patterns.back().tree = cons_lookup(binding, p.expr, p.guard, p.pattern, multiarg);
    ok &= !patterns.front().tree.sum || patterns.back().tree.sum;
    std::string cname = match->patterns.size() == 1 ? fnname : fnname + ".case"  + std::to_string(f);
    std::string gname = match->patterns.size() == 1 ? fnname : fnname + ".guard"  + std::to_string(f);
    cases[f] = std::unique_ptr<Expr>(case_name(new Lambda(p.expr->location, "_", p.expr.release()), cname));
    if (guard)
      guards[f] = std::unique_ptr<Expr>(case_name(new Lambda(p.guard->location, "_", p.guard.release()), gname));
    ++f;
  }
  if (!ok) return nullptr;
  auto body = expand_patterns(match->location, fnname, patterns);
  if (!body) return nullptr;
  for (auto &p : patterns) {
    if (!p.uses) {
      std::cerr << "Pattern unreachable in match at " << p.location.text() << std::endl;
      return nullptr;
    }
  }
  // Bind cases to pattern destruct tree (not a DefMap so as to forbid generalization)
  body = flat_def1(match->location, "_ a", match->args, std::move(body));
  body = flat_def1(match->location, "_ f", cases, std::move(body));
  body = flat_def1(match->location, "_ g", guards, std::move(body));
  case_name(static_cast<Lambda*>(body.get()), fnname);
  body = flat_def2(match->location, std::move(guards), std::move(body));
  body = flat_def2(match->location, std::move(cases), std::move(body));
  body = flat_def2(match->location, std::move(match->args), std::move(body));
  return body;
}

static std::unique_ptr<Expr> fracture(bool anon, const std::string& name, std::unique_ptr<Expr> expr, ResolveBinding *binding) {
  if (expr->type == &VarRef::type) {
    VarRef *ref = static_cast<VarRef*>(expr.get());
    // don't fail if unbound; leave that for the second pass
    rebind_ref(binding, ref->name);
    return expr;
  } else if (expr->type == &Subscribe::type) {
    Subscribe *sub = static_cast<Subscribe*>(expr.get());
    VarRef *out = rebind_subscribe(binding, sub->location, sub->name);
    out->flags |= FLAG_AST;
    return std::unique_ptr<Expr>(out);
  } else if (expr->type == &App::type) {
    App *app = static_cast<App*>(expr.get());
    app->fn  = fracture(true, name, std::move(app->fn),  binding);
    app->val = fracture(true, name, std::move(app->val), binding);
    return expr;
  } else if (expr->type == &Lambda::type) {
    Lambda *lambda = static_cast<Lambda*>(expr.get());
    ResolveBinding lbinding;
    lbinding.parent = binding;
    lbinding.current_index = 0;
    lbinding.prefix = -1;
    lbinding.depth = binding->depth + 1;
    lbinding.index[lambda->name] = 0;
    lbinding.defs.emplace_back(lambda->name, LOCATION, nullptr);
    if (lambda->body->type == &Lambda::type) {
      lambda->body = fracture(anon, name, std::move(lambda->body), &lbinding);
    } else {
      if (lambda->fnname.empty()) {
        lambda->fnname = addanon(name, anon);
      } else if (lambda->fnname[0] == ' ') {
        lambda->fnname = name + lambda->fnname.substr(1);
      }
      lambda->body = fracture(false, lambda->fnname, std::move(lambda->body), &lbinding);
    }
    return expr;
  } else if (expr->type == &Match::type) {
    std::unique_ptr<Match> m(static_cast<Match*>(expr.release()));
    auto out = rebind_match(name, binding, std::move(m));
    if (!out) return out;
    out->flags |= FLAG_AST;
    return fracture(anon, name, std::move(out), binding);
  } else if (expr->type == &DefMap::type) {
    DefMap *def = static_cast<DefMap*>(expr.get());
    ResolveBinding dbinding;
    dbinding.parent = binding;
    dbinding.prefix = -1;
    dbinding.depth = binding->depth + 1;
    int chain = 0;
    for (auto &i : def->map) {
      dbinding.index[i.first] = dbinding.defs.size();
      dbinding.defs.emplace_back(i.first, i.second.location, std::move(i.second.body));
    }
    chain_publish(&dbinding, def->pub, chain);
    dbinding.current_index = 0;
    for (auto &i : dbinding.defs) {
      i.expr = fracture(false, addanon(name, anon) + "." + trim(i.name), std::move(i.expr), &dbinding);
      ++dbinding.current_index;
    }
    dbinding.current_index = -1;
    std::unique_ptr<Expr> body = fracture(true, name, std::move(def->body), &dbinding);
    auto out = fracture_binding(def->location, dbinding.defs, std::move(body));
    if ((def->flags & FLAG_AST) != 0)
      out->flags |= FLAG_AST;
    return out;
  } else if (expr->type == &Top::type) {
    Top *top = static_cast<Top*>(expr.get());
    ResolveBinding tbinding;
    tbinding.parent = binding;
    tbinding.prefix = 0;
    tbinding.depth = binding ? binding->depth+1 : 0;
    int chain = 0;
    for (auto &b : top->defmaps) {
      for (auto &i : b->map) {
        std::string name;
        Top::DefOrder::iterator glob;
        // If this file defines the global, put it at the global name; otherwise, localize the name
        if ((glob = top->globals.find(i.first)) != top->globals.end() && glob->second == tbinding.prefix) {
          name = i.first;
        } else {
          name = std::to_string(tbinding.prefix) + " " + i.first;
        }
        tbinding.index[name] = tbinding.defs.size();
        tbinding.defs.emplace_back(name, i.second.location, std::move(i.second.body));
      }
      chain_publish(&tbinding, b->pub, chain);
      ++tbinding.prefix;
    }

    tbinding.current_index = 0;
    tbinding.prefix = 0;
    for (auto &b : top->defmaps) {
      int total = b->map.size();
      for (auto &j : b->pub) total += j.second.size();
      for (int i = 0; i < total; ++i) {
        ResolveDef &def = tbinding.defs[tbinding.current_index];
        def.expr = fracture(false, trim(def.name), std::move(def.expr), &tbinding);
        ++tbinding.current_index;
      }
      ++tbinding.prefix;
    }
    tbinding.current_index = -1;
    std::unique_ptr<Expr> body = fracture(true, name, std::move(top->body), &tbinding);
    return fracture_binding(top->location, tbinding.defs, std::move(body));
  } else {
    // Literal/Prim/Construct/Destruct/Get
    return expr;
  }
}

struct NameRef {
  int index;
  int def;
  Location target;
  Lambda *lambda;
  TypeVar *var;
  NameRef() : index(-1), def(0), target(LOCATION), lambda(nullptr), var(nullptr) { }
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
    DefBinding::Order::iterator i;
    if (lambda && lambda->name == x) {
      out.index = 0;
      out.def = 0;
      out.var = &lambda->typeVar[0];
      out.target = lambda->token;
    } else if (binding && (i = binding->order.find(x)) != binding->order.end()) {
      int idx = i->second.index;
      out.def = idx < generalized;
      out.target = i->second.location;
      if (idx < (int)binding->val.size()) {
        auto x = binding->val[idx].get();
        out.index = idx;
        out.var = x?&x->typeVar:0;
      } else {
        auto x = binding->fun[idx-binding->val.size()].get();
        out.index = 0;
        out.var = x?&x->typeVar:0;
        out.lambda = x;
        if (idx >= generalized) // recursive use
          x->flags |= FLAG_RECURSIVE;
      }
    } else if (next) {
      out = next->find(x);
      if (out.index >= 0) {
        if (binding)
          out.index += binding->val.size();
        if (lambda)
          ++out.index;
      }
    } else {
      out.index = -1;
    }
    return out;
  }
};

struct FnErrorMessage : public TypeErrorMessage  {
  const Location *lf;
  FnErrorMessage(const Location *lf_) : lf(lf_) { }
  void formatA(std::ostream &os) const { os << "Type error; expression " << lf->text() << " has type"; }
  void formatB(std::ostream &os) const { os << "but is used as a function and must have function type"; }
};

struct ArgErrorMessage : public TypeErrorMessage {
  const Location *lf, *la;
  const char *arg;
  ArgErrorMessage(const Location *lf_, const Location *la_, const char *arg_) : lf(lf_), la(la_), arg(arg_) { }
  void formatA(std::ostream &os) const {
    os << "Type error; function " << lf->text() << " expected argument";
    if (arg && arg[0] && !strchr(arg, ' ') && strcmp(arg, "_")) os << " '" << arg << "'";
    os << " of type";
  }
  void formatB(std::ostream &os) const { os << "but was supplied argument " << la->text() << " of type"; }
};

struct RecErrorMessage : public TypeErrorMessage  {
  const Location *lf;
  RecErrorMessage(const Location *lf_) : lf(lf_) { }
  void formatA(std::ostream &os) const { os << "Type error; recursive use of " << lf->text() << " requires return type"; }
  void formatB(std::ostream &os) const { os << "but the function body actually returns type"; }
};

static bool explore(Expr *expr, const PrimMap &pmap, NameBinding *binding) {
  if (!expr) return false; // failed fracture
  expr->typeVar.setDOB();
  if (expr->type == &VarRef::type) {
    VarRef *ref = static_cast<VarRef*>(expr);
    NameRef pos;
    if ((pos = binding->find(ref->name)).index == -1) {
      std::cerr << "Variable reference '" << ref->name << "' is unbound at "
        << ref->location.file() << std::endl;
      return false;
    }
    ref->index = pos.index;
    ref->lambda = pos.lambda;
    ref->target = pos.target;
    if (!pos.var) return true;
    if (pos.def) {
      TypeVar temp;
      pos.var->clone(temp);
      return ref->typeVar.unify(temp, &ref->location);
    } else {
      if (pos.lambda) ref->flags |= FLAG_RECURSIVE;
      return ref->typeVar.unify(*pos.var, &ref->location);
    }
  } else if (expr->type == &App::type) {
    App *app = static_cast<App*>(expr);
    binding->open = false;
    bool f = explore(app->fn .get(), pmap, binding);
    bool a = explore(app->val.get(), pmap, binding);
    FnErrorMessage fnm(&app->fn->location);
    bool t = f && app->fn->typeVar.unify(TypeVar(FN, 2), &fnm);
    ArgErrorMessage argm(&app->fn->location, &app->val->location, t?app->fn->typeVar.getTag(0):0);
    bool ta = t && a && app->fn->typeVar[0].unify(app->val->typeVar, &argm);
    bool tr = t && app->fn->typeVar[1].unify(app->typeVar, &app->location);
    return f && a && t && ta && tr;
  } else if (expr->type == &Lambda::type) {
    Lambda *lambda = static_cast<Lambda*>(expr);
    bool t = lambda->typeVar.unify(TypeVar(FN, 2), &lambda->location);
    if (t && lambda->name != "_" && lambda->name.find(' ') == std::string::npos)
      lambda->typeVar.setTag(0, lambda->name.c_str());
    NameBinding bind(binding, lambda);
    bool out = explore(lambda->body.get(), pmap, &bind);
    RecErrorMessage recm(&lambda->body->location);
    bool tr = t && out && lambda->typeVar[1].unify(lambda->body->typeVar, &recm);
    return out && t && tr;
  } else if (expr->type == &DefBinding::type) {
    DefBinding *def = static_cast<DefBinding*>(expr);
    binding->open = false;
    NameBinding bind(binding, def);
    bool ok = true;
    for (auto &i : def->val)
      ok = explore(i.get(), pmap, binding) && ok;
    for (unsigned i = 0; i < def->fun.size(); ++i) {
      def->fun[i]->typeVar.setDOB();
      for (unsigned j = i+1; j < def->fun.size() && i == def->scc[j]; ++j)
        if (def->fun[j]) def->fun[j]->typeVar.setDOB(def->fun[i]->typeVar);
      bind.generalized = def->val.size() + def->scc[i];
      ok = explore(def->fun[i].get(), pmap, &bind) && ok;
    }
    bind.generalized = def->val.size() + def->fun.size();
    ok = explore(def->body.get(), pmap, &bind) && ok;
    ok = ok && def->typeVar.unify(def->body->typeVar, &def->location) && ok;
    return ok;
  } else if (expr->type == &Literal::type) {
    Literal *lit = static_cast<Literal*>(expr);
    return lit->typeVar.unify(*lit->litType, &lit->location);
  } else if (expr->type == &Construct::type) {
    Construct *cons = static_cast<Construct*>(expr);
    bool ok = cons->typeVar.unify(
      TypeVar(cons->sum->name.c_str(), cons->sum->args.size()));
    std::map<std::string, TypeVar*> ids;
    for (size_t i = 0; i < cons->sum->args.size(); ++i)
      ids[cons->sum->args[i]] = &cons->typeVar[i];
    if (binding->lambda) {
      NameBinding *iter = binding;
      std::vector<AST> &v = cons->cons->ast.args;
      for (size_t i = v.size(); i; --i) {
        TypeVar &ty = iter->lambda->typeVar;
        ok = v[i-1].unify(ty[0], ids) && ok;
        if (!v[i-1].tag.empty()) ty.setTag(0, v[i-1].tag.c_str());
        iter = iter->next;
      }
    } else {
      DefBinding::Values &vals = binding->binding->val;
      std::vector<AST> &v = cons->cons->ast.args;
      size_t num = v.size();
      for (size_t i = 0; i < num; ++i)
        ok = v[num-1-i].unify(vals[i]->typeVar, ids) && ok;
    }
    return ok;
  } else if (expr->type == &Destruct::type) {
    Destruct *des = static_cast<Destruct*>(expr);
    // (typ => b) => (typ => b) => typ => b
    TypeVar &typ = binding->lambda->typeVar[0];
    bool ok = typ.unify(
      TypeVar(des->sum->name.c_str(), des->sum->args.size()));
    std::map<std::string, TypeVar*> ids;
    for (size_t i = 0; i < des->sum->args.size(); ++i)
      ids[des->sum->args[i]] = &typ[i];
    NameBinding *iter = binding;
    for (size_t i = des->sum->members.size(); i; --i) {
      iter = iter->next;
      TypeVar &arg = iter->lambda->typeVar[0];
      if (!arg.unify(TypeVar(FN, 2))) { ok = false; break; }
      ok = arg[0].unify(typ) && ok;
      ok = des->typeVar.unify(arg[1]) && ok;
    }
    return ok;
  } else if (expr->type == &Prim::type) {
    Prim *prim = static_cast<Prim*>(expr);
    std::vector<TypeVar*> args;
    for (NameBinding *iter = binding; iter && iter->open && iter->lambda; iter = iter->next)
      args.push_back(&iter->lambda->typeVar[0]);
    std::reverse(args.begin(), args.end());
    prim->args = args.size();
    PrimMap::const_iterator i = pmap.find(prim->name);
    if (i == pmap.end()) {
      std::cerr << "Primitive reference "
        << prim->name << " is unbound at "
        << prim->location.file() << std::endl;
      return false;
    } else {
      prim->pflags = i->second.flags;
      prim->fn   = i->second.fn;
      prim->data = i->second.data;
      bool ok = i->second.type(args, &prim->typeVar);
      if (!ok) std::cerr << "Primitive reference "
        << prim->name << " has wrong type signature at "
        << prim->location.file() << std::endl;
      return ok;
    }
  } else if (expr->type == &Get::type) {
    Get *get = static_cast<Get*>(expr);
    while (!binding->lambda) binding = binding->next;
    TypeVar &typ = binding->lambda->typeVar[0];
    bool ok = typ.unify(
      TypeVar(get->sum->name.c_str(), get->sum->args.size()));
    std::map<std::string, TypeVar*> ids;
    for (size_t i = 0; i < get->sum->args.size(); ++i)
      ids[get->sum->args[i]] = &typ[i];
    ok = get->cons->ast.args[get->index].unify(get->typeVar, ids) && ok;
    return ok;
  } else {
    assert(0 /* unreachable */);
    return false;
  }
}

std::unique_ptr<Expr> bind_refs(std::unique_ptr<Top> top, const PrimMap &pmap) {
  NameBinding bottom;
  std::unique_ptr<Expr> out = fracture(false, "", std::move(top), 0);
  if (out && !explore(out.get(), pmap, &bottom)) out.reset();
  return out;
}
