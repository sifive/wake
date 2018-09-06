#ifndef VALUE_H
#define VALUE_H

#include <string>
#include <vector>
#include <memory>
#include <gmp.h>

/* Values */

struct Expr;
struct Future;

struct Value {
  const char *type;

  Value(const char *type_) : type(type_) { }
  virtual ~Value();
};

std::ostream & operator << (std::ostream &os, const Value *value);

struct String : public Value {
  std::string value;

  static const char *type;
  String(const std::string &value_) : Value(type), value(value_) { }
};

struct Integer : public Value {
  mpz_t value;

  static const char *type;
  Integer(const char *value_) : Value(type) { mpz_init_set_str(value, value_, 0); }
  Integer(long value_) : Value(type) { mpz_init_set_si(value, value_); }
  Integer() : Value(type) { mpz_init(value); }
  ~Integer();

  std::string str(int base = 10) const;
};

struct Binding {
  std::shared_ptr<Binding> next;
  std::vector<std::shared_ptr<Future> > future;

  Binding(const std::shared_ptr<Binding> &next_) : next(next_), future() { }
  Binding(const std::shared_ptr<Binding> &next_, std::shared_ptr<Future> &&arg) : next(next_), future(1, arg) { }
};

struct Closure : public Value {
  Expr *body;
  std::shared_ptr<Binding> bindings;

  static const char *type;
  Closure(Expr *body_, const std::shared_ptr<Binding> &bindings_) : Value(type), body(body_), bindings(bindings_) { }
};

#endif
