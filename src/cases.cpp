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

struct PassCases {
  ScopeAnalysis scope;
  PassCases() { }
};

static uintptr_t tometa(Constructor *con) {
  return reinterpret_cast<uintptr_t>(static_cast<void*>(con));
}

void RArg::pass_cases(PassCases &p) {
  // These get filled in by RDes
  meta = 0;
}

void RLit::pass_cases(PassCases &p) {
  meta = 0;
}

void RApp::pass_cases(PassCases &p) {
  meta = 0;
}

void RPrim::pass_cases(PassCases &p) {
  meta = 0;
}

void RGet::pass_cases(PassCases &p) {
  meta = 0;
}

void RDes::pass_cases(PassCases &p) {
  for (size_t i = 0, num = args.size()-1; i < num; ++i) {
    RFun *fun = static_cast<RFun*>(p.scope[args[i]]);
    Term *arg = fun->terms[0].get();
    if (arg->meta) {
      arg->meta = ~static_cast<uintptr_t>(0);
    } else {
      arg->meta = tometa(&sum->members[i]);
    }
  }
  meta = 0;
}

void RCon::pass_cases(PassCases &p) {
  meta = tometa(kind.get());
}

void RFun::pass_cases(PassCases &p) {
  for (auto &x : terms) {
    p.scope.push(x.get());
    x->pass_cases(p);
  }
  meta = 0;
  p.scope.pop(terms.size());
}

std::unique_ptr<Term> Term::pass_cases(std::unique_ptr<Term> term) {
  PassCases pass;
  pass.scope.push(term.get());
  term->pass_cases(pass);
  return term;
}
