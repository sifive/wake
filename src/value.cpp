#include "value.h"
#include "expr.h"
#include "heap.h"
#include "hash.h"
#include <sstream>
#include <cassert>

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
  format(str, -1);
  return str.str();
}

std::ostream & operator << (std::ostream &os, const Value *value) {
  value->format(os, 0);
  return os;
}

static std::string pad(int depth) {
  return std::string(depth, ' ');
}

void String::format(std::ostream &os, int depth) const {
  os << "String(" << value << ")";
  if (depth >= 0) os << std::endl;
}

void Integer::format(std::ostream &os, int depth) const {
  os << "Integer(" << str() << ")";
  if (depth >= 0) os << std::endl;
}

void Closure::format(std::ostream &os, int depth) const {
  os << "Closure @ " << lambda->location;
  if (depth >= 0) {
    os << ":" << std::endl;
    if (binding) binding->format(os, depth+2);
  }
}

void Binding::format(std::ostream &os, int depth) const {
  std::vector<const Binding*> todo;
  const Binding *iter;
  for (iter = this; iter; iter = iter->next.get()) {
    if ((iter->flags & FLAG_PRINTED) != 0) break;
    iter->flags |= FLAG_PRINTED;
    todo.push_back(iter);
  }
  for (size_t i = todo.size(); i > 0; --i) {
    iter = todo[i-1];
    for (int i = 0; i < iter->nargs; ++i) {
      if (iter->expr->type == Lambda::type) {
        Lambda *lambda = reinterpret_cast<Lambda*>(iter->expr);
        os << pad(depth) << lambda->name << " = "; // " @ " << lambda->location << " = ";
      }
      if (iter->expr->type == DefBinding::type) {
        DefBinding *defbinding = reinterpret_cast<DefBinding*>(iter->expr);
        std::string name;
        for (auto &x : defbinding->order) if (x.second == i) name = x.first;
        if (name.find(' ') != std::string::npos) continue; // internal pub/sub detail
        os << pad(depth) << name << " = "; // " @ " << defbinding->val[i]->location << " = ";
      }
      if (iter->future[i].value) {
        iter->future[i].value->format(os, depth);
      } else {
        os << "UNRESOLVED FUTURE" << std::endl;
      }
    }
  }
}

void Exception::format(std::ostream &os, int depth) const {
  os << "Exception";
  if (depth >= 0) {
    os << std::endl;
    for (auto &i : causes) {
      os << pad(depth+2) << i->reason << std::endl;
      for (auto &j : i->stack) {
        os << pad(depth+4) << "from " << j << std::endl;
      }
    }
  } else if (!causes.empty()) {
    os << "(" << causes[0]->reason << ")";
  }
}

TypeVar String::typeVar("String", 0);
TypeVar &String::getType() {
  return typeVar;
}

Hash String::hash() const {
  Hash payload;
  HASH(value.data(), value.size(), (long)type, payload);
  return payload;
}

TypeVar Integer::typeVar("Integer", 0);
TypeVar &Integer::getType() {
  return typeVar;
}

Hash Integer::hash() const {
  Hash payload;
  HASH(value[0]._mp_d, abs(value[0]._mp_size)*sizeof(mp_limb_t), (long)type, payload);
  return payload;
}

TypeVar Exception::typeVar("Exception", 0);
TypeVar &Exception::getType() {
  assert (0); // unreachable
  return typeVar;
}

Hash Exception::hash() const {
  Hash payload;
  std::string str = to_str();
  HASH(str.data(), str.size(), (long)type, payload);
  return payload;
}

TypeVar Closure::typeVar("Closure", 0);
TypeVar &Closure::getType() {
  assert (0); // unreachable
  return typeVar;
}

Hash Closure::hash() const {
  Hash out;
  std::vector<uint64_t> codes;
  lambda->hashcode.push(codes);
  if (binding) {
    assert (binding->flags & FLAG_HASH_POST);
    binding->hashcode.push(codes);
  }
  HASH(codes.data(), 8*codes.size(), (long)Closure::type, out);
  return out;
}

TypeVar Data::typeBool("Bool", 0);
const TypeVar Data::typeList("List", 1);
const TypeVar Data::typePair("Pair", 2);
TypeVar &Data::getType() {
  assert (0); // unreachable
  return typeBool;
}

Cause::Cause(const std::string &reason_, std::vector<Location> &&stack_)
 : reason(reason_), stack(std::move(stack_)) { }

Exception::Exception(const std::string &reason, const std::shared_ptr<Binding> &binding) : Value(type) {
  causes.emplace_back(std::make_shared<Cause>(reason, binding->stack_trace()));
}

std::string Integer::str(int base) const {
  char buffer[mpz_sizeinbase(value, base) + 2];
  mpz_get_str(buffer, base, value);
  return buffer;
}
