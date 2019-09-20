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

static void doit(TermRewriter &state, TermStack *stack, Expr *expr) {
  TermStack frame;
  frame.next = stack;
  frame.expr = expr;
  if (expr->type == &VarRef::type) {
    VarRef *ref = static_cast<VarRef*>(expr);
    ref->meta = stack->resolve(ref);
  } else if (expr->type == &App::type) {
    App *app = static_cast<App*>(expr);
    doit(state, stack, app->fn .get());
    doit(state, stack, app->val.get());
    RApp *out = new RApp();
    out->args.push_back(app->fn->meta);
    out->args.push_back(app->val->meta);
    app->meta = state.insert(std::unique_ptr<Term>(out));
  } else if (expr->type == &Lambda::type) {
    Lambda *lambda = static_cast<Lambda*>(expr);
    RFun *fun = new RFun(lambda->fnname.empty() ? "anon" : lambda->fnname.c_str());
    lambda->meta = state.insert(std::unique_ptr<Term>(fun));
    CheckPoint cp = state.begin();
    state.insert(std::unique_ptr<Term>(new RArg(lambda->name.c_str())));
    doit(state, &frame, lambda->body.get());
    fun->output = lambda->body->meta;
    fun->terms = state.end(cp);
  } else if (expr->type == &DefBinding::type) {
    DefBinding *def = static_cast<DefBinding*>(expr);
    for (auto &x : def->val) doit(state, stack, x.get());
    for (auto &x : def->fun) x->meta = Term::invalid;
    for (auto &x : def->fun) doit(state, &frame, x.get()); // !!! mutual bad
    for (auto &x : def->order) {
      int i = x.second.index, n = def->val.size();
      Expr *what = i>=n ? def->fun[i-n].get() : def->val[i].get();
      state[what->meta]->label = x.first;
    }
    doit(state, &frame, def->body.get());
    def->meta = def->body->meta;
  } else if (expr->type == &Literal::type) {
    Literal *lit = static_cast<Literal*>(expr);
    lit->meta = state.insert(std::unique_ptr<Term>(new RLit(lit->value)));
  } else if (expr->type == &Construct::type) {
    Construct *con = static_cast<Construct*>(expr);
    RCon *out = new RCon(con->cons->index);
    for (unsigned i = con->cons->ast.args.size(); i > 0; --i)
      out->args.push_back(stack->index(i-1));
    con->meta = state.insert(std::unique_ptr<Term>(out));
  } else if (expr->type == &Destruct::type) {
    Destruct *des = static_cast<Destruct*>(expr);
    RDes *out = new RDes();
    for (unsigned i = des->sum->members.size()+1; i > 0; --i)
      out->args.push_back(stack->index(i-1));
    des->meta = state.insert(std::unique_ptr<Term>(out));
  } else if (expr->type == &Prim::type) {
    Prim *prim = static_cast<Prim*>(expr);
    RPrim *out = new RPrim(prim->name.c_str(), prim->fn, prim->data, prim->pflags);
    for (unsigned i = prim->args; i > 0; --i)
      out->args.push_back(stack->index(i-1));
    prim->meta = state.insert(std::unique_ptr<Term>(out));
  } else if (expr->type == &Get::type) {
    Get *get = static_cast<Get*>(expr);
    RGet *out = new RGet(get->index);
    out->args.push_back(stack->index(0));
    get->meta = state.insert(std::unique_ptr<Term>(out));
  } else {
    assert(0 /* unreachable */);
  }
}
std::unique_ptr<Term> Term::fromExpr(std::unique_ptr<Expr> expr) {
  TermRewriter state;
  RFun *out = new RFun("top");
  state.insert(std::unique_ptr<Term>(out));
  CheckPoint cp = state.begin();
  doit(state, nullptr, expr.get());
  out->output = expr->meta;
  out->terms = state.end(cp);
  return state.finish();
}
