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
#include "type.h"
#include "expr.h"
#include "ssa.h"
#include "hash.h"
#include "symbol.h"
#include "status.h"
#include "sfinae.h"
#include "tuple.h"
#include <sstream>
#include <string.h>
#include <assert.h>

// typeid().hash_code() changes between runs
#define TYPE_STRING	1
#define TYPE_INTEGER	2
#define TYPE_DOUBLE	3
#define TYPE_REGEXP	4

bool Value::operator == (const Value &x) const {
  assert(0 /* unreachable */);
  return false;
}

size_t Value::hashid() const {
  assert(0 /* unreachable */);
  return 0;
}

void FormatState::resume() {
  stack.emplace_back(current.value, current.precedence, current.state+1);
}

void FormatState::child(const HeapObject *value, int precedence) {
  stack.emplace_back(value, precedence, 0);
}

void HeapObject::format(std::ostream &os, const HeapObject *value, bool detailed, int indent) {
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

std::string HeapObject::to_str() const {
  std::stringstream str;
  str << this;
  return str.str();
}

TypeVar String::typeVar("String", 0);

String::String(size_t length_) : length(length_) { }

String::String(const String &s) : length(s.length) {
  memcpy(data(), s.data(), length+1);
}

String *String::claim(Heap &h, size_t length) {
  return new (h.claim(reserve(length))) String(length);
}

String *String::claim(Heap &h, const std::string &str) {
  String *out = claim(h, str.size());
  memcpy(out->c_str(), str.c_str(), str.size()+1);
  return out;
}

String *String::claim(Heap &h, const char *str, size_t length) {
  auto out = claim(h, length);
  memcpy(out->c_str(), str, length);
  out->c_str()[length] = 0;
  return out;
}

String *String::alloc(Heap &h, size_t length) {
  return new (h.alloc(reserve(length))) String(length);
}

String *String::alloc(Heap &h, const std::string &str) {
  String *out = alloc(h, str.size());
  memcpy(out->data(), str.c_str(), str.size()+1);
  return out;
}

String *String::alloc(Heap &h, const char *str, size_t length) {
  String *out = alloc(h, length);
  memcpy(out->c_str(), str, length);
  out->c_str()[length] = 0;
  return out;
}

String *String::alloc(Heap &h, const char *str) {
  size_t size = strlen(str);
  String *out = alloc(h, size);
  memcpy(out->data(), str, size+1);
  return out;
}

RootPointer<String> String::literal(Heap &h, const std::string &value) {
  h.guarantee(reserve(value.size()));
  String *out = claim(h, value);
  return h.root(out);
}

void String::cstr_format(std::ostream &os, const char *s, size_t len) {
  const char *e = s + len;
  for (const char *i = s; i != e; ++i) switch (char ch = *i) {
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
}

int String::compare(const char *other_data, size_t other_length) const {
  int out = memcmp(data(), other_data, std::min(length, other_length));
  if (out == 0) {
    if (length < other_length) out = -1;
    if (length > other_length) out = 1;
  }
  return out;
}

int String::compare(const char *other_data) const {
  return compare(other_data, strlen(other_data));
}

void String::format(std::ostream &os, FormatState &state) const {
  os << "\"";
  cstr_format(os, c_str(), length);
  os << "\"";
}

Hash String::hash() const {
  return Hash(c_str(), length);
}

size_t String::hashid() const {
  Hash h = hash();
  return h.data[0] ^ h.data[1] ^ TYPE_STRING;
}

bool String::operator == (const Value &x) const {
  if (typeid(x) != typeid(*this)) return false;
  return compare(static_cast<const String &>(x)) == 0;
}

TypeVar Integer::typeVar("Integer", 0);

Integer::Integer(int length_) : length(length_) { }

Integer::Integer(const Integer &i) : length(i.length) {
  memcpy(data(), i.data(), abs(length) * sizeof(mp_limb_t));
}

Integer *Integer::claim(Heap &h, const MPZ &mpz) {
  Integer *out = new (h.claim(reserve(mpz))) Integer(mpz.value[0]._mp_size);
  memcpy(out->data(), mpz.value[0]._mp_d, sizeof(mp_limb_t)*abs(out->length));
  return out;
}

Integer *Integer::alloc(Heap &h, const MPZ &mpz) {
  Integer *out = new (h.alloc(reserve(mpz))) Integer(mpz.value[0]._mp_size);
  memcpy(out->data(), mpz.value[0]._mp_d, sizeof(mp_limb_t)*abs(out->length));
  return out;
}

RootPointer<Integer> Integer::literal(Heap &h, const std::string &value) {
  MPZ mpz(value);
  h.guarantee(reserve(mpz));
  Integer *out = claim(h, mpz);
  return h.root(out);
}

std::string Integer::str(int base) const {
  mpz_t value = { wrap() };
  char buffer[mpz_sizeinbase(value, base) + 2];
  mpz_get_str(buffer, base, value);
  return buffer;
}

void Integer::format(std::ostream &os, FormatState &state) const {
  os << str();
}

Hash Integer::hash() const {
  return Hash(data(), abs(length)*sizeof(mp_limb_t));
}

size_t Integer::hashid() const {
  Hash h = hash();
  return h.data[0] ^ h.data[1] ^ TYPE_INTEGER;
}

bool Integer::operator == (const Value &x) const {
  if (typeid(x) != typeid(*this)) return false;
  mpz_t a = { wrap() }, b = { static_cast<const Integer &>(x).wrap() };
  return mpz_cmp(a, b) == 0;
}

TypeVar Double::typeVar("Double", 0);

RootPointer<Double> Double::literal(Heap &h, const char *str) {
  h.guarantee(reserve());
  Double *out = claim(h, str);
  return h.root(out);
}

void Double::format(std::ostream &os, FormatState &state) const {
  os << str();
}

Hash Double::hash() const {
  return Hash(&value, sizeof(value));
}

size_t Double::hashid() const {
  Hash h = hash();
  return h.data[0] ^ h.data[1] ^ TYPE_DOUBLE;
}

bool Double::operator == (const Value &x) const {
  if (typeid(x) != typeid(*this)) return false;
  return value == static_cast<const Double &>(x).value;
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

TypeVar RegExp::typeVar("RegExp", 0);

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

RegExp::RegExp(Heap &h, const re2::StringPiece &regexp, const RE2::Options &opts)
 : Parent(h), exp(std::make_shared<RE2>(
     has_set_dot_nl<RE2::Options>::value
     ? re2::StringPiece(regexp)
     : re2::StringPiece("(?s)" + regexp.as_string()),
     opts)) { }

RegExp::RegExp(Heap &h, const re2::StringPiece &regexp)
 : RegExp(h, regexp, defops(RE2::Options())) { }

void RegExp::format(std::ostream &os, FormatState &state) const {
  if (APP_PRECEDENCE < state.p()) os << "(";
  os << "RegExp `";
  auto p = exp->pattern();
  os.write(p.c_str(), p.size());
  os << "`";
  if (APP_PRECEDENCE < state.p()) os << ")";
}

Hash RegExp::hash() const {
  return Hash(exp->pattern());
}

size_t RegExp::hashid() const {
  Hash h = hash();
  return h.data[0] ^ h.data[1] ^ TYPE_REGEXP;
}

bool RegExp::operator == (const Value &x) const {
  if (typeid(x) != typeid(*this)) return false;
  return exp->pattern() == static_cast<const RegExp &>(x).exp->pattern();
}

RootPointer<RegExp> RegExp::literal(Heap &h, const std::string &value) {
  h.guarantee(reserve());
  RegExp *out = claim(h, h, value);
  return h.root(out);
}

void Closure::format(std::ostream &os, FormatState &state) const {
  os << "<" << fun->location.file() << ">";
}

Hash Closure::hash() const {
  return fun->hash + Hash(applied);
}

Hash Record::hash() const {
  uint64_t buf[2];
  buf[0] = size();
  buf[1] = cons?cons->index:~static_cast<uint64_t>(0);
  return Hash(&buf[0], sizeof(buf));
}

void Record::format(std::ostream &os, FormatState &state) const {
  const char *name = cons->ast.name.c_str();
  const HeapObject* child = nullptr;
  if (state.get() < (int)size()) {
    const Promise *p = at(state.get());
    if (*p) child = p->coerce<HeapObject>();
  }

  if (strncmp(name, "binary ", 7) == 0) {
    op_type q = op_precedence(name + 7);
    switch (state.get()) {
    case 0:
      if (q.p < state.p()) os << "(";
      state.resume();
      state.child(child, q.p + !q.l);
      break;
    case 1:
      if (name[7] != ',') os << " ";
      os << name + 7 << " ";
      state.resume();
      state.child(child, q.p + q.l);
      break;
    case 2:
      if (q.p < state.p()) os << ")";
      break;
    }
  } else if (strncmp(name, "unary ", 6) == 0) {
    op_type q = op_precedence(name + 6);
    switch (state.get()) {
    case 0:
      if (q.p < state.p()) os << "(";
      os << name + 6;
      state.resume();
      state.child(child, q.p);
      break;
    case 1:
      if (q.p < state.p()) os << ")";
      break;
    }
  } else {
    if (state.get() == 0) {
      if (APP_PRECEDENCE < state.p() && !empty()) os << "(";
      os << name;
    }
    if (state.get() < (int)size()) {
      os << " ";
      state.resume();
      state.child(child, APP_PRECEDENCE+1);
    } else {
      if (APP_PRECEDENCE < state.p() && !empty()) os << ")";
    }
  }
}

Hash Scope::hash() const {
  uint64_t buf[1];
  buf[0] = size();
  return Hash(&buf[0], sizeof(buf));
}

void Scope::format(std::ostream &os, FormatState &state) const {
  const HeapObject* child = (state.get() < (int)size()) ? at(state.get())->coerce<HeapObject>() : nullptr;

  if (state.get() == 0) {
    if (APP_PRECEDENCE < state.p() && !empty()) os << "(";
    os << "Scope ";
  }
  if (state.get() < (int)size()) {
    os << " ";
    state.resume();
    state.child(child, APP_PRECEDENCE+1);
  } else {
    if (APP_PRECEDENCE < state.p() && !empty()) os << ")";
    if (next) state.child(next.get(), APP_PRECEDENCE+1);
  }
}

TypeVar Data::typeBoolean("Boolean", 0);
TypeVar Data::typeOrder("Order", 0);
TypeVar Data::typeUnit("Unit", 0);
TypeVar Data::typeJValue("JValue", 0);
TypeVar Data::typeError("Error", 0);
const TypeVar Data::typeList("List", 1);
const TypeVar Data::typePair("Pair", 2);
const TypeVar Data::typeResult("Result", 2);
