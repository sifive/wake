#ifndef EXPR_H
#define EXPR_H

#include "location.h"
#include "primfn.h"
#include "hash.h"
#include <memory>
#include <string>
#include <map>
#include <vector>

struct Receiver;
struct Value;

#define FLAG_TOUCHED 1

/* Expression AST */
struct Expr {
  const char *type;
  Location location;
  Hash hashcode;
  long flags;

  Expr(const char *type_, const Location &location_, long flags_ = 0) : type(type_), location(location_), flags(flags_) { }
  virtual ~Expr();

  std::string to_str() const;
  virtual void format(std::ostream &os, int depth) const = 0;
  virtual void hash() = 0;
};

std::ostream & operator << (std::ostream &os, const Expr *expr);

struct Prim : public Expr {
  std::string name;
  int args;

  PrimFn fn;
  void *data;

  static const char *type;
  Prim(const Location &location_, const std::string &name_) : Expr(type, location_), name(name_), args(0) { }

  void format(std::ostream &os, int depth) const;
  void hash();
};

struct App : public Expr {
  std::unique_ptr<Expr> fn;
  std::unique_ptr<Expr> val;

  static const char *type;
  App(const Location &location_, Expr *fn_, Expr *val_)
   : Expr(type, location_), fn(fn_), val(val_) { }

  void format(std::ostream &os, int depth) const;
  void hash();
};

struct Lambda : public Expr {
  std::string name;
  std::unique_ptr<Expr> body;

  static const char *type;
  Lambda(const Location &location_, const std::string &name_, Expr *body_)
   : Expr(type, location_), name(name_), body(body_) { }

  void format(std::ostream &os, int depth) const;
  void hash();
};

struct VarRef : public Expr {
  std::string name;
  int depth;
  int offset;

  static const char *type;
  VarRef(const Location &location_, const std::string &name_, int depth_ = 0, int offset_ = -1)
   : Expr(type, location_), name(name_), depth(depth_), offset(offset_) { }

  void format(std::ostream &os, int depth) const;
  void hash();
};

struct Literal : public Expr {
  std::shared_ptr<Value> value;
  static const char *type;

  Literal(const Location &location_, std::shared_ptr<Value> &&value_);
  Literal(const Location &location_, const char *value_);

  void format(std::ostream &os, int depth) const;
  void hash();
};

struct Memoize : public Expr {
  std::unique_ptr<Expr> body;
  std::map<Hash, std::shared_ptr<Value> > values;

  static const char *type;
  Memoize(const Location &location_, Expr *body_)
   : Expr(type, location_), body(body_) { }

  void format(std::ostream &os, int depth) const;
  void hash();
};

struct Subscribe : public Expr {
  std::string name;
  static const char *type;
  Subscribe(const Location &location_, const std::string &name_)
   : Expr(type, location_), name(name_) { }

  void format(std::ostream &os, int depth) const;
  void hash();
};

typedef std::map<std::string, int> DefOrder;

struct DefMap : public Expr {
  typedef std::map<std::string, std::unique_ptr<Expr> > defs;
  defs map;
  defs publish;
  std::unique_ptr<Expr> body;

  static const char *type;
  DefMap(const Location &location_, defs &&map_, defs &&publish_, Expr *body_)
   : Expr(type, location_), map(std::move(map_)), publish(std::move(publish_)), body(body_) { }
  DefMap(const Location &location_) : Expr(type, location_), map(), publish(), body(new Literal(location, "top")) { }

  void format(std::ostream &os, int depth) const;
  void hash();
};

struct Top : public Expr {
  typedef std::vector<DefMap> DefMaps;
  DefMaps defmaps;
  DefOrder globals;
  std::unique_ptr<Expr> body;

  static const char *type;
  Top() : Expr(type, LOCATION), defmaps(), globals() { }

  void format(std::ostream &os, int depth) const;
  void hash();
};

// Created by transforming DefMap+Top
struct DefBinding : public Expr {
  typedef std::vector<std::unique_ptr<Expr> > values;
  typedef std::vector<std::unique_ptr<Lambda> > functions;

  std::unique_ptr<Expr> body;
  values val;     // access prior binding
  functions fun;  // access current binding
  DefOrder order; // values, then functions

  static const char *type;
  DefBinding(const Location &location_, std::unique_ptr<Expr> body_) : Expr(type, location_), body(std::move(body_)) { }

  void format(std::ostream &os, int depth) const;
  void hash();
};

#endif
