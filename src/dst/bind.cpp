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

// Open Group Base Specifications Issue 7
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include "bind.h"

#include <algorithm>
#include <cassert>
#include <list>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <vector>

#include "expr.h"
#include "parser/lexer.h"
#include "types/primfn.h"
#include "types/sums.h"
#include "util/diagnostic.h"
#include "util/fragment.h"

static CPPFile cppFile(__FILE__);

typedef std::map<std::string, int> NameIndex;

struct ResolveDef {
  std::string name;
  FileFragment fragment;
  std::unique_ptr<Expr> expr;
  std::vector<ScopedTypeVar> typeVars;
  std::set<int> edges;          // edges: things this name uses
  int index, lowlink, onstack;  // Tarjan SCC variables
  int uses;
  ResolveDef(const std::string &name_, const FileFragment &fragment_, std::unique_ptr<Expr> &&expr_,
             std::vector<ScopedTypeVar> &&typeVars_)
      : name(name_),
        fragment(fragment_),
        expr(std::move(expr_)),
        typeVars(std::move(typeVars_)),
        uses(0) {}
  ResolveDef(const std::string &name_, const FileFragment &fragment_, std::unique_ptr<Expr> &&expr_)
      : name(name_), fragment(fragment_), expr(std::move(expr_)), uses(0) {}
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
      auto out = state.binding->order.insert(std::make_pair(
          w.name, DefBinding::OrderValue(w.fragment,
                                         state.binding->fun.size() + state.binding->val.size())));
      assert(out.second);
      state.binding->fun.emplace_back(static_cast<Lambda *>(w.expr.release()));
      state.binding->funVars.emplace_back(std::move(w.typeVars));
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
  std::vector<Symbols *> symbols;

  ResolveBinding(ResolveBinding *parent_)
      : parent(parent_), current_index(0), depth(parent_ ? parent_->depth + 1 : 0) {}

  void qualify_def(std::string &name, const FileFragment &fragment) {
    SymbolSource *override = nullptr;
    for (auto sym : symbols) {
      auto it = sym->defs.find(name);
      if (it != sym->defs.end()) {
        if (override && it->second.qualified != override->qualified) {
          WARNING(fragment.location(),
                  "reference '" << name << "' is ambiguous; definition imported from both "
                                << it->second.fragment.location() << " and "
                                << override->fragment.location());
        }
        override = &it->second;
      }
    }
    if (override) name = override->qualified;
  }

  bool qualify_topic(std::string &name, const FileFragment &fragment) {
    SymbolSource *override = nullptr;
    for (auto sym : symbols) {
      auto it = sym->topics.find(name);
      if (it != sym->topics.end()) {
        if (override && it->second.qualified != override->qualified) {
          WARNING(fragment.location(), "reference '" << name
                                                     << "' is ambiguous; topic imported from both "
                                                     << it->second.fragment.location() << " and "
                                                     << override->fragment.location());
        }
        override = &it->second;
      }
    }
    if (override) name = override->qualified;
    return override;
  }

  bool qualify_type(std::string &name, const FileFragment &use, FileFragment &def) {
    SymbolSource *override = nullptr;
    for (auto sym : symbols) {
      auto it = sym->types.find(name);
      if (it != sym->types.end()) {
        if (override && it->second.qualified != override->qualified) {
          WARNING(use.location(), "refernce '" << name << "' is ambiguous; type imported from both "
                                               << it->second.fragment.location() << " and "
                                               << override->fragment.location());
        }
        override = &it->second;
      }
    }
    if (override) {
      name = override->qualified;
      def = override->origin;
    }
    return override;
  }
};

struct RelaxedVertex {
  int v;
  int d;
  RelaxedVertex(int v_, int d_) : v(v_), d(d_) {}
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
  return keep ? x : x.substr(space + 1);
}

static std::unique_ptr<Expr> fracture_binding(const FileFragment &fragment,
                                              std::vector<ResolveDef> &defs,
                                              std::unique_ptr<Expr> body) {
  // Bellman-Ford algorithm, run for longest path
  // if f uses [yg], then d[f] must be <= d[yg]
  // if x uses [yg], then d[x] must be <= d[yg]+1
  // if we ever find a d[_] > n, there is an illegal loop

retry:
  std::vector<int> d(defs.size(), 0), p(defs.size(), -1);
  std::queue<RelaxedVertex> q;

  for (int i = 0; i < (int)defs.size(); ++i) q.push(RelaxedVertex(i, 0));

  while (!q.empty()) {
    RelaxedVertex rv = q.front();
    int drv = d[rv.v];
    q.pop();
    if (rv.d < drv) continue;
    ResolveDef &def = defs[rv.v];
    if (drv >= (int)defs.size()) {
      int j = rv.v;
      for (int i = 0; i < (int)defs.size(); ++i) j = p[j];
      // j is now inside the cycle
      int i = j;
      do {
        ERROR(defs[p[i]].fragment.location(),
              "definition of '" << defs[p[i]].name << "' references '" << defs[i].name
                                << "' forming an illegal cyclic value");
        // Wipe-out the cyclic expressions
        defs[i].edges.clear();
        defs[i].expr.reset();
        i = p[i];
      } while (i != j);
      goto retry;
    }
    int w = (!def.expr || def.expr->type == &Lambda::type) ? 0 : 1;
    for (auto i : def.edges) {
      if (drv + w > d[i]) {
        d[i] = drv + w;
        p[i] = rv.v;
        q.push(RelaxedVertex(i, drv + w));
      }
    }
  }

  std::vector<std::list<int>> levels(defs.size());
  for (int i = 0; i < (int)defs.size(); ++i) levels[d[i]].push_back(i);

  std::unique_ptr<Expr> out(std::move(body));
  for (int i = 0; i < (int)defs.size(); ++i) {
    if (levels[i].empty()) continue;
    std::unique_ptr<DefBinding> bind(new DefBinding(fragment, std::move(out)));
    for (auto j : levels[i]) {
      if (defs[j].expr && defs[j].expr->type != &Lambda::type) {
        auto out = bind->order.insert(std::make_pair(
            defs[j].name, DefBinding::OrderValue(defs[j].fragment, bind->val.size())));
        assert(out.second);
        bind->val.emplace_back(std::move(defs[j].expr));
        bind->valVars.emplace_back(std::move(defs[j].typeVars));
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
      if (defs[j].index == -1 && (!defs[j].expr || defs[j].expr->type == &Lambda::type))
        SCC(state, j);
    out = std::move(bind);
  }

  return out;
}

static bool reference_map(ResolveBinding *binding, const std::string &name) {
  NameIndex::iterator i;
  if ((i = binding->index.find(name)) != binding->index.end()) {
    if (binding->current_index != -1) binding->defs[binding->current_index].edges.insert(i->second);
    ++binding->defs[i->second].uses;
    return true;
  } else {
    return false;
  }
}

static bool rebind_ref(ResolveBinding *binding, std::string &name, const FileFragment &fragment) {
  ResolveBinding *iter;
  for (iter = binding; iter; iter = iter->parent) {
    iter->qualify_def(name, fragment);
    if (reference_map(iter, name)) return true;
  }
  return false;
}

static VarRef *rebind_subscribe(ResolveBinding *binding, const FileFragment &fragment,
                                std::string &name) {
  ResolveBinding *iter;
  for (iter = binding; iter; iter = iter->parent) {
    if (iter->qualify_topic(name, fragment)) break;
  }
  if (!iter) {
    ERROR(fragment.location(), "subscribe to non-existent topic '" << name << "'");
    return nullptr;
  }
  return new VarRef(fragment, "topic " + name);
}

static std::string rebind_publish(ResolveBinding *binding, const FileFragment &fragment,
                                  const std::string &key) {
  std::string name(key);
  ResolveBinding *iter;
  for (iter = binding; iter; iter = iter->parent) {
    if (iter->qualify_topic(name, fragment)) break;
  }
  if (!iter) {
    ERROR(fragment.location(), "publish to non-existent topic '" << name << "'");
  }
  return name;
}

struct PatternTree {
  FileFragment token, region;
  std::shared_ptr<Sum> sum;  // nullptr if unexpanded
  optional<AST> type;        // nullptr if untyped
  int cons;
  int var;  // -1 if unbound/_
  std::vector<PatternTree> children;
  PatternTree(const FileFragment &token_ = FRAGMENT_CPP_LINE,
              const FileFragment &region_ = FRAGMENT_CPP_LINE, int var_ = -1)
      : token(token_), region(region_), sum(0), cons(0), var(var_) {}
  void format(std::ostream &os, int p) const;
};

std::ostream &operator<<(std::ostream &os, const PatternTree &pt) {
  pt.format(os, 0);
  return os;
}

void PatternTree::format(std::ostream &os, int p) const {
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

enum Refutable { TOTAL, IDENTITY, OTHERWISE };

struct PatternRef {
  FileFragment fragment;  // patterns (right-hand-side), prototype (first arg)
  FileFragment guard_fragment;
  PatternTree tree;
  int index;  // for prototype: next var name, for patterns: function index
  int uses;
  union {
    bool guard;           // for non-prototype
    Refutable refutable;  // for prototype
  };

  PatternRef(const FileFragment &fragment_)
      : fragment(fragment_), guard_fragment(FRAGMENT_CPP_LINE), uses(0) {}
  PatternRef(PatternRef &&other) = default;
  PatternRef &operator=(PatternRef &&other) = default;
};

// assumes a detail <= b
static std::shared_ptr<Sum> find_mismatch(std::vector<int> &path, const PatternTree &a,
                                          const PatternTree &b) {
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
    expr = new App(expr->fragment, expr, new VarRef(b.region, "_ a" + std::to_string(a.var)));
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

static Expr *build_identity(const FileFragment &l, PatternTree &tree) {
  if (tree.sum) {
    Constructor &cons = tree.sum->members[tree.cons];
    Expr *out = new Construct(l, tree.sum, &cons);
    for (size_t i = 0; i < tree.children.size(); ++i) out = new Lambda(l, "_", out);
    for (auto &c : tree.children) {
      out = new App(l, out, build_identity(l, c));
    }
    return out;
  } else {
    return new VarRef(l, "_ a" + std::to_string(tree.var));
  }
}

static std::unique_ptr<Expr> ascribe(std::unique_ptr<Expr> expr, const FileFragment &l,
                                     optional<AST> &&type) {
  if (!type) return expr;
  return std::unique_ptr<Expr>(
      new Ascribe(FRAGMENT_CPP_LINE, AST(std::move(*type)), expr.release(), l));
}

// invariants: !patterns.empty(); patterns have detail >= patterns[0]
// post-condition: patterns unchanged (internal mutation is reversed)
static std::unique_ptr<Expr> expand_patterns(const std::string &fnname,
                                             std::vector<PatternRef> &patterns) {
  PatternRef &prototype = patterns[0];
  FileFragment fragment = prototype.fragment;
  if (patterns.size() == 1) {
    if (prototype.refutable == IDENTITY) {
      ++prototype.uses;
      return std::unique_ptr<Expr>(build_identity(prototype.fragment, prototype.tree));
    } else if (prototype.refutable == OTHERWISE) {
      ++prototype.uses;
      FileFragment line = prototype.fragment;
      return std::unique_ptr<Expr>(
          new App(line, new VarRef(line, "_ else"), new VarRef(line, "_ a0")));
    } else {
      ERROR(fragment.location(), "non-exhaustive match; missing: " << prototype.tree);
      return nullptr;
    }
  }
  std::vector<int> expand;
  std::shared_ptr<Sum> sum = find_mismatch(expand, prototype.tree, patterns[1].tree);
  if (sum) {
    PatternTree *prototype_token = get_expansion(&prototype.tree, expand);
    FileFragment *argument;
    if (prototype_token->region.empty()) {
      argument = &get_expansion(&patterns[1].tree, expand)->region;
    } else {
      argument = &prototype_token->region;
    }
    std::unique_ptr<Destruct> des(new Destruct(
        fragment, sum, new VarRef(*argument, "_ a" + std::to_string(prototype_token->var))));
    for (size_t c = 0; c < sum->members.size(); ++c) {
      Constructor &cons = sum->members[c];
      std::vector<PatternRef> bucket;
      int args = cons.ast.args.size();
      int var = prototype.index;
      prototype.index += args;
      // These bare Gets create a dependency on case function's first argument.
      // While this is nominally the same as the destructor's argument, writing
      // the function this way prevents lifting the Get out of the case.
      std::vector<std::unique_ptr<Expr>> gets;
      for (int i = 0; i < args; ++i) gets.emplace_back(new Get(FRAGMENT_CPP_LINE, sum, &cons, i));
      des->uses.resize(des->uses.size() + 1);
      for (auto p = patterns.begin(); p != patterns.end(); ++p) {
        PatternTree *t = get_expansion(&p->tree, expand);
        if (!t->sum) {
          t->sum = sum;
          t->cons = c;
          t->children.resize(args);
          if (p == patterns.begin())
            for (auto &c : t->children) c.var = var++;
          bucket.emplace_back(std::move(*p));
          p->index = -1;
        } else if (t->sum != sum) {
          ERROR(p->fragment.location(), "constructor '" << t->sum->members[t->cons].ast.name
                                                        << "' is used in a pattern matching '"
                                                        << sum->name
                                                        << "', but is not a member of this type");
          return nullptr;
        } else if (t->sum && t->cons == (int)c) {
          des->uses.back().emplace_back(t->token);
          // Put any supplied type constraints on the object
          assert(args == (int)t->children.size());
          for (int i = 0; i < args; ++i) {
            PatternTree &arg = t->children[i];
            gets[i] = ascribe(std::move(gets[i]), arg.region, std::move(arg.type));
          }
          bucket.emplace_back(std::move(*p));
          p->index = -2;
        }
      }
      std::unique_ptr<DefMap> rmap(new DefMap(fragment));
      rmap->body = expand_patterns(fnname, bucket);
      if (!rmap->body) return nullptr;
      for (size_t i = args; i > 0; --i) {
        auto out = rmap->defs.insert(std::make_pair(
            "_ a" + std::to_string(--var), DefValue(FRAGMENT_CPP_LINE, std::move(gets[i - 1]))));
        assert(out.second);
      }
      Lambda *lam = new Lambda(rmap->body->fragment, "_ tuple_case", rmap.get());
      rmap.release();
      lam->fnname = fnname;
      des->cases.emplace_back(lam);
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
    des->fragment = des->cases.front()->fragment;
    return std::unique_ptr<Expr>(des.release());
  } else {
    PatternRef &p = patterns[1];
    ++p.uses;
    if (!p.guard) {
      return std::unique_ptr<Expr>(
          new App(p.fragment,
                  fill_pattern(new VarRef(p.fragment, "_ f" + std::to_string(p.index)),
                               prototype.tree, p.tree),
                  new VarRef(p.fragment, "Unit@wake")));
    } else {
      PatternRef save(std::move(patterns[1]));
      patterns.erase(patterns.begin() + 1);
      std::unique_ptr<Expr> guard_false(expand_patterns(fnname, patterns));
      patterns.emplace(patterns.begin() + 1, std::move(save));
      if (!guard_false) return nullptr;
      std::unique_ptr<DefMap> fmap(new DefMap(p.fragment));
      fmap->defs.insert(std::make_pair(
          "_ guardpair",
          DefValue(p.fragment, std::unique_ptr<Expr>(fill_pattern(
                                   new VarRef(p.fragment, "_ f" + std::to_string(p.index)),
                                   prototype.tree, p.tree)))));
      fmap->defs.insert(std::make_pair(
          "_ rhs", DefValue(p.fragment, std::unique_ptr<Expr>(new App(
                                            p.fragment, new VarRef(p.fragment, "getPairFirst@wake"),
                                            new VarRef(p.fragment, "_ guardpair"))))));
      fmap->defs.insert(std::make_pair(
          "_ guard",
          DefValue(p.fragment, std::unique_ptr<Expr>(
                                   new App(p.fragment, new VarRef(p.fragment, "getPairSecond@wake"),
                                           new VarRef(p.fragment, "_ guardpair"))))));
      std::unique_ptr<Expr> guard_true(new App(p.fragment, new VarRef(p.fragment, "_ rhs"),
                                               new VarRef(p.fragment, "Unit@wake")));
      std::unique_ptr<Destruct> des(
          new Destruct(fragment, Boolean,
                       new App(p.fragment, new VarRef(p.fragment, "_ guard"),
                               new VarRef(p.fragment, "Unit@wake"))));
      des->cases.emplace_back(new Lambda(guard_true->fragment, "_", guard_true.get()));
      guard_true.release();
      des->cases.emplace_back(new Lambda(guard_false->fragment, "_", guard_false.get()));
      guard_false.release();
      des->fragment = des->cases.front()->fragment;
      des->uses.resize(2);
      fmap->body = std::move(des);
      return std::unique_ptr<Expr>(std::move(fmap));
    }
  }
}

static PatternTree cons_lookup(ResolveBinding *binding, std::unique_ptr<Expr> &expr, AST &ast,
                               std::shared_ptr<Sum> multiarg) {
  PatternTree out(ast.token, ast.region);
  out.type = std::move(ast.type);
  if (ast.name == "_") {
    // no-op; unbound
  } else if (!ast.name.empty() && lex_kind(ast.name) == LOWER) {
    Lambda *lambda = new Lambda(expr->fragment, ast.name, expr.get());
    expr.release();
    if (ast.name.compare(0, 3, "_ k") != 0) lambda->token = ast.token;
    expr = std::unique_ptr<Expr>(lambda);
    out.var = 0;  // bound
  } else {
    if (ast.name.empty()) {
      out.sum = multiarg;
    } else
      for (ResolveBinding *iter = binding; iter; iter = iter->parent) {
        iter->qualify_def(ast.name, ast.token);
        auto it = iter->index.find(ast.name);
        if (it != iter->index.end()) {
          Expr *cons = iter->defs[it->second].expr.get();
          while (cons && cons->type == &Lambda::type)
            cons = static_cast<Lambda *>(cons)->body.get();
          if (cons && cons->type == &Construct::type) {
            Construct *c = static_cast<Construct *>(cons);
            out.sum = c->sum;
            out.cons = c->cons->index;
          }
        }
      }
    if (!out.sum) {
      ERROR(ast.token.location(), "constructor '" << ast.name << "' is not defined");
      out.var = 0;
    } else if (out.sum->members[out.cons].ast.args.size() != ast.args.size()) {
      std::ostringstream message;
      if (ast.name.empty()) {
        message << "case";
      } else {
        message << "constructor '" << ast.name << "'";
      }
      message << " is used with " << ast.args.size() << " parameters, but must have "
              << out.sum->members[out.cons].ast.args.size();
      reporter->reportError(ast.region.location(), message.str());
      out.sum = 0;
      out.var = 0;
    } else {
      for (auto a = ast.args.rbegin(); a != ast.args.rend(); ++a)
        out.children.push_back(cons_lookup(binding, expr, *a, nullptr));
      std::reverse(out.children.begin(), out.children.end());
    }
  }
  return out;
}

static std::unique_ptr<Expr> rebind_match(const std::string &fnname, ResolveBinding *binding,
                                          std::unique_ptr<Match> match) {
  std::unique_ptr<DefMap> map(new DefMap(FRAGMENT_CPP_LINE));
  std::vector<PatternRef> patterns;

  patterns.reserve(match->patterns.size() + 1);
  patterns.emplace_back(match->args.front()->fragment);
  PatternRef &prototype = patterns.front();
  prototype.uses = 1;
  prototype.index = match->args.size();
  prototype.refutable = match->refutable ? IDENTITY : TOTAL;

  if (match->otherwise) {
    prototype.fragment = match->otherwise->fragment;
    prototype.refutable = OTHERWISE;
    auto out = map->defs.insert(std::make_pair(
        "_ else",
        DefValue(FRAGMENT_CPP_LINE, std::unique_ptr<Expr>(new Lambda(
                                        FRAGMENT_CPP_LINE, "_", match->otherwise.release())))));
    assert(out.second);
  }

  std::shared_ptr<Sum> multiarg;
  if (match->args.size() == 1) {
    std::unique_ptr<Expr> arg = std::move(match->args.front());
    prototype.tree.region = arg->fragment;
    prototype.tree.var = 0;
    for (auto &p : match->patterns)
      arg = ascribe(std::move(arg), p.pattern.region, std::move(p.pattern.type));
    auto out =
        map->defs.insert(std::make_pair("_ a0", DefValue(FRAGMENT_CPP_LINE, std::move(arg))));
    assert(out.second);
  } else {
    prototype.tree.sum = multiarg = std::make_shared<Sum>(AST(FRAGMENT_CPP_LINE));
    multiarg->members.emplace_back(AST(FRAGMENT_CPP_LINE));
    size_t i = 0;
    for (auto &a : match->args) {
      prototype.tree.children.emplace_back(a->fragment, a->fragment, i);
      for (auto &p : match->patterns)
        if (i < p.pattern.args.size())
          a = ascribe(std::move(a), p.pattern.args[i].region, std::move(p.pattern.args[i].type));
      auto out = map->defs.insert(
          std::make_pair("_ a" + std::to_string(i), DefValue(FRAGMENT_CPP_LINE, std::move(a))));
      assert(out.second);
      multiarg->members.front().ast.args.emplace_back(AST(FRAGMENT_CPP_LINE));
      ++i;
    }
  }

  int f = 0;
  bool ok = true;
  for (auto &p : match->patterns) {
    if (p.pattern.args.empty() && multiarg) {
      ERROR(p.pattern.region.location(), "multi-argument match requires a multi-argument pattern");
      continue;
    }
    patterns.emplace_back(p.expr->fragment);
    patterns.back().index = f;
    patterns.back().guard = static_cast<bool>(p.guard);
    std::unique_ptr<Expr> expr;
    if (true) {
      std::string cname =
          match->patterns.size() == 1 ? fnname : fnname + ".case" + std::to_string(f);
      expr = std::unique_ptr<Expr>(new Lambda(p.expr->fragment, "_", p.expr.get(), cname.c_str()));
      p.expr.release();
    }
    if (p.guard) {
      patterns.back().guard_fragment = p.guard->fragment;
      std::string gname =
          match->patterns.size() == 1 ? fnname : fnname + ".guard" + std::to_string(f);
      expr = std::unique_ptr<Expr>(new App(
          FRAGMENT_CPP_LINE,
          new App(FRAGMENT_CPP_LINE, new VarRef(FRAGMENT_CPP_LINE, "Pair@wake"), expr.release()),
          new Lambda(p.guard->fragment, "_", p.guard.get(), gname.c_str())));
      p.guard.release();
    }
    patterns.back().tree = cons_lookup(binding, expr, p.pattern, multiarg);
    auto out = map->defs.insert(
        std::make_pair("_ f" + std::to_string(f), DefValue(FRAGMENT_CPP_LINE, std::move(expr))));
    assert(out.second);
    ok &= !patterns.front().tree.sum || patterns.back().tree.sum;
    ++f;
  }
  if (!ok) return nullptr;
  map->body = expand_patterns(fnname, patterns);
  if (!map->body) return nullptr;
  for (auto &p : patterns) {
    if (!p.uses) {
      ERROR(p.fragment.location(), "pattern is impossible to match");
      return nullptr;
    }
  }
  if (match->refutable && patterns.front().uses <= 1) {
    ERROR(match->fragment.location(), "the required pattern can never fail; use a def instead");
    return nullptr;
  }
  map->fragment = map->body->fragment;
  return std::unique_ptr<Expr>(map.release());
}

struct SymMover {
  std::pair<const std::string, SymbolSource> &sym;
  const char *kind;
  std::string def;
  bool warn;
  Package *package;

  SymMover(Top &top, std::pair<const std::string, SymbolSource> &sym_, const char *kind_)
      : sym(sym_), kind(kind_) {
    size_t at = sym.second.qualified.find_first_of('@');
    def.assign(sym.second.qualified, 0, at);

    std::string pkg(sym.second.qualified, at + 1);
    auto it = top.packages.find(pkg);
    if (it == top.packages.end()) {
      warn = false;
      package = nullptr;
      WARNING(sym.second.fragment.location(),
              "import of " << kind << " '" << def << "' from non-existent package '" << pkg << "'");
    } else {
      warn = true;
      package = it->second.get();
    }
  }

  ~SymMover() {
    if (warn) {
      WARNING(sym.second.fragment.location(),
              kind << " '" << def << "' is not exported by package '" << package->name << "'");
    }
  }

  void consider(const Symbols::SymbolMap &from, Symbols::SymbolMap &to) {
    if (def.compare(0, 3, "op ") == 0) {
      auto unary = from.find("unary " + def.substr(3));
      if (unary != from.end()) {
        to.insert(
            std::make_pair("unary " + sym.first.substr(3), sym.second.qualify(unary->second)));
        warn = false;
      }
      auto binary = from.find("binary " + def.substr(3));
      if (binary != from.end()) {
        to.insert(
            std::make_pair("binary " + sym.first.substr(3), sym.second.qualify(binary->second)));
        warn = false;
      }
    } else {
      auto it = from.find(def);
      if (it != from.end()) {
        to.insert(std::make_pair(sym.first, sym.second.qualify(it->second)));
        warn = false;
      }
    }
  }

  void defs(Symbols::SymbolMap &defs) {
    if (package) consider(package->exports.defs, defs);
  }
  void types(Symbols::SymbolMap &types) {
    if (package) consider(package->exports.types, types);
  }
  void topics(Symbols::SymbolMap &topics) {
    if (package) consider(package->exports.topics, topics);
  }
};

static std::vector<Symbols *> process_import(Top &top, Imports &imports, FileFragment &fragment) {
  Symbols::SymbolMap mixed(std::move(imports.mixed));
  for (auto &d : mixed) {
    SymMover mover(top, d, "symbol");
    mover.defs(imports.defs);
    mover.types(imports.types);
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

  std::vector<Symbols *> out;
  for (auto &p : imports.import_all) {
    auto it = top.packages.find(p.first);
    if (it == top.packages.end()) {
      WARNING(p.second.location(), "full import from non-existent package '" << p.first << "'");
    } else {
      out.push_back(&it->second->exports);
    }
  }
  out.push_back(&imports);
  return out;
}

static bool qualify_type(ResolveBinding *binding, std::string &name, const FileFragment &use,
                         FileFragment &def) {
  ResolveBinding *iter;
  for (iter = binding; iter; iter = iter->parent) {
    if (iter->qualify_type(name, use, def)) break;
  }

  if (iter) {
    return true;
  } else if (name == "BadType") {
    return false;
  } else {
    ERROR(use.location(), "reference to undefined type '" << name << "'");
    return false;
  }
}

static bool qualify_type(ResolveBinding *binding, AST &type) {
  // Type variables do not get qualified
  if (lex_kind(type.name) == LOWER) return true;
  bool ok = qualify_type(binding, type.name, type.token, type.definition);
  for (auto &x : type.args)
    if (!qualify_type(binding, x)) ok = false;
  return ok;
}

static std::unique_ptr<Expr> fracture(Top &top, bool anon, const std::string &name,
                                      std::unique_ptr<Expr> expr, ResolveBinding *binding) {
  if (expr->type == &VarRef::type) {
    VarRef *ref = static_cast<VarRef *>(expr.get());
    // don't fail if unbound; leave that for the second pass
    rebind_ref(binding, ref->name, ref->fragment);
    return expr;
  } else if (expr->type == &Subscribe::type) {
    Subscribe *sub = static_cast<Subscribe *>(expr.get());
    VarRef *out = rebind_subscribe(binding, sub->fragment, sub->name);
    if (!out) return nullptr;
    out->flags |= FLAG_AST;
    return fracture(top, true, name, std::unique_ptr<Expr>(out), binding);
  } else if (expr->type == &App::type) {
    App *app = static_cast<App *>(expr.get());
    app->fn = fracture(top, true, name, std::move(app->fn), binding);
    app->val = fracture(top, true, name, std::move(app->val), binding);
    return expr;
  } else if (expr->type == &Lambda::type) {
    Lambda *lambda = static_cast<Lambda *>(expr.get());
    ResolveBinding lbinding(binding);
    lbinding.index[lambda->name] = 0;
    lbinding.defs.emplace_back(lambda->name, FRAGMENT_CPP_LINE, nullptr);
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
    if (lbinding.defs.back().uses == 0 && !lambda->name.empty() && lambda->name[0] != '_') {
      WARNING(lambda->token.location(), "unused function argument '" << lambda->name
                                                                     << "'; consider renaming to _"
                                                                     << lambda->name);
    }
    return expr;
  } else if (expr->type == &Match::type) {
    std::unique_ptr<Match> m(static_cast<Match *>(expr.release()));
    auto out = rebind_match(name, binding, std::move(m));
    if (!out) return out;
    return fracture(top, anon, name, std::move(out), binding);
  } else if (expr->type == &Destruct::type) {
    Destruct *app = static_cast<Destruct *>(expr.get());
    app->arg = fracture(top, true, name, std::move(app->arg), binding);
    for (auto &lam : app->cases) lam = fracture(top, true, name, std::move(lam), binding);
    return expr;
  } else if (expr->type == &DefMap::type) {
    DefMap *def = static_cast<DefMap *>(expr.get());
    ResolveBinding dbinding(binding);
    dbinding.symbols = process_import(top, def->imports, def->fragment);
    for (auto &i : def->defs) {
      dbinding.index[i.first] = dbinding.defs.size();
      dbinding.defs.emplace_back(i.first, i.second.fragment, std::move(i.second.body),
                                 std::move(i.second.typeVars));
    }
    for (auto &i : dbinding.defs) {
      i.expr = fracture(top, false, addanon(name, anon) + "." + trim(i.name), std::move(i.expr),
                        &dbinding);
      ++dbinding.current_index;
    }
    dbinding.current_index = -1;
    std::unique_ptr<Expr> body = fracture(top, true, name, std::move(def->body), &dbinding);
    for (auto &i : dbinding.defs) {
      if (i.uses == 0 && !i.name.empty() && i.name[0] != '_') {
        WARNING(i.fragment.location(), "unused local definition of '"
                                           << i.name << "'; consider removing or renaming to _"
                                           << i.name);
      }
    }
    auto out = fracture_binding(def->fragment, dbinding.defs, std::move(body));
    if (out && (def->flags & FLAG_AST) != 0) out->flags |= FLAG_AST;
    return out;
  } else if (expr->type == &Construct::type) {
    Construct *con = static_cast<Construct *>(expr.get());
    bool ok = true;
    if (!con->sum->scoped) {
      con->sum->scoped = true;
      FileFragment ignored(FRAGMENT_CPP_LINE);
      if (!qualify_type(binding, con->sum->name, con->sum->token, ignored)) ok = false;
    }
    if (!con->cons->scoped) {
      con->cons->scoped = true;
      for (auto &arg : con->cons->ast.args)
        if (!qualify_type(binding, arg)) ok = false;
    }
    if (binding->defs.size() == 1 && !binding->defs[0].expr) {
      // Use all lambda arguments
      size_t todo = con->cons->ast.args.size();
      for (ResolveBinding *iter = binding; todo != 0; iter = iter->parent, --todo)
        ++iter->defs[0].uses;
    }  // else: edit/set function
    if (!ok) expr.reset();
    return expr;
  } else if (expr->type == &Ascribe::type) {
    Ascribe *asc = static_cast<Ascribe *>(expr.get());
    asc->body = fracture(top, true, name, std::move(asc->body), binding);
    if (qualify_type(binding, asc->signature)) {
      return expr;
    } else {
      return std::move(asc->body);
    }
  } else if (expr->type == &Prim::type) {
    // Use all the arguments
    for (ResolveBinding *iter = binding; iter && iter->defs.size() == 1 && !iter->defs[0].expr;
         iter = iter->parent)
      ++iter->defs[0].uses;
    return expr;
  } else {
    // Literal/Get
    return expr;
  }
}

static std::unique_ptr<Expr> fracture(std::unique_ptr<Top> top) {
  ResolveBinding gbinding(nullptr);    // global mapping + qualified defines
  ResolveBinding pbinding(&gbinding);  // package mapping
  ResolveBinding ibinding(&pbinding);  // file import mapping
  ResolveBinding dbinding(&ibinding);  // file local mapping
  size_t publish_count = 0;
  bool fail = false;

  for (auto &p : top->packages) {
    for (auto &f : p.second->files) {
      for (auto &d : f.content->defs) {
        auto it = gbinding.index.insert(std::make_pair(d.first, gbinding.defs.size()));
        if (it.second) {
          gbinding.defs.emplace_back(d.first, d.second.fragment, std::move(d.second.body),
                                     std::move(d.second.typeVars));
        } else {
          // discard duplicate symbol; we already reported an error in package-local join
        }
      }
      for (auto it = f.pubs.rbegin(); it != f.pubs.rend(); ++it) {
        auto name = "publish " + it->first + " " + std::to_string(++publish_count);
        gbinding.index[name] = gbinding.defs.size();
        gbinding.defs.emplace_back(name, it->second.fragment, std::move(it->second.body));
      }
    }
  }
  for (auto &p : top->packages) {
    for (auto &f : p.second->files) {
      for (auto &t : f.topics) {
        auto name = "topic " + t.first + "@" + p.first;
        auto it = gbinding.index.insert(std::make_pair(name, gbinding.defs.size()));
        if (it.second) {
          gbinding.defs.emplace_back(
              name, t.second.fragment,
              std::unique_ptr<Expr>(new VarRef(t.second.fragment, "Nil@wake")));
        } else {
          // discard duplicate topic; we already reported an error in package-local join
        }
      }
    }
  }

  gbinding.symbols.push_back(&top->globals);
  for (auto &p : top->packages) {
    pbinding.symbols.clear();
    pbinding.symbols.push_back(&p.second->package);
    for (auto &f : p.second->files) {
      ibinding.symbols = process_import(*top, f.content->imports, f.content->fragment);
      dbinding.symbols.clear();
      dbinding.symbols.push_back(&f.local);
      for (auto &d : f.content->defs) {
        // Only process if not a duplicate skipped in earlier loop
        if (gbinding.index[d.first] == gbinding.current_index) {
          ResolveDef &def = gbinding.defs[gbinding.current_index];
          def.expr = fracture(*top, false, trim(def.name), std::move(def.expr), &dbinding);
          ++gbinding.current_index;
        }
      }
      for (auto it = f.pubs.rbegin(); it != f.pubs.rend(); ++it) {
        ResolveDef &def = gbinding.defs[gbinding.current_index];
        auto qualified = rebind_publish(&dbinding, def.fragment, it->first);
        size_t at = qualified.find('@');
        if (at != std::string::npos) {
          def.expr = fracture(*top, false, trim(def.name), std::move(def.expr), &dbinding);
          ResolveDef &topicdef = gbinding.defs[gbinding.index.find("topic " + qualified)->second];
          FileFragment &l = topicdef.expr->fragment;
          topicdef.expr = std::unique_ptr<Expr>(new App(
              l,
              new App(l, new VarRef(l, "binary ++@wake"), new VarRef(def.expr->fragment, def.name)),
              topicdef.expr.release()));
        } else {
          fail = true;
        }
        ++gbinding.current_index;
      }
      for (auto &t : f.topics) {
        if (!qualify_type(&dbinding, t.second.type)) fail = true;
      }
    }
  }

  for (auto &p : top->packages) {
    for (auto &f : p.second->files) {
      for (auto &t : f.topics) {
        auto name = "topic " + t.first + "@" + p.first;
        // Only process if not a duplicate skipped in earlier loop
        if (gbinding.index[name] == gbinding.current_index) {
          ResolveDef &def = gbinding.defs[gbinding.current_index];
          def.expr = fracture(*top, false, trim(def.name), std::move(def.expr), &gbinding);
          ++gbinding.current_index;

          FileFragment topic = def.expr->fragment;

          // Form the type required for publishes
          std::vector<AST> args;
          args.emplace_back(t.second.type);  // qualified by prior pass
          AST signature(t.second.type.region, "List@wake", std::move(args));

          // Insert Ascribe requirements on all publishes
          Expr *next = nullptr;
          for (Expr *iter = def.expr.get(); iter->type == &App::type; iter = next) {
            App *app1 = static_cast<App *>(iter);
            App *app2 = static_cast<App *>(app1->fn.get());
            FileFragment publish = app2->val->fragment;
            app2->val = std::unique_ptr<Expr>(
                new Ascribe(FRAGMENT_CPP_LINE, AST(signature), app2->val.release(), publish));
            next = app1->val.get();
          }

          // If the topic is empty, still force the type
          if (!next)
            def.expr = std::unique_ptr<Expr>(
                new Ascribe(FRAGMENT_CPP_LINE, AST(signature), def.expr.release(), topic));
        }
      }
    }
  }

  Package *defp = nullptr;
  auto defit = top->packages.find(top->def_package);
  if (defit != top->packages.end()) defp = defit->second.get();

  gbinding.current_index = -1;
  pbinding.symbols.clear();
  ibinding.symbols.clear();
  dbinding.symbols.clear();
  if (defp) {
    dbinding.symbols.push_back(&defp->package);
    std::set<std::string> imports;
    for (auto &file : defp->files)
      for (auto &bulk : file.content->imports.import_all) imports.insert(bulk.first);
    for (auto &imp : imports) {
      auto it = top->packages.find(imp);
      if (it != top->packages.end()) ibinding.symbols.push_back(&it->second->exports);
    }
  }

  std::unique_ptr<Expr> body = fracture(*top, true, "", std::move(top->body), &dbinding);

  // Mark exports and globals as uses
  for (auto &g : top->globals.defs) {
    auto it = gbinding.index.find(g.second.qualified);
    if (it != gbinding.index.end()) ++gbinding.defs[it->second].uses;
  }
  for (auto &g : top->globals.topics) {
    auto it = gbinding.index.find("topic " + g.second.qualified);
    if (it != gbinding.index.end()) ++gbinding.defs[it->second].uses;
  }
  for (auto &p : top->packages) {
    for (auto &e : p.second->exports.defs) {
      auto it = gbinding.index.find(e.second.qualified);
      if (it != gbinding.index.end()) ++gbinding.defs[it->second].uses;
    }
    for (auto &e : p.second->exports.topics) {
      auto it = gbinding.index.find("topic " + e.second.qualified);
      if (it != gbinding.index.end()) ++gbinding.defs[it->second].uses;
    }
  }

  for (auto &package : top->packages) {
    for (auto &file : package.second->files) {
      std::map<std::string, std::pair<int, Location>> imports = {};
      std::map<std::string, std::string> unqualified_to_qualified = {};

      std::string filename = "";

      for (auto &import : file.content->imports.defs) {
        filename = import.second.fragment.location().filename;
        imports.insert({import.second.qualified, {0, import.second.fragment.location()}});

        std::size_t at_pos = import.second.qualified.find("@");
        unqualified_to_qualified.insert(
            {import.second.qualified.substr(0, at_pos), import.second.qualified});
      }

      for (auto &import : file.content->imports.topics) {
        filename = import.second.fragment.location().filename;
        imports.insert({import.second.qualified, {0, import.second.fragment.location()}});

        std::size_t at_pos = import.second.qualified.find("@");
        unqualified_to_qualified.insert(
            {import.second.qualified.substr(0, at_pos), import.second.qualified});
      }

      for (auto &import : file.content->imports.import_all) {
        filename = import.second.location().filename;
        printf("fex: %s: %s\n", filename.c_str(), import.first.c_str());

        imports.insert({"_@" + import.first, {0, import.second.location()}});

        for (auto &bind : gbinding.defs) {
          size_t at_pkg = bind.name.find("@" + import.first);
          if (at_pkg != std::string::npos) {
            unqualified_to_qualified.insert({bind.name.substr(0, at_pkg), bind.name});
          }
        }
      }

      // TODO: file.content->imports.types are not currently
      // being checked as there isn't a good way to get the edges from a type

      std::vector<std::string> resolved_defs;

      for (auto &bind : gbinding.defs) {
        if (bind.fragment.location().filename == filename) {
          resolved_defs.push_back(bind.name);
        }
      }

      for (auto g : file.local.defs) {
        // TODO: what should actually be added to resolved_defs here??
        resolved_defs.push_back(g.first + "@" + package.first);
      }

      for (auto g : file.local.topics) {
        // TODO: what should actually be added to resolved_defs here??
        resolved_defs.push_back(g.first + "@" + package.first);
      }

      for (auto &publish : file.pubs) {
        // If the publish was an imported publish, mark it as used
        auto qual_it = unqualified_to_qualified.find(publish.first);
        if (qual_it == unqualified_to_qualified.end()) {
          printf("fex: cannot qualify %s\n", publish.first.c_str());
          continue;
        }

        // Mark import all as used
        size_t at = qual_it->second.find("@");
        std::string pkg = qual_it->second.substr(at);
        auto pkg_import_it = imports.find("_" + pkg);
        if (pkg_import_it != imports.end()) {
          pkg_import_it->second.first++;
        }

        // Mark import as used
        auto import_it = imports.find(qual_it->second);
        if (import_it == imports.end()) {
          continue;
        }
        import_it->second.first++;
      }

      for (auto &topic : file.topics) {
        // If the topic was an imported topic, mark it as used
        auto qual_it = unqualified_to_qualified.find(topic.first);
        if (qual_it == unqualified_to_qualified.end()) {
          continue;
        }

        // Mark import all as used
        size_t at = qual_it->second.find("@");
        std::string pkg = qual_it->second.substr(at);
        auto pkg_import_it = imports.find("_" + pkg);
        if (pkg_import_it != imports.end()) {
          pkg_import_it->second.first++;
        }

        // Mark import as used
        auto import_it = imports.find(qual_it->second);
        if (import_it == imports.end()) {
          continue;
        }
        import_it->second.first++;
      }

      for (const std::string &def : resolved_defs) {
        auto it = gbinding.index.find(def);
        if (it == gbinding.index.end()) {
          continue;
        }

        auto &bound_def = gbinding.defs[it->second];
        for (const auto &used_id : bound_def.edges) {
          auto &used_def = gbinding.defs[used_id];

          // Mark import all as used
          size_t at = used_def.name.find("@");
          std::string pkg = used_def.name.substr(at);
          auto pkg_import_it = imports.find("_" + pkg);
          if (pkg_import_it != imports.end()) {
            pkg_import_it->second.first++;
          }

          // Mark import as used
          auto import_it = imports.find(used_def.name);
          if (import_it != imports.end()) {
            import_it->second.first++;
          }
        }
      }

      for (auto &import : imports) {
        if (import.second.first > 0) {
          continue;
        }

        WARNING(import.second.second,
                "unused import of '" << import.first << "'; consider removing.");
      }

      // TODO: track unused import_all imports (from x import _)
      //  This should be achievable by:
      //    1. looping over f.content->imports.import_all
      //    2. storing the package name
      //    3. counting the edges of all things from that package
      //    4. if count = 0 then unused
    }
  }

  // Report unused definitions
  for (auto &def : gbinding.defs) {
    if (def.uses == 0 && !def.name.empty() && def.name[0] != '_' && def.expr &&
        !(def.expr->flags & FLAG_SYNTHETIC)) {
      size_t at = def.name.find_first_of('@');
      std::string name = def.name.substr(0, at);
      WARNING(def.fragment.location(), "unused top-level definition of '"
                                           << name << "'; consider removing or renaming to _"
                                           << name);
    }
  }

  FileFragment fragment = body->fragment;
  auto out = fracture_binding(fragment, gbinding.defs, std::move(body));
  if (fail) out.reset();
  return out;
}

struct NameRef {
  int index;
  int def;
  FileFragment target;
  Lambda *lambda;
  TypeVar *var;
  NameRef() : index(-1), def(0), target(FRAGMENT_CPP_LINE), lambda(nullptr), var(nullptr) {}
};

struct NameBinding {
  NameBinding *next;
  DefBinding *binding;
  Lambda *lambda;
  bool open;
  int generalized;

  NameBinding() : next(0), binding(0), lambda(0), open(true), generalized(0) {}
  NameBinding(NameBinding *next_, Lambda *lambda_)
      : next(next_), binding(0), lambda(lambda_), open(true), generalized(0) {}
  NameBinding(NameBinding *next_, DefBinding *binding_)
      : next(next_), binding(binding_), lambda(0), open(true), generalized(0) {}

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
      out.target = i->second.fragment;
      if (idx < (int)binding->val.size()) {
        auto x = binding->val[idx].get();
        out.index = idx;
        out.var = x ? &x->typeVar : 0;
      } else {
        auto x = binding->fun[idx - binding->val.size()].get();
        out.index = 0;
        out.var = x ? &x->typeVar : 0;
        out.lambda = x;
        if (idx >= generalized)  // recursive use
          x->flags |= FLAG_RECURSIVE;
      }
    } else if (next) {
      out = next->find(x);
      if (out.index >= 0) {
        if (binding) out.index += binding->val.size();
        if (lambda) ++out.index;
      }
    } else {
      out.index = -1;
    }
    return out;
  }
};

struct FnErrorMessage : public TypeErrorMessage {
  FnErrorMessage(const FileFragment *f_) : TypeErrorMessage(f_) {}
  void formatA(std::ostream &os) const {
    os << "type error; expression " << f->segment() << " has type";
  }
  void formatB(std::ostream &os) const {
    os << "but is used as a function and must have function type";
  }
};

struct ArgErrorMessage : public TypeErrorMessage {
  const FileFragment *fn;
  const char *arg;
  ArgErrorMessage(const FileFragment *fn_, const FileFragment *a_, const char *arg_)
      : TypeErrorMessage(a_), fn(fn_), arg(arg_) {}
  void formatA(std::ostream &os) const {
    os << "type error; function " << fn->segment() << " expected argument";
    if (arg && arg[0] && !strchr(arg, ' ') && strcmp(arg, "_")) os << " '" << arg << "'";
    os << " of type";
  }
  void formatB(std::ostream &os) const {
    os << "but was supplied argument " << f->segment() << " of type";
  }
};

struct AscErrorMessage : public TypeErrorMessage {
  AscErrorMessage(const FileFragment *f_) : TypeErrorMessage(f_) {}
  void formatA(std::ostream &os) const {
    os << "type error; expression " << f->segment() << " of type";
  }
  void formatB(std::ostream &os) const { os << "does not match explicit type ascription of"; }
};

struct RecErrorMessage : public TypeErrorMessage {
  RecErrorMessage(const FileFragment *f_) : TypeErrorMessage(f_) {}
  void formatA(std::ostream &os) const {
    os << "type error; recursive use of " << f->segment() << " requires return type";
  }
  void formatB(std::ostream &os) const { os << "but the function body actually returns type"; }
};

struct MatchArgErrorMessage : public TypeErrorMessage {
  MatchArgErrorMessage(const FileFragment *f_) : TypeErrorMessage(f_) {}
  void formatA(std::ostream &os) const {
    os << "type error; case analysis of " << f->segment() << " with type";
  }
  void formatB(std::ostream &os) const { os << "does not match the pattern requirement of type"; }
};

struct MatchResultErrorMessage : public TypeErrorMessage {
  const std::string &case0;
  const std::string &casen;
  MatchResultErrorMessage(const FileFragment *f_, const std::string &case0_,
                          const std::string &casen_)
      : TypeErrorMessage(f_), case0(case0_), casen(casen_) {}
  void formatA(std::ostream &os) const {
    os << "type error; case '" << casen << "' returns expression " << f->segment() << " of type";
  }
  void formatB(std::ostream &os) const {
    os << "which does not match case '" << case0 << "' which returned type";
  }
};

struct MatchTypeVarErrorMessage : public TypeErrorMessage {
  const std::string &casen;
  MatchTypeVarErrorMessage(const FileFragment *f_, const std::string &casen_)
      : TypeErrorMessage(f_), casen(casen_) {}
  void formatA(std::ostream &os) const {
    os << "type error; pattern for case '" << casen << "' expected type";
  }
  void formatB(std::ostream &os) const { os << "but the argument " << f->segment() << " has type"; }
};

struct ExploreState {
  const PrimMap &pmap;
  TypeMap typeVars;
  ExploreState(const PrimMap &pmap_) : pmap(pmap_) {}
};

struct OpenTypeVar : public ScopedTypeVar {
  TypeVar var;
  OpenTypeVar(const ScopedTypeVar &scoped) : ScopedTypeVar(scoped) {}
  bool operator<(const OpenTypeVar &o) const { return var < o.var; }
};

struct TypeScope {
  TypeScope(ExploreState &state_, const std::vector<ScopedTypeVar> &typeVars, const TypeVar &dob);
  ~TypeScope();

  ExploreState &state;
  std::vector<OpenTypeVar> vars;
};

TypeScope::TypeScope(ExploreState &state_, const std::vector<ScopedTypeVar> &typeVars,
                     const TypeVar &dob)
    : state(state_) {
  // reserve to keep pointers into array legal
  vars.reserve(typeVars.size());
  for (auto &var : typeVars) {
    auto out = state.typeVars.insert(std::make_pair(var.name, nullptr));
    if (!out.second) continue;
    vars.emplace_back(var);
    vars.back().var.setDOB(dob);
    out.first->second = &vars.back().var;
  }
}

TypeScope::~TypeScope() {
  for (auto &var : vars) state.typeVars.erase(var.name);

  std::sort(vars.begin(), vars.end());
  for (size_t i = 0; i < vars.size(); ++i) {
    OpenTypeVar &var = vars[i];
    if (!var.var.isFree()) {
      std::ostringstream message;
      message << "introduced type variable '" << var.name
              << "' is not free; it has type:" << std::endl
              << "    ";
      var.var.format(message, var.var);
      reporter->reportError(var.fragment.location(), message.str());
      continue;
    }
    if (i == 0) continue;
    OpenTypeVar &prior = vars[i - 1];
    if (prior.var == var.var) {
      std::ostringstream message;
      message << "introduced free type variables '" << prior.name << "' and '" << var.name
              << "' are actually the same";
      reporter->reportError(prior.fragment.location(), message.str());
      reporter->reportError(var.fragment.location(), message.str());
    }
  }
}

static bool explore(Expr *expr, ExploreState &state, NameBinding *binding) {
  if (!expr) return false;  // failed fracture
  expr->typeVar.setDOB();
  if (expr->type == &VarRef::type) {
    VarRef *ref = static_cast<VarRef *>(expr);
    NameRef pos;
    if ((pos = binding->find(ref->name)).index == -1) {
      ERROR(ref->fragment.location(), "reference to undefined variable '" << ref->name << "'");
      return false;
    }
    ref->index = pos.index;
    ref->lambda = pos.lambda;
    ref->target = pos.target;
    if (!pos.var) return true;
    if (pos.def) {
      TypeVar temp;
      pos.var->clone(temp);
      return ref->typeVar.unify(temp, &ref->fragment);
    } else {
      if (pos.lambda) ref->flags |= FLAG_RECURSIVE;
      return ref->typeVar.unify(*pos.var, &ref->fragment);
    }
  } else if (expr->type == &App::type) {
    App *app = static_cast<App *>(expr);
    binding->open = false;
    bool f = explore(app->fn.get(), state, binding);
    bool a = explore(app->val.get(), state, binding);
    // Even if fn is nullptr, this is just an offset calculation, not a dereference
    FnErrorMessage fnm(&app->fn->fragment);
    bool t = f && app->fn->typeVar.unify(TypeVar(FN, 2), &fnm);
    ArgErrorMessage argm(&app->fn->fragment, &app->val->fragment,
                         t ? app->fn->typeVar.getTag(0) : 0);
    bool ta = t && a && app->fn->typeVar[0].unify(app->val->typeVar, &argm);
    bool tr = t && app->fn->typeVar[1].unify(app->typeVar, &app->fragment);
    return f && a && t && ta && tr;
  } else if (expr->type == &Lambda::type) {
    Lambda *lambda = static_cast<Lambda *>(expr);
    bool t = lambda->typeVar.unify(TypeVar(FN, 2), &lambda->fragment);
    if (t && lambda->name != "_" && lambda->name.find(' ') == std::string::npos)
      lambda->typeVar.setTag(0, lambda->name.c_str());
    NameBinding bind(binding, lambda);
    bool out = explore(lambda->body.get(), state, &bind);
    // Even if body is nullptr, this is just an offset calculation, not a dereference
    RecErrorMessage recm(&lambda->body->fragment);
    bool tr = t && out && lambda->typeVar[1].unify(lambda->body->typeVar, &recm);
    return out && t && tr;
  } else if (expr->type == &DefBinding::type) {
    DefBinding *def = static_cast<DefBinding *>(expr);
    binding->open = false;
    NameBinding bind(binding, def);
    bool ok = true;
    for (unsigned i = 0; i < def->val.size(); ++i) {
      if (!def->val[i]) {
        ok = false;
        continue;
      }
      def->val[i]->typeVar.setDOB();
      TypeScope scope(state, def->valVars[i], def->val[i]->typeVar);
      ok = explore(def->val[i].get(), state, binding) && ok;
    }
    for (unsigned i = 0; i < def->fun.size(); ++i) {
      if (!def->fun[i]) {
        ok = false;
        continue;
      }
      def->fun[i]->typeVar.setDOB();
      for (unsigned j = i + 1; j < def->fun.size() && i == def->scc[j]; ++j)
        if (def->fun[j]) def->fun[j]->typeVar.setDOB(def->fun[i]->typeVar);
      TypeScope scope(state, def->funVars[i], def->fun[i]->typeVar);
      bind.generalized = def->val.size() + def->scc[i];
      ok = explore(def->fun[i].get(), state, &bind) && ok;
    }
    bind.generalized = def->val.size() + def->fun.size();
    ok = explore(def->body.get(), state, &bind) && ok;
    ok = ok && def->typeVar.unify(def->body->typeVar, &def->fragment);
    return ok;
  } else if (expr->type == &Literal::type) {
    Literal *lit = static_cast<Literal *>(expr);
    return lit->typeVar.unify(*lit->litType, &lit->fragment);
  } else if (expr->type == &Construct::type) {
    Construct *cons = static_cast<Construct *>(expr);
    bool ok = cons->typeVar.unify(TypeVar(cons->sum->name.c_str(), cons->sum->args.size()));
    TypeMap ids;
    for (size_t i = 0; i < cons->sum->args.size(); ++i) ids[cons->sum->args[i]] = &cons->typeVar[i];
    if (binding->lambda) {
      NameBinding *iter = binding;
      std::vector<AST> &v = cons->cons->ast.args;
      for (size_t i = v.size(); i; --i) {
        TypeVar &ty = iter->lambda->typeVar;
        ok = v[i - 1].unify(ty[0], ids) && ok;
        if (!v[i - 1].tag.empty()) ty.setTag(0, v[i - 1].tag.c_str());
        iter = iter->next;
      }
    } else {
      DefBinding::Values &vals = binding->binding->val;
      std::vector<AST> &v = cons->cons->ast.args;
      size_t num = v.size();
      for (size_t i = 0; i < num; ++i) ok = v[num - 1 - i].unify(vals[i]->typeVar, ids) && ok;
    }
    return ok;
  } else if (expr->type == &Destruct::type) {
    Destruct *des = static_cast<Destruct *>(expr);
    bool ok = explore(des->arg.get(), state, binding);
    if (ok) {
      MatchArgErrorMessage ma(&des->arg->fragment);
      ok = des->arg->typeVar.unify(TypeVar(des->sum->name.c_str(), des->sum->args.size()), &ma);
      for (size_t i = 0; i < des->cases.size(); ++i) {
        Lambda *lam = static_cast<Lambda *>(des->cases[i].get());
        bool c = explore(lam, state, binding);
        if (!c) {
          ok = false;
          continue;
        }
        MatchResultErrorMessage mr(&lam->fragment, des->sum->members[0].ast.name,
                                   des->sum->members[i].ast.name);
        ok = ok && lam->typeVar[1].unify(des->typeVar, &mr);
        MatchTypeVarErrorMessage tv(&des->arg->fragment, des->sum->members[i].ast.name);
        ok = ok && lam->typeVar[0].unify(des->arg->typeVar, &tv);
      }
    }
    return ok;
  } else if (expr->type == &Ascribe::type) {
    Ascribe *asc = static_cast<Ascribe *>(expr);
    bool b = explore(asc->body.get(), state, binding);
    bool ts = asc->signature.unify(asc->typeVar, state.typeVars);
    AscErrorMessage ascm(&asc->body_fragment);
    bool tb = asc->body && asc->body->typeVar.unify(asc->typeVar, &ascm);
    return b && tb && ts;
  } else if (expr->type == &Prim::type) {
    Prim *prim = static_cast<Prim *>(expr);
    std::vector<TypeVar *> args;
    for (NameBinding *iter = binding; iter && iter->open && iter->lambda; iter = iter->next)
      args.push_back(&iter->lambda->typeVar[0]);
    std::reverse(args.begin(), args.end());
    prim->args = args.size();
    PrimMap::const_iterator i = state.pmap.find(prim->name);
    if (i != state.pmap.end()) {
      prim->pflags = i->second.flags;
      prim->fn = i->second.fn;
      prim->data = i->second.data;
      bool ok = i->second.type(args, &prim->typeVar);
      if (!ok) {
        ERROR(prim->fragment.location(),
              "primitive '" << prim->name << "' is used with the wrong number of arguments");
      }
      return ok;
    } else if (state.pmap.size() > 10) {
      ERROR(prim->fragment.location(),
            "reference to unimplemented primitive '" << prim->name << "'");
      return false;
    } else {
      return true;
    }
  } else if (expr->type == &Get::type) {
    Get *get = static_cast<Get *>(expr);
    while (!binding->lambda) binding = binding->next;
    TypeVar &typ = binding->lambda->typeVar[0];
    bool ok = typ.unify(TypeVar(get->sum->name.c_str(), get->sum->args.size()));
    TypeMap ids;
    for (size_t i = 0; i < get->sum->args.size(); ++i) ids[get->sum->args[i]] = &typ[i];
    ok = get->cons->ast.args[get->index].unify(get->typeVar, ids) && ok;
    return ok;
  } else {
    assert(0 /* unreachable */);
    return false;
  }
}

std::unique_ptr<Expr> bind_refs(std::unique_ptr<Top> top, const PrimMap &pmap, bool &isTreeBuilt) {
  std::unique_ptr<Expr> out = fracture(std::move(top));
  NameBinding bottom;
  ExploreState state(pmap);
  if (out && !explore(out.get(), state, &bottom)) isTreeBuilt = false;
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
  std::string pkg(sym.qualified, at + 1);
  std::string def(sym.qualified, 0, at);

  if ((sym.flags & SYM_GRAY) != 0) {
    if (con.warn) {
      ERROR(sym.fragment.location(), "export of " << con.kind << " '" << def << "' from '" << pkg
                                                  << "' has cyclic definition");
    }
    return false;
  }

  auto ip = con.top.packages.find(pkg);
  if (ip == con.top.packages.end()) {
    if (con.warn) {
      ERROR(sym.fragment.location(), "export of " << con.kind << " '" << def
                                                  << "' from non-existent package '" << pkg << "'");
    }
    return false;
  } else {
    auto &map = con.member(ip->second->exports);
    auto ie = map.find(def);
    if (ie == map.end()) {
      if (con.warn) {
        ERROR(sym.fragment.location(),
              con.kind << " '" << def << "' is not exported by package '" << pkg << "'");
      }
      return false;
    }
    sym.flags |= SYM_GRAY;
    bool ok = contract(con, ie->second);
    sym.flags &= ~SYM_GRAY;
    sym.flags |= SYM_LEAF;
    sym.qualified = ie->second.qualified;
    if (!ie->second.origin
             .empty())  // builtin types have empty origin => keep export/import location
      sym.origin = ie->second.origin;
    return ok;
  }
}

struct DefContractor final : public Contractor {
  DefContractor(const Top &top, bool warn) : Contractor(top, warn, "definition") {}
  Symbols::SymbolMap &member(Symbols &sym) const override { return sym.defs; }
};

static bool contract_def(Top &top, SymbolSource &sym, bool warn) {
  DefContractor con(top, warn);
  return contract(con, sym);
}

struct TypeContractor final : public Contractor {
  TypeContractor(const Top &top, bool warn) : Contractor(top, warn, "type") {}
  Symbols::SymbolMap &member(Symbols &sym) const override { return sym.types; }
};

static bool contract_type(Top &top, SymbolSource &sym, bool warn) {
  TypeContractor con(top, warn);
  return contract(con, sym);
}

struct TopicContractor final : public Contractor {
  TopicContractor(const Top &top, bool warn) : Contractor(top, warn, "topic") {}
  Symbols::SymbolMap &member(Symbols &sym) const override { return sym.topics; }
};

static bool contract_topic(Top &top, SymbolSource &sym, bool warn) {
  TopicContractor con(top, warn);
  return contract(con, sym);
}

static bool sym_contract(Top &top, Symbols &symbols, bool warn) {
  bool ok = true;
  for (auto &d : symbols.defs)
    if (!contract_def(top, d.second, warn)) ok = false;
  for (auto &d : symbols.types)
    if (!contract_type(top, d.second, warn)) ok = false;
  for (auto &d : symbols.topics)
    if (!contract_topic(top, d.second, warn)) ok = false;
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
