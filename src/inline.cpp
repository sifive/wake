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
#include <unordered_map>

static bool operator == (const std::shared_ptr<RootPointer<Value> > &x, const std::shared_ptr<RootPointer<Value> > &y) {
  return **x == **y;
}

namespace std {
  template <> struct hash<std::shared_ptr<RootPointer<Value> > > {
    size_t operator () (const std::shared_ptr<RootPointer<Value> > &x) const {
      return (*x)->hashid();
    }
  };
}

typedef std::unordered_map<std::shared_ptr<RootPointer<Value> >, size_t> ConstantPool;

// In this pass, meta = the size of the AST & number of unapplied args
static size_t make_meta(size_t size, size_t args) { return (size << 8) | args; }
static size_t meta_size(size_t meta) { return meta >> 8; }
static size_t meta_args(size_t meta) { return meta & 255; }

struct PassInline {
  TermStream stream;
  ConstantPool pool;
  PassInline(TargetScope &scope, size_t start = 0) : stream(scope, start) { }
};

void RArg::pass_inline(PassInline &p, std::unique_ptr<Term> self) {
  meta = make_meta(1, 0); // we don't know the number unapplied args, but 0 prevents inlining anyway
  p.stream.transfer(std::move(self));
}

void RLit::pass_inline(PassInline &p, std::unique_ptr<Term> self) {
  meta = make_meta(1, 0);
  size_t me = p.stream.scope().end();
  auto ins = p.pool.insert(std::make_pair(value, me));
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
    Term *term = self.get();
    size_t fnid;
    do {
      RApp *app = static_cast<RApp*>(term);
      for (size_t i = app->args.size()-1; i > 0; --i)
        fargs.push_back(app->args[i]);
      fnid = app->args[0];
      term = p.stream[fnid];
    } while (term->id() == typeid(RApp));

    if (meta_size(term->meta) < 100 && !term->get(SSA_RECURSIVE)) {
      auto fun = static_unique_pointer_cast<RFun>(term->clone());
      PassInline q(p.stream.scope(), fnid); // refs up to fun are unmodified
      q.pool = std::move(p.pool);
      q.stream.discard(); // discard name of inlined fn
      for (size_t i = fargs.size(); i > 0; --i)
        q.stream.discard(fargs[i-1]); // bind arguments to inline function
      for (size_t i = fargs.size(); i < fun->terms.size(); ++i) {
        std::unique_ptr<Term> &x = fun->terms[i];
        x->pass_inline(q, std::move(x));
      }
      fun->update(q.stream.map());
      p.stream.discard(fun->output); // replace App with function output
      p.pool = std::move(q.pool);
    } else {
      // Combine App() but don't inline
      args.clear();
      args.emplace_back(fnid);
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
  Term *input = p.stream[args.back()];
  if (input->id() == typeid(RCon)) {
    RCon *con = static_cast<RCon*>(input);
    std::unique_ptr<RApp> app(
      new RApp(args[con->kind->index], args.back(), label.c_str()));
    rapp_inline(p, std::move(app));
  } else {
    p.stream.transfer(std::move(self));
  }
}

void RCon::pass_inline(PassInline &p, std::unique_ptr<Term> self) {
  meta = make_meta(1, 0);
  update(p.stream.map());
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
  terms = p.stream.end(cp);

  size_t size = 1;
  for (auto &x : terms) size += meta_size(x->meta);
  meta = make_meta(size, args);
}

std::unique_ptr<Term> Term::pass_inline(std::unique_ptr<Term> term) {
  TargetScope scope;
  PassInline pass(scope);
  term->pass_inline(pass, std::move(term));
  return scope.finish();
}
