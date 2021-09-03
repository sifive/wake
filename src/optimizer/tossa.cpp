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

#include <algorithm>
#include <cassert>

#include "ssa.h"
#include "dst/expr.h"
#include "types/data.h"
#include "runtime/value.h"
#include "runtime/runtime.h"

static CPPFile cppFile(__FILE__);

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

struct ToSSACommon {
  Runtime &runtime;
  TargetScope &scope;
  ToSSACommon(Runtime &runtime_, TargetScope &scope_)
   : runtime(runtime_), scope(scope_) { }
};

static void doit(ToSSACommon common, TermStack *stack, Expr *expr) {
  TermStack frame;
  frame.next = stack;
  frame.expr = expr;
  if (expr->type == &VarRef::type) {
    VarRef *ref = static_cast<VarRef*>(expr);
    ref->meta = stack->resolve(ref);
  } else if (expr->type == &App::type) {
    App *app = static_cast<App*>(expr);
    doit(common, stack, app->fn .get());
    doit(common, stack, app->val.get());
    app->meta = common.scope.append(new RApp(app->fn->meta, app->val->meta));
  } else if (expr->type == &Lambda::type) {
    Lambda *lambda = static_cast<Lambda*>(expr);
    size_t flags = (lambda->flags & FLAG_RECURSIVE) ? SSA_RECURSIVE : 0;
    RFun *fun = new RFun(lambda->body->fragment, lambda->fnname.empty() ? "anon" : lambda->fnname.c_str(), flags);
    lambda->meta = common.scope.append(fun);
    size_t cp = common.scope.append(new RArg(lambda->name.c_str()));
    doit(common, &frame, lambda->body.get());
    fun->output = lambda->body->meta;
    fun->terms = common.scope.unwind(cp);
  } else if (expr->type == &DefBinding::type) {
    DefBinding *def = static_cast<DefBinding*>(expr);
    for (auto &x : def->val) doit(common, stack, x.get());
    for (unsigned i = 0, j; i < def->fun.size(); i = j) {
      unsigned scc = def->scc[i];
      for (j = i+1; j < def->fun.size() && scc == def->scc[j]; ++j) { }
      if (j == i+1) {
        doit(common, &frame, def->fun[i].get());
      } else {
        RFun *mutual = new RFun(FRAGMENT_CPP_LINE, "mutual", SSA_RECURSIVE);
        size_t mid = common.scope.append(mutual);
        size_t mcp = common.scope.append(new RArg("_"));
        for (j = i; j < def->fun.size() && scc == def->scc[j]; ++j) {
          RFun *proxy = new RFun(def->fun[j]->body->fragment, "proxy", 0);
          def->fun[j]->meta = common.scope.append(proxy);
          size_t x = common.scope.append(new RArg("_"));
          size_t a = common.scope.append(new RApp(mid, mid));
          size_t g = common.scope.append(new RGet(j-i, a));
          proxy->output = common.scope.append(new RApp(g, x));
          proxy->terms = common.scope.unwind(x);
        }
        std::vector<size_t> imp;
        for (j = i; j < def->fun.size() && scc == def->scc[j]; ++j) {
          doit(common, &frame, def->fun[j].get());
          imp.push_back(def->fun[j]->meta);
        }
        auto random = std::make_shared<int>(0);
        auto cons = std::shared_ptr<Constructor>(random, &Constructor::array);
        mutual->output = common.scope.append(new RCon(std::move(cons), std::move(imp)));
        mutual->terms = common.scope.unwind(mcp);
        size_t tid = common.scope.append(new RApp(mid, mid));
        for (j = i; j < def->fun.size() && scc == def->scc[j]; ++j) {
          def->fun[j]->meta = common.scope.append(new RGet(j-i, tid));
        }
      }
    }
    for (auto &x : def->order) {
      int i = x.second.index, n = def->val.size();
      Expr *what = i>=n ? def->fun[i-n].get() : def->val[i].get();
      if (common.scope[what->meta]->label.empty())
        common.scope[what->meta]->label = x.first;
    }
    doit(common, &frame, def->body.get());
    def->meta = def->body->meta;
  } else if (expr->type == &Ascribe::type) {
    Ascribe *asc = static_cast<Ascribe*>(expr);
    doit(common, stack, asc->body.get());
    asc->meta = asc->body->meta;
  } else if (expr->type == &Literal::type) {
    Literal *lit = static_cast<Literal*>(expr);
    if (lit->litType == &Data::typeString) {
      lit->meta = common.scope.append(new RLit(std::make_shared<RootPointer<Value>>(String::literal(common.runtime.heap, lit->value))));
    } else if (lit->litType == &Data::typeRegExp) {
      lit->meta = common.scope.append(new RLit(std::make_shared<RootPointer<Value>>(RegExp::literal(common.runtime.heap, lit->value))));
    } else if (lit->litType == &Data::typeInteger) {
      lit->meta = common.scope.append(new RLit(std::make_shared<RootPointer<Value>>(Integer::literal(common.runtime.heap, lit->value))));
    } else if (lit->litType == &Data::typeDouble) {
      lit->meta = common.scope.append(new RLit(std::make_shared<RootPointer<Value>>(Double::literal(common.runtime.heap, lit->value.c_str()))));
    } else {
      assert(0);
    }
  } else if (expr->type == &Construct::type) {
    Construct *con = static_cast<Construct*>(expr);
    std::vector<size_t> args;
    for (unsigned i = con->cons->ast.args.size(); i > 0; --i)
      args.push_back(stack->index(i-1));
    con->meta = common.scope.append(new RCon(std::shared_ptr<Constructor>(con->sum, con->cons), std::move(args)));
  } else if (expr->type == &Destruct::type) {
    Destruct *des = static_cast<Destruct*>(expr);
    std::vector<size_t> args;
    for (auto &x : des->cases) {
      doit(common, stack, x.get());
      args.push_back(x->meta);
    }
    doit(common, stack, des->arg.get());
    args.push_back(des->arg->meta);
    des->meta = common.scope.append(new RDes(std::move(args)));
  } else if (expr->type == &Prim::type) {
    Prim *prim = static_cast<Prim*>(expr);
    std::vector<size_t> args;
    for (unsigned i = prim->args; i > 0; --i)
      args.push_back(stack->index(i-1));
    prim->meta = common.scope.append(
      new RPrim(prim->name.c_str(), prim->fn, prim->data, prim->pflags, std::move(args)));
  } else if (expr->type == &Get::type) {
    Get *get = static_cast<Get*>(expr);
    get->meta = common.scope.append(new RGet(get->index, stack->index(0)));
  } else {
    assert(0 /* unreachable */);
  }
}

std::unique_ptr<Term> Term::fromExpr(std::unique_ptr<Expr> expr, Runtime &runtime) {
  TargetScope scope;
  ToSSACommon common(runtime, scope);
  RFun *out = new RFun(FRAGMENT_CPP_LINE, "top", 0);
  size_t cp = scope.append(out);
  scope.append(new RArg("_"));
  doit(common, nullptr, expr.get());
  out->output = expr->meta;
  out->terms = scope.unwind(cp+1);
  return scope.finish();
}
