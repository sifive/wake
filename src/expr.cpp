#include "expr.h"
#include "value.h"
#include "hash.h"
#include "thunk.h"
#include <iostream>
#include <cassert>
#include <sstream>

Expr::~Expr() { }
const char *Prim::type = "Prim";
const char *App::type = "App";
const char *Lambda::type = "Lambda";
const char *VarRef::type = "VarRef";
const char *Literal::type = "Literal";
const char *DefBinding::type = "DefBinding";
// these are removed by bind
const char *Subscribe::type = "Subscribe";
const char *Memoize::type = "Memoize";
const char *DefMap::type = "DefMap";
const char *Top::type = "Top";

Literal::Literal(const Location &location_, std::shared_ptr<Value> &&value_)
 : Expr(type, location_), value(std::move(value_)) { }

Literal::Literal(const Location &location_, const char *value_)
 : Expr(type, location_), value(std::make_shared<String>(value_)) { }

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
  os << ") @ " << location << std::endl;
}

void VarRef::hash() {
  uint64_t payload[2];
  payload[0] = depth;
  payload[1] = offset;
  HASH(payload, 16, (long)type, hashcode);
}

void Subscribe::format(std::ostream &os, int depth) const {
  os << pad(depth) << "Subscribe(" << name << ") @ " << location << std::endl;
}

void Subscribe::hash() {
  assert(0 /* unreachable */);
}

void Memoize::format(std::ostream &os, int depth) const {
  os << pad(depth) << "Memoize @ " << location << std::endl;
  body->format(os, depth+2);
}

void Memoize::hash() {
  body->hash();
  HASH(&body->hashcode, sizeof(body->hashcode), (long)type, hashcode);
}

void App::format(std::ostream &os, int depth) const {
  os << pad(depth) << "App @ " << location << std::endl;
  fn->format(os, depth+2);
  val->format(os, depth+2);
}

void App::hash() {
  std::vector<uint64_t> codes;
  fn->hash();
  val->hash();
  fn->hashcode.push(codes);
  val->hashcode.push(codes);
  HASH(codes.data(), codes.size()*8, (long)type, hashcode);
}

void Lambda::format(std::ostream &os, int depth) const {
  os << pad(depth) << "Lambda(" << name << ") @ " << location << std::endl;
  body->format(os, depth+2);
}

void Lambda::hash() {
  body->hash();
  HASH(&body->hashcode, sizeof(body->hashcode), (long)type, hashcode);
}

void DefMap::format(std::ostream &os, int depth) const {
  os << pad(depth) << "DefMap @ " << location << std::endl;
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

void DefMap::hash() {
  assert(0 /* unreachable */);
}

void Literal::format(std::ostream &os, int depth) const {
  os << pad(depth) << "Literal @ " << location << " = ";
  value->format(os, depth);
}

void Literal::hash() {
  hashcode = value->hash();
  HASH(&hashcode, sizeof(hashcode), (long)type, hashcode);
}

void Prim::format(std::ostream &os, int depth) const {
 os << pad(depth) << "Prim(" << args << "," << name << ") @ " << location << std::endl;
}

void Prim::hash() {
  HASH(name.data(), name.size(), (long)type, hashcode);
}

void Top::format(std::ostream &os, int depth) const {
  os << pad(depth) << "Top; globals =";
  for (auto &i : globals) os << " " << i.first;
  os << std::endl;
  for (auto &i : defmaps) i.format(os, depth+2);
  body->format(os, depth+2);
}

void Top::hash() {
  assert(0 /* unreachable */);
}

void DefBinding::format(std::ostream &os, int depth) const {
  os << pad(depth) << "DefBinding @ " << location << std::endl;
  int vals = val.size();
  for (auto &i : order) {
    os << pad(depth+2) << (i.second < vals ? "val " : "fun ") << i.first << " =" << std::endl;
    if (i.second < vals) {
      val[i.second]->format(os, depth+4);
    } else {
      fun[i.second - vals]->format(os, depth+4);
    }
  }
  body->format(os, depth+2);
}

void DefBinding::hash() {
  std::vector<uint64_t> codes;
  for (auto &i : val) {
    i->hash();
    i->hashcode.push(codes);
  }
  for (auto &i : fun) {
    i->hash();
    i->hashcode.push(codes);
  }
  body->hash();
  body->hashcode.push(codes);
  HASH(codes.data(), codes.size()*8, (long)type, hashcode);
}

std::ostream & operator << (std::ostream &os, const Expr *expr) {
  expr->format(os, 0);
  return os;
}
