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
#include <algorithm>
#include <unordered_map>

struct PassDecon {
  TermStream stream;

  PassDecon(TargetScope &scope) : stream(scope) { }
};

static uintptr_t tometa(Constructor *con) {
  return reinterpret_cast<uintptr_t>(static_cast<void*>(con));
}

void RArg::pass_decon(PassDecon &p, std::unique_ptr<Term> self) {
  p.stream.transfer(std::move(self));
}

void RLit::pass_decon(PassDecon &p, std::unique_ptr<Term> self) {
  p.stream.transfer(std::move(self));
}

void RApp::pass_decon(PassDecon &p, std::unique_ptr<Term> self) {
  update(p.stream.map());
  p.stream.transfer(std::move(self));
}

void RPrim::pass_decon(PassDecon &p, std::unique_ptr<Term> self) {
  update(p.stream.map());
  p.stream.transfer(std::move(self));
}

void RGet::pass_decon(PassDecon &p, std::unique_ptr<Term> self) {
  update(p.stream.map());
  p.stream.transfer(std::move(self));
}

void RDes::pass_decon(PassDecon &p, std::unique_ptr<Term> self) {
  update(p.stream.map());
  p.stream.transfer(std::move(self));
}

void RCon::pass_decon(PassDecon &p, std::unique_ptr<Term> self) {
  update(p.stream.map());
  if (args.empty()) {
    p.stream.transfer(std::move(self));
  } else if (p.stream[args[0]]->id() != typeid(RGet)) {
    p.stream.transfer(std::move(self));
  } else {
    size_t candidate = static_cast<RGet*>(p.stream[args[0]])->args[0];
    bool optimize = p.stream[candidate]->meta == tometa(kind.get());
    for (size_t i = 0; optimize && i < args.size(); ++i) {
      Term *term = p.stream[args[i]];
      if (term->id() != typeid(RGet)) {
        optimize = false;
      } else {
        RGet *get = static_cast<RGet*>(term);
        if (get->index != i || get->args[0] != candidate)
          optimize = false;
      }
    }
    if (optimize) {
      p.stream.discard(candidate);
    } else {
      p.stream.transfer(std::move(self));
    }
  }
}

void RFun::pass_decon(PassDecon &p, std::unique_ptr<Term> self) {
  p.stream.transfer(std::move(self));
  CheckPoint body = p.stream.begin();

  for (auto &x : terms)
    x->pass_decon(p, std::move(x));

  update(p.stream.map());

  // Detect returns of a RCon equal to our first argument
  // This is helpful because it makes it possible collapse cases in an RDes
  Term *out = p.stream[output];
  if (out->id() == typeid(RCon)) {
    RCon *con = static_cast<RCon*>(out);
    if (con->args.empty() && p.stream[body.target]->meta == tometa(con->kind.get()))
      output = body.target;
  }

  terms = p.stream.end(body);
}

std::unique_ptr<Term> Term::pass_decon(std::unique_ptr<Term> term) {
  TargetScope scope;
  PassDecon pass(scope);
  term->pass_decon(pass, std::move(term));
  return scope.finish();
}
