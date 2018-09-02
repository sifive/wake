#include "expr.h"
#include "value.h"
#include <iostream>
#include <cassert>

Expr::~Expr() { }
const char *Prim::type = "Prim";
const char *App::type = "App";
const char *Lambda::type = "Lambda";
const char *VarRef::type = "VarRef";
const char *DefMap::type = "DefMap";
const char *Literal::type = "Literal";

Literal::Literal(const Location& location_, std::unique_ptr<Value> value_)
 : Expr(type, location_), value(std::move(value_)) { }

Literal::Literal(const Location& location_, const char *value_)
 : Expr(type, location_), value(new String(value_)) { }

static std::string pad(int depth) {
  return std::string(depth, ' ');
}

static void format(std::ostream& os, int depth, const Expr *expr) {
  if (expr->type == VarRef::type) {
    const VarRef *ref = reinterpret_cast<const VarRef*>(expr);
    os << pad(depth) << "VarRef(" << ref->name << ") @ " << ref->location.str() << std::endl;
  } else if (expr->type == App::type) {
    const App *app = reinterpret_cast<const App*>(expr);
    os << pad(depth) << "App @ " << app->location.str() << std::endl;
    format(os, depth+2, app->fn.get());
    format(os, depth+2, app->val.get());
  } else if (expr->type == Lambda::type) {
    const Lambda *lambda = reinterpret_cast<const Lambda*>(expr);
    os << pad(depth) << "Lambda(" << lambda->name << ") @ " << lambda->location.str() << std::endl;
    format(os, depth+2, lambda->body.get());
  } else if (expr->type == DefMap::type) {
    const DefMap *def = reinterpret_cast<const DefMap*>(expr);
    os << pad(depth) << "DefMap @ " << def->location.str() << std::endl;
    for (auto i = def->map.begin(); i != def->map.end(); ++i) {
      os << pad(depth+2) << i->first << " =" << std::endl;
      format(os, depth+4, i->second.get());
    }
    format(os, depth+2, def->body.get());
  } else if (expr->type == Literal::type) {
    const Literal *lit = reinterpret_cast<const Literal*>(expr);
    os << pad(depth) << "Literal(" << lit->value.get() << ") @ " << lit->location.str() << std::endl;
  } else if (expr->type == Prim::type) {
    const Prim *prim = reinterpret_cast<const Prim*>(expr);
    os << pad(depth) << "Prim(" << prim->args << "," << prim->name << ") @ " << prim->location.str() << std::endl;
  } else {
    assert(0 /* unreachable */);
  }
}

std::ostream& operator << (std::ostream& os, const Expr *expr) {
  format(os, 0, expr);
  return os;
}
