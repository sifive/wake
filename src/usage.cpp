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

struct PassUsage {
  ReverseScope scope;
};

static void redux_usage(PassUsage &p, Redux *r) {
  for (auto x : r->args)
    p.scope[x]->meta = 0;
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
  p.scope.push(terms);
  p.scope[output]->meta = 0;
  for (unsigned i = 0; i < terms.size(); ++i) {
    Term *t = p.scope.peek();
    if (t->id() == typeid(RArg)) t->meta = 0;
    else if (!(t->meta & 1)) t->pass_usage(p);
    p.scope.pop();
  }
}

std::unique_ptr<Term> Term::pass_usage(std::unique_ptr<Term> term) {
  PassUsage pass;
  pass.scope.push(term.get());
  term->pass_usage(pass);
  return term;
}
