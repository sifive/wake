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

struct PassPurity {
  TermStream stream;
  bool first;
  bool fixed;
};

void RArg::pass_purity(PassPurity &p) {
}

void RLit::pass_purity(PassPurity &p) {
}

void RApp::pass_purity(PassPurity &p) {
}

void RPrim::pass_purity(PassPurity &p) {
}

void RGet::pass_purity(PassPurity &p) {
}

void RDes::pass_purity(PassPurity &p) {
}

void RCon::pass_purity(PassPurity &p) {
}

void RFun::pass_purity(PassPurity &p) {
}


/*

static const std::type_info &uniqueid(const std::unique_ptr<Term> &t) {
  const Term *x = t.get();
  return typeid(*x);
}

// flatten -> compress lambdas
// inline Apps as up to the length of the lambdas ... or single-use
// purity -> usage -> deadcode+flatten

static void purity(TermStream &stream, std::unique_ptr<Term> term) {
  if (uniqueid(term) == typeid(RFun)) {
    RFun *fun = static_cast<RFun*>(term.get());
    stream.transfer(std::move(term));
    CheckPoint cp = stream.begin();
    for (auto &x : fun->terms) rewrite(stream, std::move(x));
    fun->update(stream.map());
    fun->terms = stream.end(cp);
  } else {
    term->update(stream.map());
    stream.transfer(std::move(term));
  }
}

std::unique_ptr<Term> consume(std::unique_ptr<Term> rf) {
  TargetScope scope;
  TermStream stream(scope);
  rewrite(stream, std::move(rf));
  return scope.finish();
}
*/
