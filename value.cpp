#include "value.h"
#include <iostream>
#include <cassert>

Value::~Value() { }
const char *String::type = "String";
const char *Integer::type = "Integer";
const char *Closure::type = "Closure";

std::ostream& operator << (std::ostream& os, const Value *value) {
  if (value->type == String::type) {
    const String *str = reinterpret_cast<const String*>(value);
    return os << "String(" << str->value << ")";
  } else if (value->type == Integer::type) {
    const Integer *integer = reinterpret_cast<const Integer*>(value);
    return os << "Integer(" << integer->value.get_str(10) << ")";
  } else if (value->type == Closure::type) {
    return os << "Closure";
  } else {
    assert(0 /* unreachable */);
    return os;
  }
}
