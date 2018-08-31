#include <iostream>
#include "expr.h"

Expr::~Expr() { }
const char *App::type = "App";
const char *Lambda::type = "Lambda";
const char *VarRef::type = "VarRef";
const char *DefMap::type = "DefMap";

std::ostream& operator<<(std::ostream& os, const Expr *expr) {
  if (expr->type == VarRef::type) {
    const VarRef *ref = reinterpret_cast<const VarRef*>(expr);
    return os << "VarRef(" << ref->name << ")";
  } else if (expr->type == App::type) {
    const App *app = reinterpret_cast<const App*>(expr);
    return os << "App(" << app->fn.get() << "," << app->val.get() << ")";
  } else if (expr->type == Lambda::type) {
    const Lambda *lambda = reinterpret_cast<const Lambda*>(expr);
    return os << "Lambda(" << lambda->name << "," << lambda->body.get() << ")";
  } else if (expr->type == DefMap::type) {
    const DefMap *def = reinterpret_cast<const DefMap*>(expr);
    os << "DefMap(";
    for (auto i = def->map.begin(); i != def->map.end(); ++i)
      os << "  " << i->first << " = " << i->second.get() << std::endl;
    return os << "  " << def->body.get() << ")" << std::endl;
  } else {
    assert(0);
  }
}
