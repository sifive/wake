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

struct PassInline {
  TermStream stream;
  PassInline(TargetScope &scope) : stream(scope) { }
};

void RArg::pass_inline(PassInline &p, std::unique_ptr<Term> self) {
  p.stream.transfer(std::move(self));
}

void RLit::pass_inline(PassInline &p, std::unique_ptr<Term> self) {
  p.stream.transfer(std::move(self));
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
