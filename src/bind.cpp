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
  int depth;
  NameIndex index;
  std::vector<ResolveDef> defs;
  std::vector<Symbols*> symbols;

  ResolveBinding(ResolveBinding *parent_)
   : parent(parent_), current_index(0), depth(parent_?parent_->depth+1:0) { }

  void qualify_def(std::string &name) {
    SymbolSource *override = nullptr;
    for (auto sym : symbols) {
      auto it = sym->defs.find(name);
      if (it != sym->defs.end()) {
        if (override) {
          std::cerr << "Ambiguous import of definition '" << name
            << "' from " << override->location.text()
            << " and " << it->second.location.text() << std::endl;
        }
        override = &it->second;
      }
    }
    if (override) name = override->qualified;
  }

  bool qualify_topic(std::string &name) {
    SymbolSource *override = nullptr;
    for (auto sym : symbols) {
      auto it = sym->topics.find(name);
      if (it != sym->topics.end()) {
        if (override) {
          std::cerr << "Ambiguous import of topic '" << name
            << "' from " << override->location.text()
            << " and " << it->second.location.text() << std::endl;
        }
        override = &it->second;
      }
    }
    if (override) name = override->qualified;
    return override;
  }

  bool qualify_type(std::string &name) {
    SymbolSource *override = nullptr;
    for (auto sym : symbols) {
      auto it = sym->types.find(name);
      if (it != sym->types.end()) {
        if (override) {
          std::cerr << "Ambiguous import of type '" << name
            << "' from " << override->location.text()
            << " and " << it->second.location.text() << std::endl;
        }
        override = &it->second;
      }
    }
    if (override) name = override->qualified;
    return override;
  }
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
    iter->qualify_def(name);
    if (reference_map(iter, name)) return true;
  }
  return false;
}

static VarRef *rebind_subscribe(ResolveBinding *binding, const Location &location, std::string &name) {
  ResolveBinding *iter;
  for (iter = binding; iter; iter = iter->parent) {
    if (iter->qualify_topic(name)) break;
  }
  if (!iter) {
    std::cerr << "Subscribe of '" << name
      << "' is to a non-existent topic at "
      << location.file() << std::endl;
  }
  return new VarRef(location, "topic " + name);
}

static std::string rebind_publish(ResolveBinding *binding, const Location &location, const std::string &key) {
  std::string name(key);
  ResolveBinding *iter;
  for (iter = binding; iter; iter = iter->parent) {
    if (iter->qualify_topic(name)) break;
  }
  if (!iter) {
    std::cerr << "Publish to '" << name
      << "' is to a non-existent topic at "
      << location.file() << std::endl;
  }
  return name;
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
        auto out = rmap->defs.insert(std::make_pair("_ a" + std::to_string(--var),
          DefValue(LOCATION, std::unique_ptr<Expr>(new Get(LOCATION, sum, &cons, i-1)))));
        assert (out.second);
      }
      Lambda *lam = new Lambda(location, "_", rmap.release());
      lam->fnname = fnname;
      auto out = map->defs.insert(std::make_pair(cname, DefValue(LOCATION, std::unique_ptr<Expr>(lam))));
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
      match->patterns.emplace_back(AST(location, "True@wake"),  guard_true .release(), nullptr);
      match->patterns.emplace_back(AST(location, "False@wake"), guard_false.release(), nullptr);
      return std::unique_ptr<Expr>(match);
    }
  }
}

static PatternTree cons_lookup(ResolveBinding *binding, std::unique_ptr<Expr> &expr, std::unique_ptr<Expr> &guard, AST &ast, std::shared_ptr<Sum> multiarg) {
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
      iter->qualify_def(ast.name);
      auto it = iter->index.find(ast.name);
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

struct SymMover {
  std::pair<const std::string, SymbolSource> &sym;
  const char *kind;
  std::string def;
  bool warn;
  Package *package;

  SymMover(Top &top, std::pair<const std::string, SymbolSource> &sym_, const char *kind_) : sym(sym_), kind(kind_) {
    size_t at = sym.second.qualified.find_first_of('@');
    def.assign(sym.second.qualified, 0, at);

    std::string pkg(sym.second.qualified, at+1);
    auto it = top.packages.find(pkg);
    if (it == top.packages.end()) {
      warn = false;
      package = nullptr;
      std::cerr << "Import of " << kind << " '" << def
        << "' is from non-existent package '" << pkg
        << "' at " << sym.second.location.text() << std::endl;
    } else {
      warn = true;
      package = it->second.get();
    }
  }

  ~SymMover() {
    if (warn) {
      std::cerr << "Import of " << kind << " '" << def
        << "' from package '" << package->name
        << "' is not exported at " << sym.second.location.text() << std::endl;
    }
  }

  void consider(const Symbols::SymbolMap &from, Symbols::SymbolMap &to) {
    if (def.compare(0, 3, "op ") == 0) {
      auto unary = from.find("unary " + def.substr(3));
      if (unary != from.end()) {
        to.insert(std::make_pair(
          "unary " + sym.first.substr(3),
          sym.second.clone(unary->second.qualified)));
        warn = false;
      }
      auto binary = from.find("binary " + def.substr(3));
      if (binary != from.end()) {
        to.insert(std::make_pair(
          "binary " + sym.first.substr(3),
          sym.second.clone(binary->second.qualified)));
        warn = false;
      }
    } else {
      auto it = from.find(def);
      if (it != from.end()) {
        to.insert(std::make_pair(sym.first, sym.second.clone(it->second.qualified)));
        warn = false;
      }
    }
  }

  void defs  (Symbols::SymbolMap &defs)   { if (package) consider(package->exports.defs,   defs);   }
  void types (Symbols::SymbolMap &types)  { if (package) consider(package->exports.types,  types);  }
  void topics(Symbols::SymbolMap &topics) { if (package) consider(package->exports.topics, topics); }
};

static std::vector<Symbols*> process_import(Top &top, Imports &imports, Location &location) {
  Symbols::SymbolMap mixed(std::move(imports.mixed));
  for (auto &d : mixed) {
    SymMover mover(top, d, "symbol");
    mover.defs  (imports.defs);
    mover.types (imports.types);
    mover.topics(imports.topics);
  }

  Symbols::SymbolMap defs(std::move(imports.defs));
  for (auto &d : defs) {
    SymMover mover(top, d, "definition");
    mover.defs(imports.defs);
  }

  Symbols::SymbolMap topics(std::move(imports.topics));
  for (auto &d : topics) {
    SymMover mover(top, d, "topic");
    mover.topics(imports.topics);
  }

  Symbols::SymbolMap types(std::move(imports.types));
  for (auto &d : types) {
    SymMover mover(top, d, "type");
    mover.types(imports.types);
  }

  std::vector<Symbols*> out;
  for (auto &p : imports.import_all) {
    auto it = top.packages.find(p);
    if (it == top.packages.end()) {
      std::cerr << "Full import from non-existent package '" << p
        << "' at " << location.text() << std::endl;
    } else {
      out.push_back(&it->second->exports);
    }
  }
  out.push_back(&imports);
  return out;
}

static bool qualify_type(ResolveBinding *binding, std::string &name, const Location &location) {
  ResolveBinding *iter;
  for (iter = binding; iter; iter = iter->parent) {
    if (iter->qualify_type(name)) break;
  }

  if (iter) {
    return true;
  } else {
    std::cerr << "Type signature '" << name
      << "' refers to a non-existent type "
      << location.file() << std::endl;
    return false;
  }
}

static bool qualify_type(ResolveBinding *binding, AST &type) {
  // Type variables do not get qualified
  if (Lexer::isLower(type.name.c_str())) return true;
  bool ok = qualify_type(binding, type.name, type.token);
  for (auto &x : type.args)
    if (!qualify_type(binding, x))
      ok = false;
  return ok;
}

static std::unique_ptr<Expr> fracture(Top &top, bool anon, const std::string &name, std::unique_ptr<Expr> expr, ResolveBinding *binding) {
  if (expr->type == &VarRef::type) {
    VarRef *ref = static_cast<VarRef*>(expr.get());
    // don't fail if unbound; leave that for the second pass
    rebind_ref(binding, ref->name);
    return expr;
  } else if (expr->type == &Subscribe::type) {
    Subscribe *sub = static_cast<Subscribe*>(expr.get());
    VarRef *out = rebind_subscribe(binding, sub->location, sub->name);
    out->flags |= FLAG_AST;
    return fracture(top, true, name, std::unique_ptr<Expr>(out), binding);
  } else if (expr->type == &App::type) {
    App *app = static_cast<App*>(expr.get());
    app->fn  = fracture(top, true, name, std::move(app->fn),  binding);
    app->val = fracture(top, true, name, std::move(app->val), binding);
    return expr;
  } else if (expr->type == &Lambda::type) {
    Lambda *lambda = static_cast<Lambda*>(expr.get());
    ResolveBinding lbinding(binding);
    lbinding.index[lambda->name] = 0;
    lbinding.defs.emplace_back(lambda->name, LOCATION, nullptr);
    if (lambda->body->type == &Lambda::type) {
      lambda->body = fracture(top, anon, name, std::move(lambda->body), &lbinding);
    } else {
      if (lambda->fnname.empty()) {
        lambda->fnname = addanon(name, anon);
      } else if (lambda->fnname[0] == ' ') {
        lambda->fnname = name + lambda->fnname.substr(1);
      }
      lambda->body = fracture(top, false, lambda->fnname, std::move(lambda->body), &lbinding);
    }
    return expr;
  } else if (expr->type == &Match::type) {
    std::unique_ptr<Match> m(static_cast<Match*>(expr.release()));
    auto out = rebind_match(name, binding, std::move(m));
    if (!out) return out;
    out->flags |= FLAG_AST;
    return fracture(top, anon, name, std::move(out), binding);
  } else if (expr->type == &DefMap::type) {
    DefMap *def = static_cast<DefMap*>(expr.get());
    ResolveBinding dbinding(binding);
    dbinding.symbols = process_import(top, def->imports, def->location);
    for (auto &i : def->defs) {
      dbinding.index[i.first] = dbinding.defs.size();
      dbinding.defs.emplace_back(i.first, i.second.location, std::move(i.second.body));
    }
    for (auto &i : dbinding.defs) {
      i.expr = fracture(top, false, addanon(name, anon) + "." + trim(i.name), std::move(i.expr), &dbinding);
      ++dbinding.current_index;
    }
    dbinding.current_index = -1;
    std::unique_ptr<Expr> body = fracture(top, true, name, std::move(def->body), &dbinding);
    auto out = fracture_binding(def->location, dbinding.defs, std::move(body));
    if (out && (def->flags & FLAG_AST) != 0)
      out->flags |= FLAG_AST;
    return out;
  } else if (expr->type == &Construct::type) {
    Construct *con = static_cast<Construct*>(expr.get());
    bool ok = true;
    if (!con->sum->scoped) {
      con->sum->scoped = true;
      if (!qualify_type(binding, con->sum->name, con->sum->token))
        ok = false;
    }
    if (!con->cons->scoped) {
      con->cons->scoped = true;
      for (auto &arg : con->cons->ast.args)
        if (!qualify_type(binding, arg))
          ok = false;
    }
    if (!ok) expr.reset();
    return expr;
  } else if (expr->type == &Ascribe::type) {
    Ascribe *asc = static_cast<Ascribe*>(expr.get());
    if (qualify_type(binding, asc->signature)) {
      asc->body = fracture(top, true, name, std::move(asc->body), binding);
    } else {
      expr.reset();
    }
    return expr;
  } else if (expr->type == &Top::type) {
    ResolveBinding gbinding(nullptr);   // global mapping + qualified defines
    ResolveBinding pbinding(&gbinding); // package mapping
    ResolveBinding ibinding(&pbinding); // file import mapping
    ResolveBinding dbinding(&ibinding); // file local mapping
    size_t publish = 0;
    bool fail = false;
    for (auto &p : top.packages) {
      for (auto &f : p.second->files) {
        for (auto &d : f.content->defs) {
          gbinding.index[d.first] = gbinding.defs.size();
          gbinding.defs.emplace_back(d.first, d.second.location, std::move(d.second.body));
        }
        for (auto it = f.pubs.rbegin(); it != f.pubs.rend(); ++it) {
          auto name = "publish " + it->first + " " + std::to_string(++publish);
          gbinding.index[name] = gbinding.defs.size();
          gbinding.defs.emplace_back(name, it->second.location, std::move(it->second.body));
        }
      }
    }
    for (auto &p : top.packages) {
      for (auto &f : p.second->files) {
        for (auto &t : f.topics) {
          auto name = "topic " + t.first + "@" + p.first;
          gbinding.index[name] = gbinding.defs.size();
          gbinding.defs.emplace_back(name, t.second.location,
            std::unique_ptr<Expr>(new VarRef(t.second.location, "Nil@wake")));
        }
      }
    }
    gbinding.symbols.push_back(&top.globals);
    for (auto &p : top.packages) {
      pbinding.symbols.clear();
      pbinding.symbols.push_back(&p.second->package);
      for (auto &f : p.second->files) {
        ibinding.symbols = process_import(top, f.content->imports, f.content->location);
        dbinding.symbols.clear();
        dbinding.symbols.push_back(&f.local);
        for (size_t i = 0; i < f.content->defs.size(); ++i) {
          ResolveDef &def = gbinding.defs[gbinding.current_index];
          def.expr = fracture(top, false, trim(def.name), std::move(def.expr), &dbinding);
          ++gbinding.current_index;
        }
        for (auto it = f.pubs.rbegin(); it != f.pubs.rend(); ++it) {
          ResolveDef &def = gbinding.defs[gbinding.current_index];
          auto qualified = rebind_publish(&dbinding, def.location, it->first);
          size_t at = qualified.find('@');
          if (at != std::string::npos) {
            def.expr = fracture(top, false, trim(def.name), std::move(def.expr), &dbinding);
            ResolveDef &topicdef = gbinding.defs[gbinding.index.find("topic " + qualified)->second];
            Location &l = topicdef.expr->location;
            topicdef.expr = std::unique_ptr<Expr>(new App(l, new App(l,
              new VarRef(l, "binary ++@wake"),
              new VarRef(def.expr->location, def.name)),
              topicdef.expr.release()));
          } else {
            fail = true;
          }
          ++gbinding.current_index;
        }
        for (auto &t : f.topics) {
          if (!qualify_type(&dbinding, t.second.type))
            fail = true;
        }
      }
    }
    for (auto &p : top.packages) {
      for (auto &f : p.second->files) {
        for (auto &t : f.topics) {
          ResolveDef &def = gbinding.defs[gbinding.current_index];
          def.expr = fracture(top, false, trim(def.name), std::move(def.expr), &gbinding);
          ++gbinding.current_index;

          // Form the type required for publishes
          std::vector<AST> args;
          args.emplace_back(t.second.type); // qualified by prior pass
          AST signature(t.second.type.region, "List@wake", std::move(args));

          // Insert Ascribe requirements on all publishes
          Expr *next = nullptr;
          for (Expr *iter = def.expr.get(); iter->type == &App::type; iter = next) {
            App *app1 = static_cast<App*>(iter);
            App *app2 = static_cast<App*>(app1->fn.get());
            app2->val = std::unique_ptr<Expr>(new Ascribe(def.expr->location, AST(signature), app2->val.release()));
            next = app1->val.get();
          }

          // If the topic is empty, still force the type
          if (!next) def.expr = std::unique_ptr<Expr>(new Ascribe(def.expr->location, AST(signature), def.expr.release()));
        }
      }
    }

    Package &defp = *top.packages[top.def_package];
    gbinding.current_index = -1;
    pbinding.symbols.clear();
    ibinding.symbols.clear();
    dbinding.symbols.clear();
    dbinding.symbols.push_back(&defp.package);
    std::set<std::string> imports;
    for (auto &file : defp.files)
      for (auto &bulk : file.content->imports.import_all)
        imports.insert(bulk);
    for (auto &imp : imports)
      ibinding.symbols.push_back(&top.packages[imp]->exports);

    std::unique_ptr<Expr> body = fracture(top, true, name, std::move(top.body), &dbinding);
    auto out = fracture_binding(top.location, gbinding.defs, std::move(body));
    if (fail) out.reset();
    return out;
  } else {
    // Literal/Prim/Destruct/Get
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

struct AscErrorMessage : public TypeErrorMessage {
  const Location *body, *type;
  AscErrorMessage(const Location *body_, const Location *type_) : body(body_), type(type_) { }
  void formatA(std::ostream &os) const { os << "Type error; expression " << body->text() << " of type"; }
  void formatB(std::ostream &os) const { os << "does not match explicit type ascription at " << type->file() << " of"; }
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
  } else if (expr->type == &Ascribe::type) {
    Ascribe *asc = static_cast<Ascribe*>(expr);
    std::map<std::string, TypeVar*> ids;
    bool b = explore(asc->body.get(), pmap, binding);
    bool ts = asc->signature.unify(asc->typeVar, ids);
    AscErrorMessage ascm(&asc->body->location, &asc->signature.region);
    bool tb = asc->body->typeVar.unify(asc->typeVar, &ascm);
    return b && tb && ts;
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
  Top &topr = *top;
  std::unique_ptr<Expr> out = fracture(topr, false, "", std::move(top), 0);
  if (out && !explore(out.get(), pmap, &bottom)) out.reset();
  return out;
}

struct Contractor {
  const Top &top;
  bool warn;
  const char *kind;
  virtual Symbols::SymbolMap &member(Symbols &sym) const = 0;
  Contractor(const Top &top_, bool warn_, const char *kind_)
   : top(top_), warn(warn_), kind(kind_) {}
};

static bool contract(const Contractor &con, SymbolSource &sym) {
  // Leaves don't need contraction
  if ((sym.flags & SYM_LEAF) != 0) return true;

  size_t at = sym.qualified.find_first_of('@');
  std::string pkg(sym.qualified, at+1);
  std::string def(sym.qualified, 0, at);

  if ((sym.flags & SYM_GRAY) != 0) {
    if (con.warn) {
      std::cerr << "Re-export of '" << def
        << "', imported from '" << pkg
        << "', has cyclic definition at "
        << sym.location.text() << std::endl;
    }
    return false;
  }

  auto ip = con.top.packages.find(pkg);
  if (ip == con.top.packages.end()) {
    if (con.warn) {
      std::cerr << "Re-export of '" << def
        << "' is from non-existent package '" << pkg
        << "' at " << sym.location.text() << std::endl;
    }
    return false;
  } else {
    auto map = con.member(ip->second->exports);
    auto ie = map.find(def);
    if (ie == map.end()) {
      if (con.warn) {
        std::cerr << "Re-export of '" << def
          << "' is not exported from '" << pkg
          << "' at " << sym.location.text() << std::endl;
      }
      return false;
    }
    sym.flags |= SYM_GRAY;
    bool ok = contract(con, ie->second);
    sym.flags &= ~SYM_GRAY;
    sym.flags |= SYM_LEAF;
    sym.qualified = ie->second.qualified;
    return ok;
  }
}

struct DefContractor final : public Contractor {
  DefContractor(const Top &top, bool warn) : Contractor(top, warn, "definition") { }
  Symbols::SymbolMap &member(Symbols &sym) const override { return sym.defs; }
};

static bool contract_def(Top &top, SymbolSource &sym, bool warn) {
  DefContractor con(top, warn);
  return contract(con, sym);
}

struct TypeContractor final : public Contractor {
  TypeContractor(const Top &top, bool warn) : Contractor(top, warn, "type") { }
  Symbols::SymbolMap &member(Symbols &sym) const override { return sym.types; }
};

static bool contract_type(Top &top, SymbolSource &sym, bool warn) {
  TypeContractor con(top, warn);
  return contract(con, sym);
}

struct TopicContractor final : public Contractor {
  TopicContractor(const Top &top, bool warn) : Contractor(top, warn, "topic") { }
  Symbols::SymbolMap &member(Symbols &sym) const override { return sym.topics; }
};

static bool contract_topic(Top &top, SymbolSource &sym, bool warn) {
  TopicContractor con(top, warn);
  return contract(con, sym);
}

static bool sym_contract(Top &top, Symbols &symbols, bool warn) {
  bool ok = true;
  for (auto &d : symbols.defs)   if (!contract_def  (top, d.second, warn)) ok = false;
  for (auto &d : symbols.types)  if (!contract_type (top, d.second, warn)) ok = false;
  for (auto &d : symbols.topics) if (!contract_topic(top, d.second, warn)) ok = false;
  return ok;
}

bool flatten_exports(Top &top) {
  bool ok = true;
  for (auto &p : top.packages) {
    if (!sym_contract(top, p.second->exports, true)) ok = false;
    if (!sym_contract(top, p.second->package, false)) ok = false;
    for (auto &f : p.second->files) {
      if (!sym_contract(top, f.local, false)) ok = false;
    }
  }
  return ok;
}
