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

#include "runtime/runtime.h"
#include "ssa.h"
#include "util/hash.h"

// typeid hash_code is not stable between invocations
#define TYPE_RARG 0
#define TYPE_RLIT 1
#define TYPE_RAPP 2
#define TYPE_RPRIM 3
#define TYPE_RGET 4
#define TYPE_RDES 5
#define TYPE_RCON 6
#define TYPE_RFUN 7

struct PassScope {
  Runtime &runtime;
  PassScope *next;
  size_t start;
  size_t index;
  std::vector<size_t> escapes;
  std::vector<uint64_t> codes;
  PassScope(Runtime &runtime_, PassScope *next_, size_t start_)
      : runtime(runtime_), next(next_), start(start_), index(start_) {}
};

static size_t scope_arg(PassScope &p, size_t input) {
  if (input < p.start) {
    size_t escape;
    for (escape = 0; escape < p.escapes.size(); ++escape)
      if (p.escapes[escape] == input) break;
    if (escape == p.escapes.size()) p.escapes.push_back(input);
    size_t depth = 0;
    PassScope *top = &p;
    do {
      ++depth;
      top = top->next;
    } while (input < top->start);
    size_t offset = input - top->start;
    p.codes.push_back(make_arg(1, escape));
    return make_arg(depth, offset);
  } else {
    size_t out = make_arg(0, input - p.start);
    p.codes.push_back(out);
    return out;
  }
}

void RArg::pass_scope(PassScope &p) { p.codes.push_back(TYPE_RARG); }

void RLit::pass_scope(PassScope &p) {
  p.codes.push_back(TYPE_RLIT);
  (*value)->deep_hash(p.runtime.heap).push(p.codes);
}

static void scope_redux(PassScope &p, Redux *redux, size_t type) {
  p.codes.push_back(type);
  p.codes.push_back(redux->args.size());
  for (auto &x : redux->args) x = scope_arg(p, x);
}

void RApp::pass_scope(PassScope &p) { scope_redux(p, this, TYPE_RAPP); }

void RPrim::pass_scope(PassScope &p) {
  scope_redux(p, this, TYPE_RPRIM);
  Hash(name).push(p.codes);
}

void RGet::pass_scope(PassScope &p) {
  scope_redux(p, this, TYPE_RGET);
  p.codes.push_back(index);
}

void RDes::pass_scope(PassScope &p) { scope_redux(p, this, TYPE_RDES); }

void RCon::pass_scope(PassScope &p) {
  scope_redux(p, this, TYPE_RCON);
  Hash(kind->ast.name).push(p.codes);
}

void RFun::pass_scope(PassScope &p) {
  PassScope frame(p.runtime, &p, p.index + 1);

  output = scope_arg(frame, output);
  for (auto &term : terms) {
    term->pass_scope(frame);
    ++frame.index;
  }

  hash = Hash(frame.codes);
  escapes = std::move(frame.escapes);

  p.codes.push_back(TYPE_RFUN);
  hash.push(p.codes);
  for (auto &x : escapes) x = scope_arg(p, x);
}

std::unique_ptr<Term> Term::scope(std::unique_ptr<Term> term, Runtime &runtime) {
  PassScope pass(runtime, nullptr, 0);
  term->pass_scope(pass);
  return term;
}
