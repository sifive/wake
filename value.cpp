#include "value.h"
#include <iostream>
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
  str << this;
  return str.str();
}

std::ostream & operator << (std::ostream &os, const Value *value) {
  if (value->type == String::type) {
    const String *str = reinterpret_cast<const String*>(value);
    return os << "String(" << str->value << ")";
  } else if (value->type == Integer::type) {
    const Integer *integer = reinterpret_cast<const Integer*>(value);
    return os << "Integer(" << integer->str() << ")";
  } else if (value->type == Closure::type) {
    return os << "Closure";
  } else if (value->type == Exception::type) {
    const Exception *exception = reinterpret_cast<const Exception*>(value);
    os << "Exception(";
    for (auto &i : exception->causes)
      os << i.reason; // !!!
    return os << ")";
  } else {
    assert(0 /* unreachable */);
    return os;
  }
}

std::string Integer::str(int base) const {
  char buffer[mpz_sizeinbase(value, base) + 2];
  mpz_get_str(buffer, base, value);
  return buffer;
}
