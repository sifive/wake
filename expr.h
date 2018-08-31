#ifndef EXPR_H
#define EXPR_H

#include <memory>
#include <string>
#include <map>

/* Expression AST */
struct Expr {
  const char *type;

  Expr(const char *type_) : type(type_) { }
  virtual ~Expr();
};

std::ostream& operator<<(std::ostream& os, const Expr *expr);

struct App : public Expr {
  std::unique_ptr<Expr> fn;
  std::unique_ptr<Expr> val;

  static const char *type;
  App(Expr* fn_, Expr* val_) : Expr(type), fn(fn_), val(val_) { }
};

struct Lambda : public Expr {
  std::string name;
  std::unique_ptr<Expr> body;

  static const char *type;
  Lambda(const std::string& name_, Expr* body_) : Expr(type), name(name_), body(body_) { }
};

struct VarRef : public Expr {
  std::string name;
  std::string location;
  int depth;
  int offset;

  static const char *type;
  VarRef(const std::string& name_, const std::string &location_) : Expr(type), name(name_), location(location_) { }
};

struct DefMap : public Expr {
  typedef std::map<std::string, std::unique_ptr<Expr> > defs;
  defs map;
  std::unique_ptr<Expr> body;

  static const char *type;
  DefMap(defs& map_, Expr* body_) : Expr(type), map(), body(body_) { map.swap(map_); }
};

#endif
