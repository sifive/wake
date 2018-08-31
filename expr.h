#ifndef EXPR_H
#define EXPR_H

#include "location.h"
#include <memory>
#include <string>
#include <map>

/* Expression AST */
struct Expr {
  const char *type;
  Location location;

  Expr(const char *type_, const Location& location_) : type(type_), location(location_) { }
  virtual ~Expr();
};

std::ostream& operator << (std::ostream& os, const Expr *expr);

struct App : public Expr {
  std::unique_ptr<Expr> fn;
  std::unique_ptr<Expr> val;

  static const char *type;
  App(const Location& location_, Expr* fn_, Expr* val_)
   : Expr(type, location_), fn(fn_), val(val_) { }
};

struct Lambda : public Expr {
  std::string name;
  std::unique_ptr<Expr> body;

  static const char *type;
  Lambda(const Location& location_, const std::string& name_, Expr* body_)
   : Expr(type, location_), name(name_), body(body_) { }
};

struct VarRef : public Expr {
  std::string name;
  int depth;
  int offset;

  static const char *type;
  VarRef(const Location& location_, const std::string& name_)
   : Expr(type, location_), name(name_) { }
};

struct DefMap : public Expr {
  typedef std::map<std::string, std::unique_ptr<Expr> > defs;
  defs map;
  std::unique_ptr<Expr> body;

  static const char *type;
  DefMap(const Location& location_, defs& map_, Expr* body_)
   : Expr(type, location_), map(), body(body_) { map.swap(map_); }
};

struct Value;
struct Literal : public Expr {
  std::unique_ptr<Value> value;
  static const char *type;

  Literal(const Location& location_, std::unique_ptr<Value> value_);
};

#endif
