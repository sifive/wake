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

#include "optimize.h"
#include "expr.h"
#include "prim.h"
#include <assert.h>

struct AppStack {
  int cutoff;
  Expr *arg;
  AppStack *next;
};

static Expr *lambda_fusion(Expr *expr, AppStack *stack, std::vector<int> &expand, int depth)  {
  if (expr->type == &VarRef::type) {
    VarRef *ref = static_cast<VarRef*>(expr);
    ref->index = (depth-1) - expand[expand.size()-1-ref->index];
    return ref;
  } else if (expr->type == &App::type) {
    App *app = static_cast<App*>(expr);
    AppStack frame;
    frame.cutoff = expand.size();
    frame.arg = app->val.release();
    frame.next = stack;
    Expr *out = lambda_fusion(app->fn.release(), &frame, expand, depth);
    if (frame.arg) {
      app->fn = std::unique_ptr<Expr>(out);
      app->val = std::unique_ptr<Expr>(lambda_fusion(frame.arg, nullptr, expand, depth));
      return app;
    } else {
      return out;
    }
  } else if (expr->type == &Lambda::type) {
    Lambda *lambda = static_cast<Lambda*>(expr);
    if (stack) {
      std::vector<int> cut(expand.begin(), expand.begin()+stack->cutoff);
      expand.push_back(depth);
      auto val = lambda_fusion(stack->arg, nullptr, cut, depth);
      auto body = lambda_fusion(lambda->body.release(), stack->next, expand, depth+1);
      DefBinding *def = new DefBinding(lambda->location, std::unique_ptr<Expr>(body));
      def->order.insert(std::make_pair(lambda->name, DefBinding::OrderValue(lambda->token, 0)));
      def->val.emplace_back(std::unique_ptr<Expr>(val));
      stack->arg = nullptr;
      expand.pop_back();
      return def;
    } else {
      expand.push_back(depth);
      auto y = lambda_fusion(lambda->body.release(), nullptr, expand, depth+1);
      expand.pop_back();
      lambda->body = std::unique_ptr<Expr>(y);
      return lambda;
    }
  } else if (expr->type == &DefBinding::type) {
    DefBinding *def = static_cast<DefBinding*>(expr);
    for (auto &x : def->val) {
      auto y = lambda_fusion(x.release(), nullptr, expand, depth);
      x = std::unique_ptr<Expr>(y);
    }
    for (unsigned i = 0; i < def->val.size(); ++i)
      expand.push_back(depth+i);
    depth += def->val.size();
    for (auto &x : def->fun) {
      auto y = lambda_fusion(x.release(), nullptr, expand, depth);
      x = std::unique_ptr<Lambda>(static_cast<Lambda*>(y));
    }
    auto y = lambda_fusion(def->body.release(), stack, expand, depth);
    def->body = std::unique_ptr<Expr>(y);
    depth -= def->val.size();
    expand.resize(expand.size() - def->val.size());
    return def;
  } else { // Literal/Construct/Destruct/Prim/Get
    return expr;
  }
}

struct ScopeStack {
  Expr  *expr;
  ScopeStack *next;

  Expr *resolve(VarRef *ref);
  Expr *index(unsigned i); // flat index
  unsigned size() const;
};

unsigned ScopeStack::size() const {
  if (expr->type == &Lambda::type) return 1;
  return static_cast<DefBinding*>(expr)->val.size();
}

Expr *ScopeStack::resolve(VarRef *ref) {
  if (ref->lambda) return ref->lambda;
  return index(ref->index);
}

Expr *ScopeStack::index(unsigned i) {
  ScopeStack *s = this;
  size_t idx, size;
  for (idx = i; idx >= (size = s->size()); idx -= size)
    s = s->next;

  if (s->expr->type == &Lambda::type) return nullptr;
  return static_cast<DefBinding*>(s->expr)->val[idx].get();
}

// meta is a purity bitmask
static void forward_purity(Expr *expr, ScopeStack *stack) {
  ScopeStack frame;
  frame.next = stack;
  frame.expr = expr;
  if (expr->type == &VarRef::type) {
    VarRef *ref = static_cast<VarRef*>(expr);
    Expr *target = stack->resolve(ref);
    // The VarRef itself has no effect, but applying it might
    ref->meta = (target?target->meta:0) | 1;
    // I choose to allow recursive functions to be pure unless they have
    // actual side-effects.  This does mean we might eliminate an unused
    // result computed by a pure function which runs forever.
    if ((ref->flags & FLAG_RECURSIVE))
      ref->meta = ~static_cast<uint64_t>(0);
    ref->set(FLAG_PURE, 1);
  } else if (expr->type == &App::type) {
    App *app = static_cast<App*>(expr);
    forward_purity(app->val.get(), stack);
    forward_purity(app->fn.get(), stack);
    app->meta = (app->fn->meta >> 1) & ~!(app->fn->meta & app->val->meta & 1);
    app->set(FLAG_PURE, expr->meta & 1);
  } else if (expr->type == &Lambda::type) {
    Lambda *lambda = static_cast<Lambda*>(expr);
    forward_purity(lambda->body.get(), &frame);
    lambda->meta = (lambda->body->meta << 1) | 1;
    lambda->set(FLAG_PURE, 1);
  } else if (expr->type == &DefBinding::type) {
    DefBinding *def = static_cast<DefBinding*>(expr);
    for (auto &x : def->val) forward_purity(x.get(), stack);
    for (auto &x : def->fun) forward_purity(x.get(), &frame);
    forward_purity(def->body.get(), &frame);
    // Result only pure when all vals and body are pure
    uint64_t isect = def->body->meta;
    for (auto &x : def->val) isect &= x->meta;
    def->meta = isect;
    def->set(FLAG_PURE, def->meta & 1);
  } else if (expr->type == &Literal::type) {
    Literal *lit = static_cast<Literal*>(expr);
    lit->meta = 1;
    lit->set(FLAG_PURE, 1);
  } else if (expr->type == &Construct::type) {
    Construct *cons = static_cast<Construct*>(expr);
    cons->meta = 1;
    cons->set(FLAG_PURE, 1);
  } else if (expr->type == &Destruct::type) {
    Destruct *des = static_cast<Destruct*>(expr);
    // Result only pure when all handlers are pure
    uint64_t isect = ~static_cast<uint64_t>(0);
    for (unsigned i = 0; i < des->sum->members.size(); ++i) {
      Expr *handler = stack->index(i+1);
      isect &= handler?handler->meta:1;
    }
    // The tuple will be evaluated
    Expr *tuple = stack->index(0);
    uint64_t vmeta = tuple?tuple->meta:0;
    // Apply the handler
    des->meta = (isect >> 1) & ~!(isect & vmeta & 1);
    des->set(FLAG_PURE, des->meta&1);
  } else if (expr->type == &Prim::type) {
    Prim *prim = static_cast<Prim*>(expr);
    prim->meta = (prim->pflags & PRIM_PURE) != 0;
    prim->set(FLAG_PURE, prim->meta);
  } else if (expr->type == &Get::type) {
    Get *get = static_cast<Get*>(expr);
    get->meta = 1;
    get->set(FLAG_PURE, 1);
  } else {
    assert(0 /* unreachable */);
  }
}

// We only explore DefBinding children with uses
static int backward_usage(Expr *expr, ScopeStack *stack) {
  ScopeStack frame;
  frame.next = stack;
  frame.expr = expr;
  if (expr->type == &VarRef::type) {
    VarRef *ref = static_cast<VarRef*>(expr);
    Expr *target = stack->resolve(ref);
    if (target) target->set(FLAG_USED, 1);
    return 0;
  } else if (expr->type == &App::type) {
    App *app = static_cast<App*>(expr);
    backward_usage(app->fn.get(), stack);
    backward_usage(app->val.get(), stack);
    return 0;
  } else if (expr->type == &Lambda::type) {
    Lambda *lambda = static_cast<Lambda*>(expr);
    return backward_usage(lambda->body.get(), &frame) - 1;
  } else if (expr->type == &DefBinding::type) {
    DefBinding *def = static_cast<DefBinding*>(expr);
    for (auto &x : def->val) x->set(FLAG_USED, 0);
    for (auto &x : def->fun) x->set(FLAG_USED, 0);
    int out = backward_usage(def->body.get(), &frame);
    for (auto it = def->fun.rbegin(); it != def->fun.rend(); ++it) {
      if (!((*it)->flags & FLAG_USED)) continue;
      backward_usage(it->get(), &frame);
    }
    for (auto it = def->val.rbegin(); it != def->val.rend(); ++it) {
      if (!((*it)->flags & FLAG_PURE)) (*it)->set(FLAG_USED, 1);
      if (!((*it)->flags & FLAG_USED)) continue;
      backward_usage(it->get(), stack);
    }
    for (auto &x : def->val) {
      if (--out >= 0) x->set(FLAG_USED, 1);
    }
    return out;
  } else if (expr->type == &Prim::type) {
    Prim *prim = static_cast<Prim*>(expr);
    return prim->args;
  } // else: Literal/Construct/Destruct/Get
}

// need the reduction contraction relabel map
// compress = prefix sum of stack usage bitmap
static void forward_reduction(Expr *expr, std::vector<int> &compress) {
  if (expr->type == &VarRef::type) {
    VarRef *ref = static_cast<VarRef*>(expr);
    ref->index = compress.back() - compress[compress.size()-1-ref->index];
  } else if (expr->type == &App::type) {
    App *app = static_cast<App*>(expr);
    forward_reduction(app->val.get(), compress);
    forward_reduction(app->fn.get(), compress);
  } else if (expr->type == &Lambda::type) {
    Lambda *lambda = static_cast<Lambda*>(expr);
    compress.push_back(compress.back()+1);
    forward_reduction(lambda->body.get(), compress);
    compress.pop_back();
  } else if (expr->type == &DefBinding::type) {
    DefBinding *def = static_cast<DefBinding*>(expr);
    DefBinding::Values val;
    DefBinding::Functions fun;
    std::vector<DefBinding::Order::iterator> refs(def->order.size());
    for (auto it = def->order.begin(); it != def->order.end(); ++it) {
      refs[it->second.index] = it;
    }
    for (auto &x : def->val) {
      if ((x->flags & FLAG_USED)) {
        forward_reduction(x.get(), compress);
      }
    }
    for (auto it = def->val.rbegin(); it != def->val.rend(); ++it) {
      int bump = ((*it)->flags&FLAG_USED)?1:0;
      compress.push_back(compress.back() + bump);
    }
    int kept = 0, index = 0;
    for (auto &x : def->val) {
      if ((x->flags & FLAG_USED)) {
        val.emplace_back(std::move(x));
        refs[index++]->second.index = kept++;
      } else {
        def->order.erase(refs[index++]);
      }
    }
    for (auto &x : def->fun) {
      if ((x->flags & FLAG_USED)) {
        forward_reduction(x.get(), compress);
        fun.emplace_back(std::move(x));
        refs[index++]->second.index = kept++;
      } else {
        def->order.erase(refs[index++]);
      }
    }
    forward_reduction(def->body.get(), compress);
    compress.resize(compress.size() - def->val.size());
    def->val = std::move(val);
    def->fun = std::move(fun);
  } // else: Literal/Construct/Destruct/Prim/Get
}

void optimize_deadcode(Expr *expr) {
  std::vector<int> expand;
  lambda_fusion(expr, nullptr, expand, 0);
  forward_purity(expr, nullptr);
  backward_usage(expr, nullptr);
  std::vector<int> compress;
  compress.push_back(0);
  forward_reduction(expr, compress);
}
