#ifndef EXPR_H
#define EXPR_H

#include "location.h"
#include <memory>
#include <string>
#include <map>
#include <vector>

/* Expression AST */
struct Expr {
  const char *type;
  Location location;

  Expr(const char *type_, const Location &location_) : type(type_), location(location_) { }
  virtual ~Expr();
};

std::ostream & operator << (std::ostream &os, const Expr *expr);

struct Action;
struct Value;
struct Prim : public Expr {
  std::string name;
  int args;

  // The function must call 'resume(completion, value);' when done
  void (*fn)(void *data, const std::vector<Value*> &args, Action *completion);
  void *data;

  static const char *type;
  Prim(const Location &location_, const std::string &name_) : Expr(type, location_), name(name_), args(0) { }
};

struct App : public Expr {
  std::unique_ptr<Expr> fn;
  std::unique_ptr<Expr> val;

  static const char *type;
  App(const Location &location_, Expr *fn_, Expr *val_)
   : Expr(type, location_), fn(fn_), val(val_) { }
};

struct Lambda : public Expr {
  std::string name;
  std::unique_ptr<Expr> body;

  static const char *type;
  Lambda(const Location &location_, const std::string &name_, Expr *body_)
   : Expr(type, location_), name(name_), body(body_) { }
};

struct VarRef : public Expr {
  std::string name;
  int depth;
  int offset;

  static const char *type;
  VarRef(const Location &location_, const std::string &name_, int depth_ = 0, int offset_ = 0)
   : Expr(type, location_), name(name_), depth(depth_), offset(offset_) { }
};

struct Literal : public Expr {
  std::unique_ptr<Value> value;
  static const char *type;

  Literal(const Location &location_, std::unique_ptr<Value> value_);
  Literal(const Location &location_ = Location(), const char *value_ = "bad");
};

struct DefMap : public Expr {
  typedef std::map<std::string, std::unique_ptr<Expr> > defs;
  defs map;
  std::unique_ptr<Expr> body;

  static const char *type;
  DefMap(const Location &location_, defs &map_, Expr *body_)
   : Expr(type, location_), map(), body(body_) { map.swap(map_); }
  DefMap(const Location &location_) : Expr(type, location_), map(), body(new Literal()) { }
};

struct Top : public Expr {
  typedef std::vector<DefMap> DefMaps;
  typedef std::map<std::string, int> Globals;
  DefMaps defmaps;
  Globals globals;

  static const char *type;
  Top() : Expr(type, Location()) { }
};

#endif
