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

#include "optimizer/ssa.h"

struct PassUsage {
  ScopeAnalysis scope;
};

static void redux_usage(PassUsage &p, Redux *r) {
  for (auto x : r->args) ++p.scope[x]->meta;
}

void RArg::pass_usage(PassUsage &p) {
  // uses nothing
}

void RLit::pass_usage(PassUsage &p) {
  // uses nothing
}

void RApp::pass_usage(PassUsage &p) {
  redux_usage(p, this);
}

void RPrim::pass_usage(PassUsage &p) {
  redux_usage(p, this);
}

void RGet::pass_usage(PassUsage &p) {
  redux_usage(p, this);
}

void RDes::pass_usage(PassUsage &p) {
  redux_usage(p, this);
}

void RCon::pass_usage(PassUsage &p) {
  redux_usage(p, this);
}

void RFun::pass_usage(PassUsage &p) {
  for (auto &x : terms) {
    x->meta = 0;
    p.scope.push(x.get());
  }
  ++p.scope[output]->meta;
  size_t last = p.scope.last();
  for (unsigned i = 0; i < terms.size(); ++i) {
    Term *t = p.scope[last-i];
    bool used = t->meta > 0 || t->get(SSA_EFFECT);
    t->set(SSA_USED, used);
    t->set(SSA_SINGLETON, t->meta == 1);
    if (used) t->pass_usage(p);
    p.scope.pop();
  }
}

std::unique_ptr<Term> Term::pass_usage(std::unique_ptr<Term> term) {
  PassUsage pass;
  pass.scope.push(term.get());
  term->pass_usage(pass);
  term->set(SSA_USED, true);
  term->set(SSA_SINGLETON, true);
  return term;
}
