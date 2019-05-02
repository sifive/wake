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

#include "expr.h"
#include "value.h"
#include "hash.h"
#include "thunk.h"
#include "datatype.h"
#include <cassert>
#include <sstream>

Expr::~Expr() { }
const TypeDescriptor Prim      ::type("Prim");
const TypeDescriptor App       ::type("App");
const TypeDescriptor Lambda    ::type("Lambda");
const TypeDescriptor VarRef    ::type("VarRef");
const TypeDescriptor Literal   ::type("Literal");
const TypeDescriptor DefBinding::type("DefBinding");
const TypeDescriptor Construct ::type("Construct");
const TypeDescriptor Destruct  ::type("Destruct");
// these are removed by bind
const TypeDescriptor Subscribe ::type("Subscribe");
const TypeDescriptor Match     ::type("Match");
const TypeDescriptor Memoize   ::type("Memoize");
const TypeDescriptor DefMap    ::type("DefMap");
const TypeDescriptor Top       ::type("Top");

Literal::Literal(const Location &location_, std::shared_ptr<Value> &&value_)
 : Expr(&type, location_), value(std::move(value_)) { }

Literal::Literal(const Location &location_, const char *value_)
 : Expr(&type, location_), value(std::make_shared<String>(value_)) { }

static std::string pad(int depth) {
  return std::string(depth, ' ');
}

std::string Expr::to_str() const {
  std::stringstream str;
  str << this;
  return str.str();
}

void VarRef::format(std::ostream &os, int depth) const {
  os << pad(depth) << "VarRef(" << name;
  if (offset != -1) os << "," << depth << "," << offset;
  os << "): " << typeVar << " @ " << location.file() << std::endl;
}

Hash VarRef::hash() {
  uint64_t payload[4];
  payload[0] = type.hashcode.data[0];
  payload[0] = type.hashcode.data[1];
  payload[2] = depth;
  payload[3] = offset;
  return hashcode = Hash(&payload[0], 32);
}

void Subscribe::format(std::ostream &os, int depth) const {
  os << pad(depth) << "Subscribe(" << name << ") @ " << location.file() << std::endl;
}

Hash Subscribe::hash() {
  assert(0 /* unreachable */);
}

void Memoize::format(std::ostream &os, int depth) const {
  os << pad(depth) << "Memoize: " << typeVar << " @ " << location.file() << std::endl;
  body->format(os, depth+2);
}

Hash Memoize::hash() {
  return hashcode = body->hash() + type.hashcode;
}

void Match::format(std::ostream &os, int depth) const {
  os << pad(depth) << "Match: " << typeVar << " @ " << location.file() << std::endl;
  for (auto &a: args)
    a->format(os, depth+2);
  for (auto &p: patterns) {
    os << pad(depth+2) << p.pattern << " = " << std::endl;
    p.expr->format(os, depth+4);
    if (p.guard) {
      os << pad(depth+2) << "if" << std::endl;
      p.guard->format(os, depth+4);
    }
  }
}

Hash Match::hash() {
  std::vector<uint64_t> codes;
  type.hashcode.push(codes);
  for (auto &a : args)
    a->hash().push(codes);
  for (auto &p : patterns) {
    std::stringstream ss;
    ss << p.pattern;
    Hash(ss.str()).push(codes);
    p.expr->hash().push(codes);
    if (p.guard) {
      p.guard->hash().push(codes);
    } else {
      codes.push_back(0);
    }
  }
  return hashcode = Hash(codes);
}

void App::format(std::ostream &os, int depth) const {
  os << pad(depth) << "App: " << typeVar << " @ " << location.file() << std::endl;
  fn->format(os, depth+2);
  val->format(os, depth+2);
}

Hash App::hash() {
  std::vector<uint64_t> codes;
  type.hashcode.push(codes);
  fn->hash().push(codes);
  val->hash().push(codes);
  return hashcode = Hash(codes);
}

void Lambda::format(std::ostream &os, int depth) const {
  os << pad(depth) << "Lambda(" << name << "): " << typeVar << " @ " << location.file() << std::endl;
  body->format(os, depth+2);
}

Hash Lambda::hash() {
  return hashcode = body->hash() + type.hashcode;
}

void DefMap::format(std::ostream &os, int depth) const {
  os << pad(depth) << "DefMap @ " << location.file() << std::endl;
  for (auto &i : map) {
    os << pad(depth+2) << i.first << " =" << std::endl;
    i.second->format(os, depth+4);
  }
  for (auto &i : publish) {
    os << pad(depth+2) << "publish " << i.first << " =" << std::endl;
    i.second->format(os, depth+4);
  }
  body->format(os, depth+2);
}

Hash DefMap::hash() {
  assert(0 /* unreachable */);
}

void Literal::format(std::ostream &os, int depth) const {
  os << pad(depth) << "Literal: " << typeVar << " @ " << location.file() << " = " << value.get() << std::endl;
}

Hash Literal::hash() {
  return hashcode = value->hash() + type.hashcode;
}

void Prim::format(std::ostream &os, int depth) const {
 os << pad(depth) << "Prim(" << args << "," << name << "): " << typeVar << " @ " << location.file() << std::endl;
}

Hash Prim::hash() {
  return hashcode = Hash(name) + type.hashcode;
}

void Top::format(std::ostream &os, int depth) const {
  os << pad(depth) << "Top; globals =";
  for (auto &i : globals) os << " " << i.first;
  os << std::endl;
  for (auto &i : defmaps) i->format(os, depth+2);
  body->format(os, depth+2);
}

Hash Top::hash() {
  assert(0 /* unreachable */);
}

void DefBinding::format(std::ostream &os, int depth) const {
  os << pad(depth) << "DefBinding: " << typeVar << " @ " << location.file() << std::endl;

  // invert name=>index map
  std::vector<const char*> names(order.size());
  for (auto &i : order) names[i.second] = i.first.c_str();

  for (int i = 0; i < (int)val.size(); ++i) {
    os << pad(depth+2) << "val " << names[i] << " = " << std::endl;
    val[i]->format(os, depth+4);
  }
  for (int i = 0; i < (int)fun.size(); ++i) {
    os << pad(depth+2) << "fun " << names[i+val.size()] << " (" << scc[i] << ") = " << std::endl;
    fun[i]->format(os, depth+4);
  }
  body->format(os, depth+2);
}

Hash DefBinding::hash() {
  std::vector<uint64_t> codes;
  type.hashcode.push(codes);
  for (auto &i : val)
    i->hash().push(codes);
  for (auto &i : fun)
    i->hash().push(codes);
  body->hash().push(codes);
  return hashcode = Hash(codes);
}

void Construct::format(std::ostream &os, int depth) const {
  os << pad(depth) << "Construct(" << cons->ast.name << "): " << typeVar << " @ " << location.file() << std::endl;
}

Hash Construct::hash() {
  return hashcode = Hash(cons->ast.name) + type.hashcode;
}

void Destruct::format(std::ostream &os, int depth) const {
  os << pad(depth) << "Destruct(" << sum.name << "): " << typeVar << " @ " << location.file() << std::endl;
}

Hash Destruct::hash() {
  return hashcode = Hash(sum.name) + type.hashcode;
}

std::ostream & operator << (std::ostream &os, const Expr *expr) {
  expr->format(os, 0);
  return os;
}
