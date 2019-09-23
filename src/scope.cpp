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

struct PassScope {
  PassScope *next;
  size_t start;
  size_t index;
  PassScope(PassScope *next_, size_t start_) : next(next_), start(start_), index(start_) { }
};

static size_t scope_arg(PassScope *top, size_t input) {
  size_t depth = 0;
  while (input < top->start) {
    ++depth;
    top = top->next;
  }
  return make_arg(depth, input - top->start);
}

void Leaf::pass_scope(PassScope &p) {
}

void Redux::pass_scope(PassScope &p) {
  for (auto &x : args) x = scope_arg(&p, x);
}

void RFun::pass_scope(PassScope &p) {
  PassScope frame(&p, p.index + 1);

  output = scope_arg(&frame, output);
  for (auto &term : terms) {
    term->pass_scope(frame);
    ++frame.index;
  }
}

std::unique_ptr<Term> Term::scope(std::unique_ptr<Term> term) {
  PassScope pass(nullptr, 0);
  term->pass_scope(pass);
  return term;
}
