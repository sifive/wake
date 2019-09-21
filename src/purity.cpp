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
#include "prim.h"

struct PassPurity {
  TermStream stream;
  bool first;
  bool fixed;
  PassPurity(TargetScope &scope) : stream(scope), first(true) { }
};

static uintptr_t filter_lowest(uintptr_t x) {
  return (x&1) | (~static_cast<uintptr_t>(0) << 1);
}

void RArg::pass_purity(PassPurity &p) {
  // An argument has no effects unless it is applied
  meta = 1;
}

void RLit::pass_purity(PassPurity &p) {
  // Literals have no effects
  meta = 1;
}

void RApp::pass_purity(PassPurity &p) {
  // The unapplied function itself does not cause an effect
  uintptr_t acc = p.stream[args[0]]->meta | 1;
  // Each application accumulates a chance to zero the lowest bit
  for (unsigned i = 1; i < args.size(); ++i)
    acc = (acc >> 1) & filter_lowest(acc);
  meta = acc;
}

void RPrim::pass_purity(PassPurity &p) {
  // Pure only if it is safe to remove this primitive
  meta = (pflags & PRIM_REMOVE) != 0;
  // Special-case for tget (purity depends on purity of fn arg)
  if ((pflags & PRIM_TGET))
    meta = p.stream[args[3]]->meta >> 1;
}

void RGet::pass_purity(PassPurity &p) {
  // Gets destructure only => pure
  meta = 1;
}

void RDes::pass_purity(PassPurity &p) {
 // Result is only pure when all applied handlers are pure
 uintptr_t acc = ~static_cast<uintptr_t>(0);
 for (unsigned i = 0, num = args.size()-1; i < num; ++i)
   acc &= p.stream[args[i]]->meta;
 meta = acc >> 1;
}

void RCon::pass_purity(PassPurity &p) {
  meta = 1;
}

void RFun::pass_purity(PassPurity &p) {
  if (p.first)
    meta = ~static_cast<uintptr_t>(0); // visible in recursive use
  uintptr_t save = meta;
  uintptr_t acc = 1;
  int args = 0;
  CheckPoint cp = p.stream.begin();
  for (auto &x : terms) {
    Term *t = x.get();
    p.stream.transfer(std::move(x));
    t->pass_purity(p);
    args += t->id() == typeid(RArg);
    acc &= t->meta;
  }
  // Effects of body
  acc = filter_lowest(acc) & p.stream[output]->meta;
  // Purity from lambda
  meta = (acc << args) | ((1 << args) - 1);
  if (save != meta) p.fixed = false;
  terms = p.stream.end(cp);
}

std::unique_ptr<Term> Term::pass_purity(std::unique_ptr<Term> term) {
  TargetScope scope;
  PassPurity pass(scope);
  do {
    pass.fixed = true;
    pass.stream.transfer(std::move(term));
    scope[0]->pass_purity(pass);
    term = scope.finish();
    pass.first = false;
  } while (!pass.fixed);
  return term;
}
