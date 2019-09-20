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
    if (x >= format.id) os << " !!!";
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
  output = map[output];
}

void RFun::format(std::ostream &os, TermFormat &format) const {
  os << "FunRet:" << output;
  if (output > format.id + terms.size()) os << " !!!";
  os << "\n";
  format.depth += 2;
  for (auto &x : terms) {
    os << pad(format.depth+2) << ++format.id;
    if (!x->label.empty()) os << " (" << x->label << ")";
    os << " = ";
    x->format(os, format);
  }
  format.id -= terms.size();
  format.depth -= 2;
}

std::vector<std::unique_ptr<Term> > TermRewriter::end(CheckPoint p) {
  std::vector<std::unique_ptr<Term> > out;
  map.resize(p.map);
  out.reserve(terms.size() - p.terms);
  for (size_t i = p.terms; i < terms.size(); ++i)
    out.emplace_back(std::move(terms[i]));
  terms.resize(p.terms);
  return out;
}
