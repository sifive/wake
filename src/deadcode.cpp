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

struct DefStack {
  Expr  *expr;
  DefStack *next;

  Expr *resolve(VarRef *ref);
  Expr *index(unsigned i); // flat index
  DefStack *unwind(unsigned &i);
  unsigned size() const;
};

unsigned DefStack::size() const {
  if (expr->type == &Lambda::type) return 1;
  return static_cast<DefBinding*>(expr)->val.size();
}

Expr *DefStack::resolve(VarRef *ref) {
  if (ref->lambda) return ref->lambda;
  return index(ref->index);
}

Expr *DefStack::index(unsigned i) {
  DefStack *s = this;
  size_t idx, size;
  for (idx = i; idx >= (size = s->size()); idx -= size)
    s = s->next;

  if (s->expr->type == &Lambda::type) return nullptr;
  return static_cast<DefBinding*>(s->expr)->val[idx].get();
}

DefStack *DefStack::unwind(unsigned &i) {
  DefStack *s = this;
  size_t size;
  for (; s && i >= (size = s->size()); i-= size)
    s = s->next;
  return s;
}

struct AppStack {
  std::vector<int> *expand;
  int cutoff;
  std::unique_ptr<Expr> arg;
  AppStack *next;
};

static Expr *clone(Expr *expr) {
  if (expr->type == &VarRef::type) {
    VarRef *var = static_cast<VarRef*>(expr);
    VarRef *out = new VarRef(*var);
    if (var->lambda && (var->lambda->flags & FLAG_MOVED))
      out->lambda = static_cast<Lambda*>(reinterpret_cast<void*>(var->lambda->meta));
    return out;
  } else if (expr->type == &App::type) {
    App *app = static_cast<App*>(expr);
    App *out = new App(*app);
    out->val = std::unique_ptr<Expr>(clone(app->val.get()));
    out->fn  = std::unique_ptr<Expr>(clone(app->fn .get()));
    return out;
  } else if (expr->type == &Lambda::type) {
    Lambda *lambda = static_cast<Lambda*>(expr);
    Lambda *out = new Lambda(*lambda);
    out->body = std::unique_ptr<Expr>(clone(lambda->body.get()));
    return out;
  } else if (expr->type == &DefBinding::type) {
    DefBinding *def = static_cast<DefBinding*>(expr);
    DefBinding *out = new DefBinding(*def);
    // create forwarding pointers for fun VarRefs
    for (auto &x : def->fun) {
      out->fun.emplace_back(new Lambda(*x));
      x->set(FLAG_MOVED, 1);
      x->meta = reinterpret_cast<uintptr_t>(static_cast<void*>(out->fun.back().get()));
    }
    for (auto &x : def->val)
      out->val.emplace_back(clone(x.get()));
    for (unsigned i = 0; i < def->fun.size(); ++i)
      out->fun[i]->body = std::unique_ptr<Expr>(clone(def->fun[i]->body.get()));
    out->body = std::unique_ptr<Expr>(clone(def->body.get()));
    for (unsigned i = 0; i < def->fun.size(); ++i) {
      def->fun[i]->set(FLAG_MOVED, 0);
      def->fun[i]->meta = out->fun[i]->meta;
    }
    return out;
  } else if (expr->type == &Literal::type) {
    return new Literal(*static_cast<Literal*>(expr));
  } else if (expr->type == &Construct::type) {
    return new Construct(*static_cast<Construct*>(expr));
  } else if (expr->type == &Destruct::type) {
    return new Destruct(*static_cast<Destruct*>(expr));
  } else if (expr->type == &Prim::type) {
    return new Prim(*static_cast<Prim*>(expr));
  } else if (expr->type == &Get::type) {
    return new Get(*static_cast<Get*>(expr));
  } else {
    assert(0 /* unreachable */);
    return nullptr;
  }
}

template <typename T, typename F>
std::unique_ptr<T> static_unique_pointer_cast(std::unique_ptr<F>&& x) {
  return std::unique_ptr<T>(static_cast<T*>(x.release()));
}

// meta indicates expression tree size to guide inlining threshold
static std::unique_ptr<Expr> forward_inline(std::unique_ptr<Expr> expr, AppStack *astack, DefStack *dstack, std::vector<int> &expand, int depth)  {
  if (expr->type == &VarRef::type) {
    VarRef *ref = static_cast<VarRef*>(expr.get());
    unsigned index = ref->index;
    ref->index = (depth-1) - expand[expand.size()-1-index];
    Expr *target = dstack->resolve(ref);
    if (target && target->type == &VarRef::type) {
      VarRef *sub = static_cast<VarRef*>(target);
      unsigned left = ref->index;
      DefStack *val = dstack->unwind(left);
      ref->index = (ref->index-left)+val->size()+sub->index;
      ref->lambda = sub->lambda;
      target = dstack->resolve(ref);
    }
    if (target && target->type == &Lambda::type && !(target->flags & FLAG_RECURSIVE) && astack && target->meta < 100) {
      unsigned undo = ref->index;
      DefStack *scope = dstack->unwind(undo);
      undo = ref->index - undo;
      if (!ref->lambda) // In case it's a val = lambda and not a fun = lambda
        undo += scope->size();
      size_t keep = depth - undo;
      std::vector<int> simple;
      simple.reserve(keep);
      for (unsigned i = 0; i < keep; ++i) simple.push_back(i);
      return forward_inline(std::unique_ptr<Expr>(clone(target)), astack, dstack, simple, depth);
    } else {
      ref->meta = 1;
      return expr;
    }
  } else if (expr->type == &App::type) {
    App *app = static_cast<App*>(expr.get());
    AppStack frame;
    frame.expand = &expand;
    frame.cutoff = expand.size();
    frame.arg = std::move(app->val);
    frame.next = astack;
    auto out = forward_inline(std::move(app->fn), &frame, dstack, expand, depth);
    if (frame.arg) {
      app->fn = std::move(out);
      app->val = forward_inline(std::move(frame.arg), nullptr, dstack, expand, depth);
      app->meta = 1 + app->fn->meta + app->val->meta;
      return expr;
    } else {
      return out;
    }
  } else if (expr->type == &Lambda::type) {
    DefStack frame;
    frame.next = dstack;
    Lambda *lambda = static_cast<Lambda*>(expr.get());
    if (astack) {
      // Transform App+Lambda => DefBinding
      std::unique_ptr<DefBinding> def(new DefBinding(lambda->location, nullptr));
      def->order.insert(std::make_pair(lambda->name, DefBinding::OrderValue(lambda->token, 0)));
      frame.expr = def.get();
      // Expand the argument in our outer scope
      std::vector<int> cut(astack->expand->begin(), astack->expand->begin()+astack->cutoff);
      def->val.emplace_back(forward_inline(std::move(astack->arg), nullptr, dstack, cut, depth));
      // Expand the body in the def scope
      expand.push_back(depth);
      def->body = forward_inline(std::move(lambda->body), astack->next, &frame, expand, depth+1);
      expand.pop_back();
      def->meta = 1 + def->body->meta + def->val[0]->meta;
      return def;
    } else {
      frame.expr = expr.get();
      expand.push_back(depth);
      lambda->body = forward_inline(std::move(lambda->body), nullptr, &frame, expand, depth+1);
      lambda->meta = lambda->body->meta + 1;
      expand.pop_back();
      return expr;
    }
  } else if (expr->type == &DefBinding::type) {
    DefBinding *def = static_cast<DefBinding*>(expr.get());
    DefStack frame;
    frame.next = dstack;
    frame.expr = def;
    for (auto &x : def->val)
      x = forward_inline(std::move(x), nullptr, dstack, expand, depth);
    for (unsigned i = 0; i < def->val.size(); ++i)
      expand.push_back(depth+i);
    depth += def->val.size();
    for (auto &x : def->fun)
      x = static_unique_pointer_cast<Lambda>(
        forward_inline(std::move(x), nullptr, &frame, expand, depth));
    def->body = forward_inline(std::move(def->body), astack, &frame, expand, depth);
    depth -= def->val.size();
    expand.resize(expand.size() - def->val.size());
    uintptr_t meta = 1 + def->body->meta;
    for (auto &x : def->val) meta += x->meta;
    for (auto &x : def->fun) meta += x->meta;
    def->meta = meta;
    return expr;
  } else { // Literal/Construct/Destruct/Prim/Get
    expr->meta = 1;
    return expr;
  }
}

// meta is a purity bitmask
static bool forward_purity(Expr *expr, DefStack *stack, bool first) {
  DefStack frame;
  frame.next = stack;
  frame.expr = expr;
  if (expr->type == &VarRef::type) {
    VarRef *ref = static_cast<VarRef*>(expr);
    Expr *target = stack->resolve(ref);
    // The VarRef itself has no effect, but applying it might
    ref->meta = (target?target->meta:0) | 1;
    ref->set(FLAG_PURE, 1);
    return false;
  } else if (expr->type == &App::type) {
    App *app = static_cast<App*>(expr);
    bool out = false;
    out = forward_purity(app->val.get(), stack, first) || out;
    out = forward_purity(app->fn.get(), stack, first) || out;
    app->meta = (app->fn->meta >> 1) & ~!(app->fn->meta & app->val->meta & 1);
    app->set(FLAG_PURE, app->meta & 1);
    return out;
  } else if (expr->type == &Lambda::type) {
    Lambda *lambda = static_cast<Lambda*>(expr);
    bool out = forward_purity(lambda->body.get(), &frame, first);
    lambda->meta = (lambda->body->meta << 1) | 1;
    lambda->set(FLAG_PURE, 1);
    return out;
  } else if (expr->type == &DefBinding::type) {
    DefBinding *def = static_cast<DefBinding*>(expr);
    bool out = false;
    // Record prior def purity for comparison to after iteration
    std::vector<uintptr_t> prior;
    prior.reserve(def->fun.size());
    for (auto &x : def->fun) {
      // Assume best-case (pure) recursive functions on initial pass
      if (first) x->meta = ~static_cast<uintptr_t>(0);
      prior.push_back(x->meta);
    }
    for (auto &x : def->val) out = forward_purity(x.get(), stack, first) || out;
    for (auto &x : def->fun) out = forward_purity(x.get(), &frame, first) || out;
    out = forward_purity(def->body.get(), &frame, first) || out;
    // Detect any changes to definition types
    for (unsigned i = 0; !out && i < prior.size(); ++i)
      out = prior[i] != def->fun[i]->meta;
    // Result only pure when all vals and body are pure
    uintptr_t isect = def->body->meta;
    for (auto &x : def->val) isect &= ~!(x->meta & 1);
    def->meta = isect;
    def->set(FLAG_PURE, def->meta & 1);
    return out;
  } else if (expr->type == &Literal::type) {
    Literal *lit = static_cast<Literal*>(expr);
    lit->meta = 1;
    lit->set(FLAG_PURE, 1);
    return false;
  } else if (expr->type == &Construct::type) {
    Construct *cons = static_cast<Construct*>(expr);
    cons->meta = 1;
    cons->set(FLAG_PURE, 1);
    return false;
  } else if (expr->type == &Destruct::type) {
    Destruct *des = static_cast<Destruct*>(expr);
    // Result only pure when all handlers are pure
    uintptr_t isect = ~static_cast<uintptr_t>(0);
    for (unsigned i = 0; i < des->sum->members.size(); ++i) {
      Expr *handler = stack->index(i+1);
      isect &= handler?handler->meta:1;
    }
    // The tuple will be evaluated
    Expr *tuple = stack->index(0);
    uintptr_t vmeta = tuple?tuple->meta:0;
    // Apply the handler
    des->meta = (isect >> 1) & ~!(isect & vmeta & 1);
    des->set(FLAG_PURE, des->meta&1);
    return false;
  } else if (expr->type == &Prim::type) {
    Prim *prim = static_cast<Prim*>(expr);
    prim->meta = (prim->pflags & PRIM_REMOVE) != 0;
    if ((prim->pflags & PRIM_TGET)) {
      Expr *fn = stack->index(0);
      if (fn) prim->meta = fn->meta >> 1;
    }
    prim->set(FLAG_PURE, prim->meta);
    return false;
  } else if (expr->type == &Get::type) {
    Get *get = static_cast<Get*>(expr);
    get->meta = 1;
    get->set(FLAG_PURE, 1);
    return false;
  } else {
    assert(0 /* unreachable */);
    return false;
  }
}

// We only explore DefBinding children with uses
static int backward_usage(Expr *expr, DefStack *stack) {
  DefStack frame;
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
    for (auto &x : def->val) {
      if (--out >= 0) x->set(FLAG_USED, 1);
    }
    for (unsigned i = def->fun.size(), j; i > 0; i = j) {
      unsigned scc = def->scc[i-1];
      bool used = false;
      for (j = i; j > 0 && scc == def->scc[j-1]; --j)
        used = (def->fun[j-1]->flags & FLAG_USED) || used;
      for (j = i; j > 0 && scc == def->scc[j-1]; --j) {
        def->fun[j-1]->set(FLAG_USED, used);
        if (used) {
          int fun = backward_usage(def->fun[j-1].get(), &frame);
          fun -= def->val.size();
          if (fun > out) out = fun;
        }
      }
    }
    for (auto it = def->val.rbegin(); it != def->val.rend(); ++it) {
      if (!((*it)->flags & FLAG_PURE)) (*it)->set(FLAG_USED, 1);
      if (!((*it)->flags & FLAG_USED)) continue;
      int val = backward_usage(it->get(), stack);
      if (val > out) out = val;
    }
    return out;
  } else if (expr->type == &Prim::type) {
    Prim *prim = static_cast<Prim*>(expr);
    return prim->args;
  } else if (expr->type == &Destruct::type) {
    Destruct *des = static_cast<Destruct*>(expr);
    return des->sum->members.size() + 1;
  } else if (expr->type == &Construct::type) {
    Construct *con = static_cast<Construct*>(expr);
    return con->cons->ast.args.size();
  } else if (expr->type == &Get::type) {
    return 1;
  } else if (expr->type == &Literal::type) {
    return 0;
  } else {
    assert(0 /* unreachable */);
    return 0;
  }
}

// need the reduction contraction relabel map
// compress = prefix sum of stack usage bitmap
static std::unique_ptr<Expr> forward_reduction(std::unique_ptr<Expr> expr, std::vector<int> &compress) {
  if (expr->type == &VarRef::type) {
    VarRef *ref = static_cast<VarRef*>(expr.get());
    ref->index = compress.back() - compress[compress.size()-1-ref->index];
    return expr;
  } else if (expr->type == &App::type) {
    App *app = static_cast<App*>(expr.get());
    app->val = forward_reduction(std::move(app->val), compress);
    app->fn = static_unique_pointer_cast<Lambda>(forward_reduction(std::move(app->fn), compress));
    return expr;
  } else if (expr->type == &Lambda::type) {
    Lambda *lambda = static_cast<Lambda*>(expr.get());
    compress.push_back(compress.back()+1);
    lambda->body = forward_reduction(std::move(lambda->body), compress);
    compress.pop_back();
    return expr;
  } else if (expr->type == &DefBinding::type) {
    DefBinding *def = static_cast<DefBinding*>(expr.get());
    DefBinding::Values val;
    DefBinding::Functions fun;
    std::vector<DefBinding::Order::iterator> refs(def->order.size());
    std::vector<unsigned> scc;
    for (auto it = def->order.begin(); it != def->order.end(); ++it) {
      refs[it->second.index] = it;
    }
    for (auto &x : def->val) {
      if ((x->flags & FLAG_USED))
        x = forward_reduction(std::move(x), compress);
    }
    for (auto it = def->val.rbegin(); it != def->val.rend(); ++it) {
      int bump = ((*it)->flags&FLAG_USED)?1:0;
      compress.push_back(compress.back() + bump);
    }
    for (unsigned i = 0; i < def->fun.size(); ++i) {
      if ((def->fun[i]->flags & FLAG_USED))
        scc.push_back(def->scc[i]);
    }
    unsigned kept = 0, index = 0;
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
        x = static_unique_pointer_cast<Lambda>(forward_reduction(std::move(x), compress));
        fun.emplace_back(std::move(x));
        refs[index++]->second.index = kept++;
      } else {
        def->order.erase(refs[index++]);
      }
    }
    def->body = forward_reduction(std::move(def->body), compress);
    compress.resize(compress.size() - def->val.size());
    def->val = std::move(val);
    def->fun = std::move(fun);
    def->scc = std::move(scc);
    return expr;
  } else { // Literal/Construct/Destruct/Prim/Get
    return expr;
  }
}

std::unique_ptr<Expr> optimize_deadcode(std::unique_ptr<Expr> expr) {
  std::vector<int> expand;
  expand.push_back(0);
  expr = forward_inline(std::move(expr), nullptr, nullptr, expand, 1);
  // Find the purity fixed-point (typically only needs 2 passes)
  for (bool first = true; forward_purity(expr.get(), nullptr, first); first = false) { }
  backward_usage(expr.get(), nullptr);
  std::vector<int> compress;
  compress.push_back(0);
  return forward_reduction(std::move(expr), compress);
}
