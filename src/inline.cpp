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

struct PassInline {
  TermStream stream;
  ConstantPool pool;
  PassInline(TargetScope &scope) : stream(scope) { }
};

void RArg::pass_inline(PassInline &p, std::unique_ptr<Term> self) {
  p.stream.transfer(std::move(self));
}

void RLit::pass_inline(PassInline &p, std::unique_ptr<Term> self) {
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

void RApp::pass_inline(PassInline &p, std::unique_ptr<Term> self) {
  update(p.stream.map());
  p.stream.transfer(std::move(self));
}

void RPrim::pass_inline(PassInline &p, std::unique_ptr<Term> self) {
  update(p.stream.map());
  p.stream.transfer(std::move(self));
}

void RGet::pass_inline(PassInline &p, std::unique_ptr<Term> self) {
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
  update(p.stream.map());
  Term *input = p.stream[args.back()];
  if (input->id() == typeid(RCon)) {
    RCon *con = static_cast<RCon*>(input);
    p.stream.discard(args[con->kind]);
  } else {
    p.stream.transfer(std::move(self));
  }
}

void RCon::pass_inline(PassInline &p, std::unique_ptr<Term> self) {
  update(p.stream.map());
  p.stream.transfer(std::move(self));
}

void RFun::pass_inline(PassInline &p, std::unique_ptr<Term> self) {
  p.stream.transfer(std::move(self));
  CheckPoint cp = p.stream.begin();
  for (auto &x : terms) x->pass_inline(p, std::move(x));
  update(p.stream.map());
  terms = p.stream.end(cp);
}

std::unique_ptr<Term> Term::pass_inline(std::unique_ptr<Term> term) {
  TargetScope scope;
  PassInline pass(scope);
  term->pass_inline(pass, std::move(term));
  return scope.finish();
}
