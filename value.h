#ifndef VALUE_H
#define VALUE_H

#include <string>

/* Values */

struct Expr;

struct Value {
  const char *type;

  Value(const char *type_) : type(type_) { }
  virtual ~Value();
};

struct String : public Value {
  static const char *type;
  std::string value;

  String(const std::string& value_) : Value(type), value(value_) { }
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
