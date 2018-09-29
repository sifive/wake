#include "value.h"
#include "expr.h"
#include "heap.h"
#include "hash.h"
#include <iostream>
#include <sstream>
#include <cassert>

Hasher::~Hasher() { }
Value::~Value() { }
const char *String::type = "String";
const char *Integer::type = "Integer";
const char *Closure::type = "Closure";
const char *Exception::type = "Exception";

Integer::~Integer() {
  mpz_clear(value);
}

std::string Value::to_str() const {
  std::stringstream str;
  str << this;
  return str.str();
}

std::ostream & operator << (std::ostream &os, const Value *value) {
  value->stream(os);
  return os;
}

void String ::stream(std::ostream &os) const { os << "String(" << value << ")"; }
void Integer::stream(std::ostream &os) const { os << "Integer(" << str() << ")"; }
void Closure::stream(std::ostream &os) const { os << "Closure(" << body->location << ")"; }

void Exception::stream(std::ostream &os) const {
  os << "Exception(" << std::endl;
  for (auto &i : causes) {
    os << "  " << i->reason << std::endl;
    for (auto &j : i->stack) {
      os << "    from " << j << std::endl;
    }
  }
  os << ")" << std::endl;
}

void String::hash(WorkQueue &queue, std::unique_ptr<Hasher> hasher) {
  Hash payload;
  HASH(value.data(), value.size(), (long)type, payload);
  Hasher::receive(queue, std::move(hasher), payload);
}

void Integer::hash(WorkQueue &queue, std::unique_ptr<Hasher> hasher) {
  Hash payload;
  HASH(value[0]._mp_d, abs(value[0]._mp_size)*sizeof(mp_limb_t), (long)type, payload);
  Hasher::receive(queue, std::move(hasher), payload);
}

void Exception::hash(WorkQueue &queue, std::unique_ptr<Hasher> hasher) {
  Hash payload;
  std::string str = to_str();
  HASH(str.data(), str.size(), (long)type, payload);
  Hasher::receive(queue, std::move(hasher), payload);
}

struct ClosureHasher : public Hasher {
  std::unique_ptr<Hasher> chain;
  Expr *body;
  ClosureHasher(std::unique_ptr<Hasher> chain_, Expr *body_) : chain(std::move(chain_)), body(body_) { }
  void receive(WorkQueue &queue, Hash hash) {
    std::vector<uint64_t> codes;
    Hash out;
    hash.push(codes);
    body->hashcode.push(codes);
    HASH(codes.data(), 8*codes.size(), (long)Closure::type, out);
    Hasher::receive(queue, std::move(chain), out);
  }
};

void Closure::hash(WorkQueue &queue, std::unique_ptr<Hasher> hasher) {
  Binding::hash(queue, binding, std::unique_ptr<Hasher>(new ClosureHasher(std::move(hasher), body)));
}

Cause::Cause(const std::string &reason_, std::vector<Location> &&stack_)
 : reason(reason_), stack(std::move(stack_)) { }

Exception::Exception(const std::string &reason, const std::shared_ptr<Binding> &binding) : Value(type) {
  causes.emplace_back(std::make_shared<Cause>(reason, Binding::stack_trace(binding)));
}

std::string Integer::str(int base) const {
  char buffer[mpz_sizeinbase(value, base) + 2];
  mpz_get_str(buffer, base, value);
  return buffer;
}
