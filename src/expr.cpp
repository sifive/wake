#include "expr.h"
#include "value.h"
#include "hash.h"
#include "thunk.h"
#include "datatype.h"
#include <cassert>
#include <sstream>

Expr::~Expr() { }
const char *Prim::type = "Prim";
const char *App::type = "App";
const char *Lambda::type = "Lambda";
const char *VarRef::type = "VarRef";
const char *Literal::type = "Literal";
const char *DefBinding::type = "DefBinding";
const char *Construct::type = "Construct";
const char *Destruct::type = "Destruct";
// these are removed by bind
const char *Subscribe::type = "Subscribe";
const char *Match::type = "Match";
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
  os << "): " << typeVar << " @ " << location << std::endl;
}

void VarRef::hash() {
  uint64_t payload[3];
  payload[0] = (long)type;
  payload[1] = depth;
  payload[2] = offset;
  hash3(&payload[0], 24, hashcode);
}

void Subscribe::format(std::ostream &os, int depth) const {
  os << pad(depth) << "Subscribe(" << name << ") @ " << location << std::endl;
}

void Subscribe::hash() {
  assert(0 /* unreachable */);
}

void Memoize::format(std::ostream &os, int depth) const {
  os << pad(depth) << "Memoize: " << typeVar << " @ " << location << std::endl;
  body->format(os, depth+2);
}

void Memoize::hash() {
  body->hash();
  rehash(body->hashcode, type, hashcode);
}

void Match::format(std::ostream &os, int depth) const {
  os << pad(depth) << "Match: " << typeVar << " @ " << location << std::endl;
  for (auto &a: args)
    a->format(os, depth+2);
  for (auto &p: patterns) {
    os << pad(depth+2) << p.pattern << " = " << std::endl;
    p.expr->format(os, depth+4);
  }
}

void Match::hash() {
  std::vector<uint64_t> codes;
  codes.push_back((long)type);
  for (auto &a : args) {
    a->hash();
    a->hashcode.push(codes);
  }
  for (auto &p : patterns) {
    std::stringstream ss;
    ss << p.pattern;
    std::string str = ss.str();
    Hash code;
    hash3(str.c_str(), str.size()+1, code);
    code.push(codes);
    p.expr->hash();
    p.expr->hashcode.push(codes);
  }
  hash3(codes.data(), codes.size()*8, hashcode);
}

void App::format(std::ostream &os, int depth) const {
  os << pad(depth) << "App: " << typeVar << " @ " << location << std::endl;
  fn->format(os, depth+2);
  val->format(os, depth+2);
}

void App::hash() {
  std::vector<uint64_t> codes;
  codes.push_back((long)type);
  fn->hash();
  val->hash();
  fn->hashcode.push(codes);
  val->hashcode.push(codes);
  hash3(codes.data(), codes.size()*8, hashcode);
}

void Lambda::format(std::ostream &os, int depth) const {
  os << pad(depth) << "Lambda(" << name << "): " << typeVar << " @ " << location << std::endl;
  body->format(os, depth+2);
}

void Lambda::hash() {
  body->hash();
  rehash(body->hashcode, type, hashcode);
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
  os << pad(depth) << "Literal: " << typeVar << " @ " << location << " = ";
  value->format(os, -1-depth);
}

void Literal::hash() {
  Hash h = value->hash();
  rehash(h, type, hashcode);
}

void Prim::format(std::ostream &os, int depth) const {
 os << pad(depth) << "Prim(" << args << "," << name << "): " << typeVar << " @ " << location << std::endl;
}

void Prim::hash() {
  hash4(name.data(), name.size(), type, hashcode);
}

void Top::format(std::ostream &os, int depth) const {
  os << pad(depth) << "Top; globals =";
  for (auto &i : globals) os << " " << i.first;
  os << std::endl;
  for (auto &i : defmaps) i->format(os, depth+2);
  body->format(os, depth+2);
}

void Top::hash() {
  assert(0 /* unreachable */);
}

void DefBinding::format(std::ostream &os, int depth) const {
  os << pad(depth) << "DefBinding: " << typeVar << " @ " << location << std::endl;

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

void DefBinding::hash() {
  std::vector<uint64_t> codes;
  codes.push_back((long)type);
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
  hash3(codes.data(), codes.size()*8, hashcode);
}

void Construct::format(std::ostream &os, int depth) const {
  os << pad(depth) << "Construct(" << cons->ast.name << "): " << typeVar << " @ " << location << std::endl;
}

void Construct::hash() {
  hash4(cons->ast.name.data(), cons->ast.name.size(), type, hashcode);
}

void Destruct::format(std::ostream &os, int depth) const {
  os << pad(depth) << "Destruct(" << sum.name << "): " << typeVar << " @ " << location << std::endl;
}

void Destruct::hash() {
  hash4(sum.name.data(), sum.name.size(), type, hashcode);
}

std::ostream & operator << (std::ostream &os, const Expr *expr) {
  expr->format(os, 0);
  return os;
}
