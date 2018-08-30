#ifndef EXPR_H
#define EXPR_H

#include <memory>
#include <string>

/* Expression AST */
struct Expr {
  const char *type;

  Expr(const char *type_) : type(type_) { }
  virtual ~Expr();
};

struct App : public Expr {
  std::unique_ptr<Expr> fn;
  std::unique_ptr<Expr> val;

  static const char *type;
  App(std::unique_ptr<Expr> fn_, std::unique_ptr<Expr> val_) : Expr(type), fn(std::move(fn_)), val(std::move(val_)) { }
};

struct Lambda : public Expr {
  std::string name;
  std::unique_ptr<Expr> body;

  static const char *type;
  Lambda(const std::string& name_, std::unique_ptr<Expr> body_) : Expr(type), name(name_), body(std::move(body_)) { }
};

struct VarRef : public Expr {
  std::string name;
  int index; // >= 0: global, < 0: arg

  static const char *type;
  VarRef(const std::string& name_) : Expr(type), name(name_) { }
};

#endif
