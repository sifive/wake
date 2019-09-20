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
#include "value.h"
#include <assert.h>

Term::~Term() { }

void Leaf::update(const std::vector<size_t> &map) {
}

void Redux::update(const std::vector<size_t> &map) {
  for (auto &x : args) x = map[x];
}

void Redux::format_args(std::ostream &os, TermFormat &format) const {
  bool first = true;
  for (auto x : args) {
    if (!first) os << " ";
    os << x;
    first = false;
  }
}

void RArg::format(std::ostream &os, TermFormat &format) const {
  os << "<arg>\n";
}

void RLit::format(std::ostream &os, TermFormat &format) const {
  HeapObject::format(os, value->get());
  os << "\n";
}

void RApp::format(std::ostream &os, TermFormat &format) const {
  os << "App(";
  format_args(os, format);
  os << ")\n";
}

void RPrim::format(std::ostream &os, TermFormat &format) const {
  os << name << "(";
  format_args(os, format);
  os << ")\n";
}

void RGet::format(std::ostream &os, TermFormat &format) const {
  os << "Get:" << index << "(";
  format_args(os, format);
  os << ")\n";
}

void RDes::format(std::ostream &os, TermFormat &format) const {
  os << "Des(";
  format_args(os, format);
  os << ")\n";
}

void RCon::format(std::ostream &os, TermFormat &format) const {
  os << "Con:" << kind << "(";
  format_args(os, format);
  os << ")\n";
}

static std::string pad(int depth) {
  return std::string(depth, ' ');
}

void RFun::update(const std::vector<size_t> &map) {
  assert(0 /* unreachable */);
}

void RFun::format(std::ostream &os, TermFormat &format) const {
  os << "FunRet:" << output << "\n";
  format.depth += 2;
  for (size_t i = 0; i < terms.size(); ++i) {
    const Term *x = terms[i].get();
    os << pad(format.depth+2) << (start+i);
    if (!x->label.empty())
      os << " (" << x->label << ")";
    os << " = ";
    x->format(os, format);
  }
  format.depth -= 2;
}

std::unique_ptr<Term> TermRewriter::enter(Term *base) {
  size_t body = terms.size() + 1;
  stack.emplace_back(body);
  std::unique_ptr<Term> out(new RFun(base->label.c_str(), body));
  out->meta = base->meta;
  return out;
}

size_t TermRewriter::enter_replace(Term *base) {
  return replace(enter(base));
}

size_t TermRewriter::enter_insert(Term *base) {
  return insert(enter(base));
}

size_t TermRewriter::exit(size_t output) {
  size_t body = stack.back();
  size_t fn = body-1;
  stack.pop_back();
  RFun *fun = static_cast<RFun*>(terms[fn].get());
  fun->output = output;
  fun->terms.clear();
  fun->terms.reserve(terms.size() - body);
  for (unsigned i = body; i < terms.size(); ++i)
    fun->terms.emplace_back(std::move(terms[i]));
  map.resize(body);
  terms.resize(body);
  return fn;
}

std::unique_ptr<Term> TermRewriter::finish() {
  assert(stack.size() == 0);
  assert(terms.size() == 1);
  return std::move(terms[0]);
  terms.clear();
  map.clear();
}
