#include "prim.h"
#include "value.h"
#include "expr.h"
#include <iostream>
#include <cstdlib>

void expect_args(const char *fn, const std::unique_ptr<Action> &completion, const std::vector<std::shared_ptr<Value> > &args, int expect) {
  if (args.size() != (size_t)expect) {
    std::cerr << fn << " called on " << args.size() << "; was expecting " << expect << std::endl;
    stack_trace(completion);
    exit(1);
  }
}

String *expect_string(const char *fn, const std::unique_ptr<Action> &completion, const std::shared_ptr<Value> &value, int index) {
  if (value->type != String::type) {
    std::cerr << fn << " called with argument "
      << index << " = "
      << value << ", which is not a String." << std::endl;
    stack_trace(completion);
    exit(1);
  }
  return reinterpret_cast<String*>(value.get());
}

Integer *expect_integer(const char *fn, const std::unique_ptr<Action> &completion, const std::shared_ptr<Value> &value, int index) {
  if (value->type != Integer::type) {
    std::cerr << fn << " called with argument "
      << index << " = "
      << value << ", which is not an Integer." << std::endl;
    stack_trace(completion);
    exit(1);
  }
  return reinterpret_cast<Integer*>(value.get());
}

std::shared_ptr<Value> make_true() {
  Location location = LOCATION;
  return std::shared_ptr<Value>(new Closure(new Lambda(location, "_", new VarRef(location, "_", 1, 0)), 0));
}

std::shared_ptr<Value> make_false() {
  Location location = LOCATION;
  return std::shared_ptr<Value>(new Closure(new Lambda(location, "_", new VarRef(location, "_", 0, 0)), 0));
}

std::shared_ptr<Value> make_list(const std::vector<std::shared_ptr<Value> > &values) {
  std::shared_ptr<Value> out = make_true();
  for (auto i = values.rbegin(); i != values.rend(); ++i) {
    // \f f (*i) out
    Expr *tail = new Literal(LOCATION, out);
    Expr *head = new Literal(LOCATION, *i);
    Expr *f    = new VarRef(LOCATION, "_");
    Expr *app1 = new App(LOCATION, f, head);
    Expr *app2 = new App(LOCATION, app1, tail);
    out = std::shared_ptr<Value>(new Closure(app2, 0));
  }
  return out;
}
