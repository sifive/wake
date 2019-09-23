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

#include "ssa.h"
#include "expr.h"
#include <algorithm>
#include <assert.h>

struct TermStack {
  Expr  *expr;
  TermStack *next;

  unsigned size() const;
  size_t resolve(VarRef *ref);
  size_t index(unsigned i); // flat index
};

unsigned TermStack::size() const {
  if (expr->type == &Lambda::type) return 1;
  return static_cast<DefBinding*>(expr)->val.size();
}

size_t TermStack::resolve(VarRef *ref) {
  if (ref->lambda) return ref->lambda->meta;
  return index(ref->index);
}

size_t TermStack::index(unsigned i) {
  TermStack *s = this;
  size_t idx, size;
  for (idx = i; idx >= (size = s->size()); idx -= size)
    s = s->next;

  if (s->expr->type == &Lambda::type) return s->expr->meta+1;
  return static_cast<DefBinding*>(s->expr)->val[idx]->meta;
}

static void doit(TargetScope &scope, TermStack *stack, Expr *expr) {
  TermStack frame;
  frame.next = stack;
  frame.expr = expr;
  if (expr->type == &VarRef::type) {
    VarRef *ref = static_cast<VarRef*>(expr);
    ref->meta = stack->resolve(ref);
  } else if (expr->type == &App::type) {
    App *app = static_cast<App*>(expr);
    doit(scope, stack, app->fn .get());
    doit(scope, stack, app->val.get());
    app->meta = scope.append(new RApp(app->fn->meta, app->val->meta));
  } else if (expr->type == &Lambda::type) {
    Lambda *lambda = static_cast<Lambda*>(expr);
    size_t flags = (lambda->flags & FLAG_RECURSIVE) ? RFUN_RECURSIVE : 0;
    RFun *fun = new RFun(lambda->body->location, lambda->fnname.empty() ? "anon" : lambda->fnname.c_str(), flags);
    lambda->meta = scope.append(fun);
    size_t cp = scope.append(new RArg(lambda->name.c_str()));
    doit(scope, &frame, lambda->body.get());
    fun->output = lambda->body->meta;
    fun->terms = scope.unwind(cp);
  } else if (expr->type == &DefBinding::type) {
    DefBinding *def = static_cast<DefBinding*>(expr);
    for (auto &x : def->val) doit(scope, stack, x.get());
    for (unsigned i = 0, j; i < def->fun.size(); i = j) {
      unsigned scc = def->scc[i];
      for (j = i+1; j < def->fun.size() && scc == def->scc[j]; ++j) { }
      if (j == i+1) {
        doit(scope, &frame, def->fun[i].get());
      } else {
        size_t null = 1;
        RFun *mutual = new RFun(LOCATION, "mutual", RFUN_RECURSIVE);
        size_t mid = scope.append(mutual);
        size_t mcp = scope.append(new RArg("_"));
        for (j = i; j < def->fun.size() && scc == def->scc[j]; ++j) {
          RFun *proxy = new RFun(def->fun[j]->body->location, "proxy", 0);
          def->fun[j]->meta = scope.append(proxy);
          size_t x = scope.append(new RArg("_"));
          size_t a = scope.append(new RApp(mid, null));
          size_t g = scope.append(new RGet(j-i, a));
          proxy->output = scope.append(new RApp(g, x));
          proxy->terms = scope.unwind(x);
        }
        std::vector<size_t> imp;
        for (j = i; j < def->fun.size() && scc == def->scc[j]; ++j) {
          doit(scope, &frame, def->fun[j].get());
          imp.push_back(def->fun[j]->meta);
        }
        auto random = std::make_shared<int>(0);
        auto cons = std::shared_ptr<Constructor>(random, &Constructor::array);
        mutual->output = scope.append(new RCon(std::move(cons), std::move(imp)));
        mutual->terms = scope.unwind(mcp);
        size_t tid = scope.append(new RApp(mid, null));
        for (j = i; j < def->fun.size() && scc == def->scc[j]; ++j) {
          def->fun[j]->meta = scope.append(new RGet(j-i, tid));
        }
      }
    }
    for (auto &x : def->order) {
      int i = x.second.index, n = def->val.size();
      Expr *what = i>=n ? def->fun[i-n].get() : def->val[i].get();
      if (scope[what->meta]->label.empty())
        scope[what->meta]->label = x.first;
    }
    doit(scope, &frame, def->body.get());
    def->meta = def->body->meta;
  } else if (expr->type == &Literal::type) {
    Literal *lit = static_cast<Literal*>(expr);
    lit->meta = scope.append(new RLit(lit->value));
  } else if (expr->type == &Construct::type) {
    Construct *con = static_cast<Construct*>(expr);
    std::vector<size_t> args;
    for (unsigned i = con->cons->ast.args.size(); i > 0; --i)
      args.push_back(stack->index(i-1));
    con->meta = scope.append(new RCon(std::shared_ptr<Constructor>(con->sum, con->cons), std::move(args)));
  } else if (expr->type == &Destruct::type) {
    Destruct *des = static_cast<Destruct*>(expr);
    std::vector<size_t> args;
    for (unsigned i = des->sum->members.size()+1; i > 0; --i)
      args.push_back(stack->index(i-1));
    des->meta = scope.append(new RDes(std::move(args)));
  } else if (expr->type == &Prim::type) {
    Prim *prim = static_cast<Prim*>(expr);
    std::vector<size_t> args;
    for (unsigned i = prim->args; i > 0; --i)
      args.push_back(stack->index(i-1));
    prim->meta = scope.append(
      new RPrim(prim->name.c_str(), prim->fn, prim->data, prim->pflags, std::move(args)));
  } else if (expr->type == &Get::type) {
    Get *get = static_cast<Get*>(expr);
    get->meta = scope.append(new RGet(get->index, stack->index(0)));
  } else {
    assert(0 /* unreachable */);
  }
}

std::unique_ptr<Term> Term::fromExpr(std::unique_ptr<Expr> expr) {
  TargetScope scope;
  RFun *out = new RFun(LOCATION, "top", 0);
  size_t cp = scope.append(out);
  doit(scope, nullptr, expr.get());
  out->output = expr->meta;
  out->terms = scope.unwind(cp+1);
  return scope.finish();
}
