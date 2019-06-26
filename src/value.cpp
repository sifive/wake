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

#include "value.h"
#include "expr.h"
#include "heap.h"
#include "hash.h"
#include "datatype.h"
#include "symbol.h"
#include "status.h"
#include "sfinae.h"
#include <sstream>
#include <cassert>
#include <algorithm>

Value::~Value() { }
const TypeDescriptor String ::type("String");
const TypeDescriptor Integer::type("Integer");
const TypeDescriptor Double ::type("Double");
const TypeDescriptor RegExp ::type("RegExp");
const TypeDescriptor Closure::type("Closure");
const TypeDescriptor Data   ::type("Data");

void FormatState::resume() {
  stack.emplace_back(current.value, current.precedence, current.state+1);
}

void FormatState::child(const Value *value, int precedence) {
  stack.emplace_back(value, precedence, 0);
}

Integer::~Integer() {
  mpz_clear(value);
}

void Value::format(std::ostream &os, const Value *value, bool detailed, int indent) {
  FormatState state;
  state.detailed = detailed;
  state.indent = indent;
  state.stack.emplace_back(value, 0, 0);
  while (!state.stack.empty()) {
    state.current = state.stack.back();
    state.stack.pop_back();
    if (state.current.value) {
      state.current.value->format(os, state);
    } else {
      os << term_red() << "<future>" << term_normal();
    }
  }
}

std::string Value::to_str() const {
  std::stringstream str;
  str << this;
  return str.str();
}

void String::format(std::ostream &os, FormatState &state) const {
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
}

void Integer::format(std::ostream &os, FormatState &state) const {
  os << str();
}

void Double::format(std::ostream &os, FormatState &state) const {
  os << str();
}

// Unfortunately, re2 does not define a VERSION macro.
TEST_MEMBER(set_dot_nl);

template <typename T>
static typename enable_if<has_set_dot_nl<T>::value, void>::type
maybe_set_dot_nl(T &x) {
  x.set_dot_nl(true);
}

template <typename T>
static typename enable_if<!has_set_dot_nl<T>::value, void>::type
maybe_set_dot_nl(T &x) {
  // noop
}

// This monstrous method is the only way I could find to
// portably call methods on a temporary without a copy.
// (older re2 did not have copy-construction for Options)
static const RE2::Options &defops(RE2::Options &&options) {
  options.set_log_errors(false);
  options.set_one_line(true);
  maybe_set_dot_nl(options);
  return options;
}

RegExp::RegExp(const std::string &regexp, const RE2::Options &opts)
 : Value(&type),
   exp(
     has_set_dot_nl<RE2::Options>::value
     ? re2::StringPiece(regexp)
     : re2::StringPiece("(?s)" + regexp),
     opts) { }
RegExp::RegExp(const std::string &regexp)
 : RegExp(regexp, defops(RE2::Options())) { }

void RegExp::format(std::ostream &os, FormatState &state) const {
  if (APP_PRECEDENCE < state.p()) os << "(";
  os << "RegExp ";
  String(exp.pattern()).format(os, state);
  if (APP_PRECEDENCE < state.p()) os << ")";
}

void Closure::format(std::ostream &os, FormatState &state) const {
  os << "<" << lambda->location.file() << ">";
  // !!! if (state.detailed) print referenced variables only
}

void Data::format(std::ostream &os, FormatState &state) const {
  const std::string &name = cons->ast.name;

  const Value* child = 0;
  if (!cons->ast.args.empty()) {
    int index = cons->ast.args.size() - 1 - state.get();
    for (const Binding *iter = binding.get(); iter; iter = iter->next.get()) {
      if (index >= iter->nargs) {
        index -= iter->nargs;
      } else {
        child = iter->future[iter->nargs-1-index].value.get();
        break;
      }
    }
  }

  if (name.substr(0, 7) == "binary ") {
    op_type q = op_precedence(name.c_str() + 7);
    switch (state.get()) {
    case 0:
      if (q.p < state.p()) os << "(";
      state.resume();
      state.child(child, q.p + !q.l);
      break;
    case 1:
      if (name[7] != ',') os << " ";
      os << name.c_str() + 7 << " ";
      state.resume();
      state.child(child, q.p + q.l);
      break;
    case 2:
      if (q.p < state.p()) os << ")";
      break;
    }
  } else if (name.substr(0, 6) == "unary ") {
    op_type q = op_precedence(name.c_str() + 6);
    switch (state.get()) {
    case 0:
      if (q.p < state.p()) os << "(";
      os << name.c_str() + 6;
      state.resume();
      state.child(child, q.p);
      break;
    case 1:
      if (q.p < state.p()) os << ")";
      break;
    }
  } else {
    if (state.get() == 0) {
      if (APP_PRECEDENCE < state.p() && !cons->ast.args.empty()) os << "(";
      os << name;
    }
    if (state.get() < (int)cons->ast.args.size()) {
      os << " ";
      state.resume();
      state.child(child, APP_PRECEDENCE+1);
    } else {
      if (APP_PRECEDENCE < state.p() && !cons->ast.args.empty()) os << ")";
    }
  }
}

TypeVar String::typeVar("String", 0);
TypeVar &String::getType() {
  return typeVar;
}

Hash String::hash() const {
  return Hash(value) + type.hashcode;
}

TypeVar Integer::typeVar("Integer", 0);
TypeVar &Integer::getType() {
  return typeVar;
}

Hash Integer::hash() const {
  return Hash(value[0]._mp_d, abs(value[0]._mp_size)*sizeof(mp_limb_t)) + type.hashcode;
}

TypeVar Double::typeVar("Double", 0);
TypeVar &Double::getType() {
  return typeVar;
}

std::string Double::str(int format, int precision) const {
  std::stringstream s;
  char buf[80];
  s.precision(precision);
  switch (format) {
    case FIXED:      s << std::fixed      << value; break;
    case SCIENTIFIC: s << std::scientific << value; break;
    // std::hexfloat is not available pre g++ 5.1
    case HEXFLOAT:   snprintf(buf, sizeof(buf), "%.*a", precision, value); s << buf; break;
    default: if (value < 0.1 && value > -0.1) s << std::scientific; s << value; break;
  }
  if (format == DEFAULTFLOAT) {
    std::string out = s.str();
    if (out.find('.') == std::string::npos &&
        out.find('e') == std::string::npos &&
        (out[0] == '-' ? out[1] >= '0' && out[1] <= '9'
                       : out[0] >= '0' && out[0] <= '9')) {
      s << "e0";
    } else {
      return out;
    }
  }
  return s.str();
}

Hash Double::hash() const {
  return Hash(str(HEXFLOAT)) + type.hashcode;
}

TypeVar RegExp::typeVar("RegExp", 0);
TypeVar &RegExp::getType() {
  return typeVar;
}

Hash RegExp::hash() const {
  return Hash(exp.pattern()) + type.hashcode;
}

TypeVar Closure::typeVar("Closure", 0);
TypeVar &Closure::getType() {
  assert (0); // unreachable
  return typeVar;
}

Hash Closure::hash() const {
  std::vector<uint64_t> codes;
  type.hashcode.push(codes);
  lambda->hashcode.push(codes);
  if (binding) {
    assert (binding->flags & FLAG_HASH_POST);
    binding->hashcode.push(codes);
  }
  return Hash(codes);
}

TypeVar Data::typeBoolean("Boolean", 0);
TypeVar Data::typeOrder("Order", 0);
TypeVar Data::typeUnit("Unit", 0);
TypeVar Data::typeJValue("JValue", 0);
TypeVar Data::typeError("Error", 0);
const TypeVar Data::typeList("List", 1);
const TypeVar Data::typePair("Pair", 2);
const TypeVar Data::typeResult("Result", 2);
TypeVar &Data::getType() {
  assert (0); // unreachable
  return typeBoolean;
}

Hash Data::hash() const {
  std::vector<uint64_t> codes;
  Hash(cons->ast.name).push(codes);
  if (binding) {
    assert (binding->flags & FLAG_HASH_POST);
    binding->hashcode.push(codes);
  }
  return Hash(codes);
}

std::string Integer::str(int base) const {
  char buffer[mpz_sizeinbase(value, base) + 2];
  mpz_get_str(buffer, base, value);
  return buffer;
}
