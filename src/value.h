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

#define APP_PRECEDENCE 21

/* Values */

struct Lambda;
struct Binding;
struct Location;
struct WorkQueue;

struct Value {
  const TypeDescriptor *type;

  Value(const TypeDescriptor *type_) : type(type_) { }
  virtual ~Value();

  std::string to_str() const; // one-line version
  virtual void format(std::ostream &os, int p) const = 0; // < 0 means indent
  virtual TypeVar &getType() = 0;
  virtual Hash hash() const = 0;
};

std::ostream & operator << (std::ostream &os, const Value *value);

struct String : public Value {
  std::string value;

  static const TypeDescriptor type;
  static TypeVar typeVar;
  String(const std::string &value_) : Value(&type), value(value_) { }
  String(std::string &&value_) : Value(&type), value(std::move(value_)) { }

  void format(std::ostream &os, int depth) const;
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
  void format(std::ostream &os, int depth) const;
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
  void format(std::ostream &os, int depth) const;
  TypeVar &getType();
  Hash hash() const;
};

struct Closure : public Value {
  Lambda *lambda;
  std::shared_ptr<Binding> binding;

  static const TypeDescriptor type;
  static TypeVar typeVar;
  Closure(Lambda *lambda_, const std::shared_ptr<Binding> &binding_) : Value(&type), lambda(lambda_), binding(binding_) { }
  void format(std::ostream &os, int depth) const;
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
  void format(std::ostream &os, int depth) const;
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

  void format(std::ostream &os, int depth) const;
  TypeVar &getType();
  Hash hash() const;
};

#endif
