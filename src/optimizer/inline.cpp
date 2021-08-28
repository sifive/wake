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

#include <assert.h>

#include <unordered_map>

#include "ssa.h"
#include "runtime/runtime.h"

struct DeepHash {
  Runtime *runtime;
  DeepHash(Runtime *runtime_) : runtime(runtime_) { }

  size_t operator ()(const std::shared_ptr<RootPointer<Value> > &x) const {
    return (*x)->deep_hash(runtime->heap).mix();
  }
  bool operator ()(const std::shared_ptr<RootPointer<Value> > &x, const std::shared_ptr<RootPointer<Value> > &y) const {
    return (*x)->deep_hash(runtime->heap) == (*y)->deep_hash(runtime->heap);
  }
};

typedef std::unordered_map<std::shared_ptr<RootPointer<Value> >, size_t, DeepHash, DeepHash> ConstantPool;

// In this pass, meta = the size of the AST & number of unapplied args
static size_t make_meta(size_t size, size_t args) { return (size << 8) | args; }
static size_t meta_size(size_t meta) { return meta >> 8; }
static size_t meta_args(size_t meta) { return meta & 255; }

struct PassInlineCommon {
  TargetScope scope;
  ConstantPool pool;
  size_t threshold;

  PassInlineCommon(Runtime *runtime, size_t threshold_) :
    scope(), pool(128, DeepHash(runtime), DeepHash(runtime)), threshold(threshold_) { }
  Runtime *runtime() { return pool.hash_function().runtime; }
};

struct PassInline {
  PassInlineCommon &common;
  TermStream stream;

  PassInline(PassInlineCommon &common_, size_t start = 0)
   : common(common_), stream(common.scope, start) { }
};

void RArg::pass_inline(PassInline &p, std::unique_ptr<Term> self) {
  meta = make_meta(1, 0); // we don't know the number unapplied args, but 0 prevents inlining anyway
  p.stream.transfer(std::move(self));
}

void RLit::pass_inline(PassInline &p, std::unique_ptr<Term> self) {
  meta = make_meta(1, 0);
  size_t me = p.stream.scope().end();
  auto ins = p.common.pool.insert(std::make_pair(value, me));
  if (ins.second) {
    // First ever use of this constant
    p.stream.transfer(std::move(self));
  } else {
    // Can share same object in heap
    value = ins.first->first;
    // Check if this literal is already in scope
    size_t prior = ins.first->second;
    if (prior < p.stream.scope().end() && p.stream[prior]->id() == typeid(RLit)) {
      RLit *lit = static_cast<RLit*>(p.stream[prior]);
      if (lit->value == value) {
        p.stream.discard(prior);
      } else {
        ins.first->second = me;
        p.stream.transfer(std::move(self));
      }
    } else {
      ins.first->second = me;
      p.stream.transfer(std::move(self));
    }
  }
}

static void rapp_inline(PassInline &p, std::unique_ptr<RApp> self) {
  std::vector<size_t> &args = self->args;
  size_t fnargs = meta_args(p.stream[args[0]]->meta);
  if (fnargs == args.size()-1) {
    std::vector<size_t> fargs;
    size_t fnid;
    Term *term = self.get();
    bool singleton = true;
    do {
      RApp *app = static_cast<RApp*>(term);
      for (size_t i = app->args.size()-1; i > 0; --i) {
        size_t argid = app->args[i];
        fargs.push_back(argid);
        if (!singleton) p.stream[argid]->set(SSA_SINGLETON, false);
      }
      fnid = app->args[0];
      term = p.stream[fnid];
      if (!term->get(SSA_SINGLETON)) singleton = false;
    } while (term->id() == typeid(RApp));
    if (term->label == "_ guard") singleton = true;
    assert (!term->get(SSA_MOVED));
    if (!term->get(SSA_RECURSIVE) && (singleton || meta_size(term->meta) < p.common.threshold)) {
      std::unique_ptr<RFun> copy;
      RFun *fun;
      if (singleton) {
        // We will move the original; preventing exponwntial growth
        fun = static_cast<RFun*>(term);
      } else {
        // Temporarily mark this term recursive in case it gets closed by a Y-combinator
        term->set(SSA_RECURSIVE, true);
        copy = static_unique_pointer_cast<RFun>(term->clone(p.stream.scope(), fnid));
        fun = copy.get();
      }
      PassInline q(p.common, fnid); // refs up to fun are unmodified
      q.stream.discard(); // discard name of inlined fn
      size_t n1 = fargs.size()-1;
      for (size_t i = 0; i <= n1; ++i)
        q.stream.discard(fargs[n1-i], fun->terms[i]->get(SSA_SINGLETON)); // bind arguments to inline function
      for (size_t i = fargs.size(); i < fun->terms.size(); ++i) {
        std::unique_ptr<Term> &x = fun->terms[i];
        x->pass_inline(q, std::move(x));
      }
      fun->update(q.stream.map());
      // Keep the result label, if possible
      if (!self->label.empty()) q.stream[fun->output]->label = std::move(self->label);
      p.stream.discard(fun->output, self->get(SSA_SINGLETON)); // replace App with function output
      if (singleton) {
        fun->output = 0;
        fun->terms.resize(fargs.size());
        fun->meta = make_meta(0, fargs.size());
        fun->set(SSA_MOVED, true);
      } else {
        term->set(SSA_RECURSIVE, false);
      }
    } else {
      // Combine App() but don't inline
      args.clear();
      args.emplace_back(fnid);
      if (!singleton) p.stream[fnid]->set(SSA_SINGLETON, false);
      for (size_t i = fargs.size(); i > 0; --i)
        args.emplace_back(fargs[i-1]);
      self->meta = make_meta(1, 0);
      p.stream.transfer(std::move(self));
    }
  } else {
    if (fnargs == 0) {
      // unknown function applied; do not optimize App
      self->meta = make_meta(1, 0);
      p.stream.transfer(std::move(self));
    } else {
      self->meta = make_meta(1, fnargs+1-args.size());
      p.stream.transfer(std::move(self));
    }
  }
}

void RApp::pass_inline(PassInline &p, std::unique_ptr<Term> self) {
  update(p.stream.map());
  rapp_inline(p, static_unique_pointer_cast<RApp>(std::move(self)));
}

void RPrim::pass_inline(PassInline &p, std::unique_ptr<Term> self) {
  meta = make_meta(1, 0);
  update(p.stream.map());
  p.stream.transfer(std::move(self));
}

void RGet::pass_inline(PassInline &p, std::unique_ptr<Term> self) {
  meta = make_meta(1, 0);
  update(p.stream.map());
  Term *input = p.stream[args[0]];
  if (input->id() == typeid(RCon)) {
    RCon *con = static_cast<RCon*>(input);
    p.stream.discard(con->args[index]);
  } else {
    p.stream.transfer(std::move(self));
  }
}

void RDes::pass_inline(PassInline &p, std::unique_ptr<Term> self) {
  meta = make_meta(1, 0);
  update(p.stream.map());
  bool same = true;
  for (unsigned i = 1; i < args.size()-1; ++i)
    same &= args[0] == args[i];
  if (same) {
    std::unique_ptr<RApp> app(new RApp(args[0], args.back(), label.c_str()));
    rapp_inline(p, std::move(app));
  } else {
    Term *input = p.stream[args.back()];
    if (input->id() == typeid(RCon)) {
      RCon *con = static_cast<RCon*>(input);
      std::unique_ptr<RApp> app(
        new RApp(args[con->kind->index], args.back(), label.c_str()));
      rapp_inline(p, std::move(app));
    } else {
      if (!input->get(SSA_ORDERED) && input->get(SSA_FRCON) && input->get(SSA_SINGLETON)) {
        RDes *des = static_cast<RDes*>(input);
        for (unsigned i = 0; i < args.size()-1; ++i)
          p.stream[args[i]]->set(SSA_SINGLETON, false);
        for (unsigned i = 0; i < des->args.size()-1; ++i)
          p.stream[des->args[i]]->set(SSA_SINGLETON, false);
        // create new functions which composes the two RDes
        std::vector<size_t> compose;
        for (unsigned i = 0; i < des->args.size()-1; ++i) {
          size_t fnid = p.stream.scope().end();
          compose.push_back(fnid);
          RFun *prior = static_cast<RFun*>(p.stream[des->args[i]]);
          RFun *f = new RFun(prior->location, prior->label.c_str(), 0, fnid+3);
          std::vector<size_t> cargs(args);
          cargs.back() = fnid+2;
          f->terms.emplace_back(new RArg());
          f->terms.emplace_back(new RApp(des->args[i], fnid+1));
          f->terms.emplace_back(new RDes(std::move(cargs)));
          PassInline q(p.common, fnid); // refs up to fun are unmodified
          f->pass_inline(q, std::unique_ptr<Term>(f));
        }
        args = std::move(compose);
        args.push_back(des->args.back());
      }
      p.stream.transfer(std::move(self));

      bool known = true;
      for (unsigned i = 0; i < args.size()-1; ++i)
        known &= p.stream[args[i]]->get(SSA_FRCON);
      set(SSA_FRCON, known);
    }
  }
}

void RCon::pass_inline(PassInline &p, std::unique_ptr<Term> self) {
  meta = make_meta(1, 0);
  update(p.stream.map());
  set(SSA_FRCON, true);
  p.stream.transfer(std::move(self));
}

void RFun::pass_inline(PassInline &p, std::unique_ptr<Term> self) {
  p.stream.transfer(std::move(self));
  CheckPoint cp = p.stream.begin();

  size_t args = 0, ate = 0;
  while (true) {
    for (; args < terms.size(); ++args) {
      std::unique_ptr<Term> &x = terms[args];
      if (x->id() != typeid(RArg)) break;
      x->pass_inline(p, std::move(x));
    }
    // consider also combining out-of-band function return ref?
    if (args != terms.size()-1) break;
    if (output-ate != cp.source+args) break;
    if (terms[args]->id() != typeid(RFun)) break;
    if (terms[args]->get(SSA_RECURSIVE)) break;
    // steal all the grandchildren
    std::unique_ptr<RFun> child(static_cast<RFun*>(terms[args].release()));
    p.stream.discard();
    terms.pop_back();
    ++ate;
    for (auto &x : child->terms)
      terms.emplace_back(std::move(x));
    output = child->output;
    label = child->label;
  }

  meta = make_meta(0, args); // size does not count in recursive use
  for (size_t i = args; i < terms.size(); ++i) {
    std::unique_ptr<Term> &x = terms[i];
    x->pass_inline(p, std::move(x));
  }

  update(p.stream.map());
  // Detect if function returns a Constructor
  set(SSA_FRCON, p.stream[output]->get(SSA_FRCON));
  terms = p.stream.end(cp);

  size_t size = 1;
  for (auto &x : terms) size += meta_size(x->meta);
  meta = make_meta(size, args);
}

std::unique_ptr<Term> Term::pass_inline(std::unique_ptr<Term> term, size_t threshold, Runtime &runtime) {
  PassInlineCommon common(&runtime, threshold);
  PassInline pass(common);
  // Top-level functions are not candidates for movement (inlining is still ok)
  // If we allowed it, function hashes become sensitive to non-local source changes.
  for (auto &t : static_cast<RFun*>(term.get())->terms)
    t->set(SSA_SINGLETON, false);
  term->pass_inline(pass, std::move(term));
  return common.scope.finish();
}
