#include "value.h"
#include "expr.h"
#include "heap.h"
#include "hash.h"
#include "datatype.h"
#include "symbol.h"
#include <sstream>
#include <cassert>
#include <algorithm>

Value::~Value() { }
const char *String::type = "String";
const char *Integer::type = "Integer";
const char *Closure::type = "Closure";
const char *Data::type = "Data";
const char *Exception::type = "Exception";

Integer::~Integer() {
  mpz_clear(value);
}

std::string Value::to_str() const {
  std::stringstream str;
  format(str, 0);
  return str.str();
}

std::ostream & operator << (std::ostream &os, const Value *value) {
  value->format(os, 0);
  return os;
}

static std::string pad(int depth) {
  if (depth < 0) depth = -depth-1;
  return std::string(depth, ' ');
}

void String::format(std::ostream &os, int p) const {
  os << "\"";
  for (char ch : value) switch (ch) {
    case '"': os << "\\\""; break;
    case '\\': os << "\\\\"; break;
    case '{': os << "\\{"; break;
    case '}': os << "\\}"; break;
    case '\a': os << "\\a"; break;
    case '\b': os << "\\b"; break;
    case '\f': os << "\\f"; break;
    case '\n': os << "\\n"; break;
    case '\r': os << "\\r"; break;
    case '\t': os << "\\t"; break;
    case '\v': os << "\\v"; break;
    default: {
      unsigned char c = ch;
      if (c < 10) {
        os << "\\x0" << (char)('0' + c);
      } else if (c < 0x10) {
        os << "\\x0" << (char)('a' + c - 10);
      } else if (c < 0x10 + 10) {
        os << "\\x1" << (char)('0' + c - 16);
      } else if (c < 0x20) {
        os << "\\x1" << (char)('a' + c - 16 - 10);
      } else {
        os << ch;
      }
      break;
    }
  }
  os << "\"";
  if (p < 0) os << std::endl;
}

void Integer::format(std::ostream &os, int p) const {
  os << str();
  if (p < 0) os << std::endl;
}

void Closure::format(std::ostream &os, int p) const {
  if (p >= 0) {
    os << "<" << lambda->location << ">";
  } else {
    os << "Closure @ " << lambda->location;
    os << ":" << std::endl;
    if (binding) binding->format(os, p-2);
  }
}

static void future_format(std::ostream &os, const Future &f, int p) {
  if (!f.value) {
    os << "<future>";
  } else {
    f.value->format(os, p);
  }
}

void Data::format(std::ostream &os, int p) const {
  const std::string &name = cons->ast.name;
  std::vector<const Future*> todo;
  for (const Binding *iter = binding.get(); iter; iter = iter->next.get())
    for (int i = iter->nargs-1; i >= 0; --i)
      todo.push_back(&iter->future[i]);
  std::reverse(todo.begin(), todo.end());

  if (p >= 0) {
    if (name.substr(0, 7) == "binary ") {
      op_type q = op_precedence(name.c_str() + 7);
      if (q.p < p) os << "(";
      future_format(os, *todo[0], q.p + !q.l);
      if (name[7] != ',') os << " ";
      os << name.c_str() + 7 << " ";
      future_format(os, *todo[1], q.p + q.l);
      if (q.p < p) os << ")";
    } else if (name.substr(0, 6) == "unary ") {
      op_type q = op_precedence(name.c_str() + 6);
      if (q.p < p) os << "(";
      os << name.c_str() + 6;
      future_format(os, *todo[0], q.p);
      if (q.p < p) os << ")";
    } else {
      op_type q = op_precedence("a");
      if (q.p < p && !todo.empty()) os << "(";
      os << name;
      for (auto v : todo) {
        os << " ";
        future_format(os, *v, q.p + q.l);
      }
      if (q.p < p && !todo.empty()) os << ")";
    }
  } else {
    os << name;
    if (!todo.empty()) os << ":";
    os << std::endl;
    for (auto v : todo) {
      os << pad(p-2);
      future_format(os, *v, p-2);
    }
  }
}

void Binding::format(std::ostream &os, int p) const {
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
        os << pad(p) << lambda->name << " = "; // " @ " << lambda->location << " = ";
      }
      if (iter->expr->type == DefBinding::type) {
        DefBinding *defbinding = reinterpret_cast<DefBinding*>(iter->expr);
        std::string name;
        for (auto &x : defbinding->order) if (x.second == i) name = x.first;
        if (name.find(' ') != std::string::npos) continue; // internal pub/sub detail
        os << pad(p) << name << " = "; // " @ " << defbinding->val[i]->location << " = ";
      }
      if (iter->future[i].value) {
        iter->future[i].value->format(os, p);
      } else {
        os << "UNRESOLVED FUTURE" << std::endl;
      }
    }
  }
}

void Exception::format(std::ostream &os, int p) const {
  op_type q = op_precedence("a");
  if (q.p < p) os << "(";

  os << "Exception";
  if (p < 0) {
    os << std::endl;
    for (auto &i : causes) {
      os << pad(p-2) << "\"" << i->reason << "\"" << std::endl;
      for (auto &j : i->stack) {
        os << pad(p-4) << "from " << j << std::endl;
      }
    }
  } else if (!causes.empty()) {
    os << " \"" << causes[0]->reason << "\"";
  }

  if (q.p < p) os << ")";
}

TypeVar String::typeVar("String", 0);
TypeVar &String::getType() {
  return typeVar;
}

Hash String::hash() const {
  Hash payload;
  hash4(value.data(), value.size(), type, payload);
  return payload;
}

TypeVar Integer::typeVar("Integer", 0);
TypeVar &Integer::getType() {
  return typeVar;
}

Hash Integer::hash() const {
  Hash payload;
  hash4(value[0]._mp_d, abs(value[0]._mp_size)*sizeof(mp_limb_t), type, payload);
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
  hash4(str.data(), str.size(), type, payload);
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
  codes.push_back((long)Closure::type);
  lambda->hashcode.push(codes);
  if (binding) {
    assert (binding->flags & FLAG_HASH_POST);
    binding->hashcode.push(codes);
  }
  hash3(codes.data(), 8*codes.size(), out);
  return out;
}

TypeVar Data::typeBoolean("Boolean", 0);
TypeVar Data::typeOrder("Order", 0);
TypeVar Data::typeUnit("Unit", 0);
TypeVar Data::typeJValue("JValue", 0);
const TypeVar Data::typeList("List", 1);
const TypeVar Data::typePair("Pair", 2);
TypeVar &Data::getType() {
  assert (0); // unreachable
  return typeBoolean;
}

Hash Data::hash() const {
  Hash out;
  std::vector<uint64_t> codes;
  codes.push_back((long)cons);
  if (binding) {
    assert (binding->flags & FLAG_HASH_POST);
    binding->hashcode.push(codes);
  }
  hash3(codes.data(), 8*codes.size(), out);
  return out;
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
