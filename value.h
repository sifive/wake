#ifndef VALUE_H
#define VALUE_H

#include <string>
#include <gmp.h>

/* Values */

struct Expr;

struct Value {
  const char *type;

  Value(const char *type_) : type(type_) { }
  virtual ~Value();
};

std::ostream& operator << (std::ostream& os, const Value *value);

struct String : public Value {
  std::string value;

  static const char *type;
  String(const std::string& value_) : Value(type), value(value_) { }
};

struct Integer : public Value {
  mpz_t value;

  static const char *type;
  Integer(const char *value_) : Value(type) { mpz_init_set_str(value, value_, 0); }
  Integer() : Value(type) { mpz_init(value); }
  ~Integer();

  std::string str(int base = 10) const;
};

struct Thunk;
struct Binding {
  Thunk *thunk;
  Binding *next;

  Binding(Thunk *thunk_, Binding *next_) : thunk(thunk_), next(next_) { }
};

struct Closure : public Value {
  Expr *body;
  Binding *bindings;

  static const char *type;
  Closure(Expr *body_, Binding *bindings_) : Value(type), body(body_), bindings(bindings_) { }
};

#endif
