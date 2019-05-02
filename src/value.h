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

#ifndef VALUE_H
#define VALUE_H

#include "hash.h"
#include "type.h"
#include <string>
#include <memory>
#include <vector>
#include <cstdint>
#include <iosfwd>
#include <limits>
#include <gmp.h>

#define APP_PRECEDENCE 22

/* Values */

struct Lambda;
struct Binding;
struct Location;
struct WorkQueue;
struct Value;

struct FormatEntry {
  const Value *value;
  int precedence;
  int state;
  FormatEntry(const Value *value_ = nullptr, int precedence_ = 0, int state_ = 0)
   : value(value_), precedence(precedence_), state(state_) { }
};

struct FormatState {
  std::vector<FormatEntry> stack;
  FormatEntry current;
  bool detailed;
  int indent; // -1 -> single-line
  void resume();
  void child(const Value *value, int precedence);
  int get() const { return current.state; }
  int p() const { return current.precedence; }
};

struct Value {
  const TypeDescriptor *type;

  Value(const TypeDescriptor *type_) : type(type_) { }
  virtual ~Value();

  std::string to_str() const; // one-line version
  static void format(std::ostream &os, const Value *value, bool detailed = false, int indent = -1);

  virtual void format(std::ostream &os, FormatState &state) const = 0;
  virtual TypeVar &getType() = 0;
  virtual Hash hash() const = 0;
};

inline std::ostream & operator << (std::ostream &os, const Value *value) {
  Value::format(os, value);
  return os;
}

struct String : public Value {
  std::string value;

  static const TypeDescriptor type;
  static TypeVar typeVar;
  String(const std::string &value_) : Value(&type), value(value_) { }
  String(std::string &&value_) : Value(&type), value(std::move(value_)) { }

  void format(std::ostream &os, FormatState &state) const;
  TypeVar &getType();
  Hash hash() const;
};

struct Integer : public Value {
  mpz_t value;

  static const TypeDescriptor type;
  static TypeVar typeVar;
  Integer(const char *value_) : Value(&type) { mpz_init_set_str(value, value_, 0); }
  Integer(long value_) : Value(&type) { mpz_init_set_si(value, value_); }
  Integer() : Value(&type) { mpz_init(value); }
  ~Integer();

  std::string str(int base = 10) const;
  void format(std::ostream &os, FormatState &state) const;
  TypeVar &getType();
  Hash hash() const;
};

#define FIXED 0
#define SCIENTIFIC 1
#define HEXFLOAT 2
#define DEFAULTFLOAT 3
struct Double : public Value {
  typedef std::numeric_limits< double > limits;
  double value;

  static const TypeDescriptor type;
  static TypeVar typeVar;

  Double(double value_ = 0) : Value(&type), value(value_) { }
  Double(const char *str);

  std::string str(int format = DEFAULTFLOAT, int precision = limits::max_digits10) const;
  void format(std::ostream &os, FormatState &state) const;
  TypeVar &getType();
  Hash hash() const;
};

struct Closure : public Value {
  Lambda *lambda;
  std::shared_ptr<Binding> binding;

  static const TypeDescriptor type;
  static TypeVar typeVar;
  Closure(Lambda *lambda_, const std::shared_ptr<Binding> &binding_) : Value(&type), lambda(lambda_), binding(binding_) { }
  void format(std::ostream &os, FormatState &state) const;
  TypeVar &getType();
  Hash hash() const;
};

struct Constructor;
struct Data : public Value {
  Constructor *cons;
  std::shared_ptr<Binding> binding;

  static const TypeDescriptor type;
  static TypeVar typeBoolean;
  static TypeVar typeOrder;
  static TypeVar typeUnit;
  static TypeVar typeJValue;
  // these two are const to prevent unify() on them; use clone
  static const TypeVar typeList;
  static const TypeVar typePair;
  Data(Constructor *cons_, std::shared_ptr<Binding> &&binding_) : Value(&type), cons(cons_), binding(std::move(binding_)) { }
  void format(std::ostream &os, FormatState &state) const;
  TypeVar &getType();
  Hash hash() const;
};

struct Cause {
  std::string reason;
  std::vector<Location> stack;
  Cause(const std::string &reason_, std::vector<Location> &&stack_);
};

struct Exception : public Value {
  std::vector<std::shared_ptr<Cause> > causes;

  static const TypeDescriptor type;
  static TypeVar typeVar;
  Exception() : Value(&type) { }
  Exception(const std::string &reason, const std::shared_ptr<Binding> &binding);

  Exception &operator += (const Exception &other) {
    causes.insert(causes.end(), other.causes.begin(), other.causes.end());
    return *this;
  }

  void format(std::ostream &os, FormatState &state) const;
  TypeVar &getType();
  Hash hash() const;
};

#endif
