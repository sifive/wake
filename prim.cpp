#include "prim.h"
#include "value.h"
#include "expr.h"
#include "action.h"
#include <cstdlib>
#include <sstream>

std::unique_ptr<Action> expect_args(const char *fn, std::unique_ptr<Action> completion, const std::vector<std::shared_ptr<Value> > &args, int expect) {
  if (args.size() != (size_t)expect) {
    std::stringstream str;
    str << fn << " called on " << args.size() << "; was expecting " << expect << std::endl;
    resume(std::move(completion), std::shared_ptr<Value>(new Exception(str.str())));
    return std::unique_ptr<Action>();
  }

  // merge exceptions
  std::shared_ptr<Exception> exception(new Exception);
  for (auto &i : args) {
    if (i->type == Exception::type) {
      (*exception) += *reinterpret_cast<Exception*>(i.get());
    }
  }

  if (exception->causes.empty()) {
    return completion;
  } else {
    resume(std::move(completion), std::move(exception));
    return std::unique_ptr<Action>();
  }
}

std::unique_ptr<Action> cast_string(std::unique_ptr<Action> completion, const std::shared_ptr<Value> &value, String **str) {
  if (value->type != String::type) {
    std::stringstream str;
    str << value << " is not a String";
    resume(std::move(completion), std::shared_ptr<Value>(new Exception(str.str())));
    return std::unique_ptr<Action>();
  } else {
    *str = reinterpret_cast<String*>(value.get());
    return completion;
  }
}

std::unique_ptr<Action> cast_integer(std::unique_ptr<Action> completion, const std::shared_ptr<Value> &value, Integer **in) {
  if (value->type != Integer::type) {
    std::stringstream str;
    str << value << " is not an Integer";
    resume(std::move(completion), std::shared_ptr<Value>(new Exception(str.str())));
    return std::unique_ptr<Action>();
  } else {
    *in = reinterpret_cast<Integer*>(value.get());
    return completion;
  }
}

// true  x y = x
static std::unique_ptr<Expr> eTrue(new Lambda(LOCATION, "_", new VarRef(LOCATION, "_", 1, 0)));
std::shared_ptr<Value> make_true() {
  return std::shared_ptr<Value>(new Closure(eTrue.get(), 0));
}

// false x y = y
static std::unique_ptr<Expr> eFalse(new Lambda(LOCATION, "_", new VarRef(LOCATION, "_", 0, 0)));
std::shared_ptr<Value> make_false() {
  return std::shared_ptr<Value>(new Closure(eFalse.get(), 0));
}

// nill x y z = y
// pair x y f = f x y # with x+y already bound
static std::unique_ptr<Expr> eNill(new Lambda(LOCATION, "_", new Lambda(LOCATION, "_", new VarRef(LOCATION, "_", 1, 0))));
static std::unique_ptr<Expr> ePair(new App(LOCATION, new App(LOCATION, new VarRef(LOCATION, "_", 0, 0), new VarRef(LOCATION, "_", 1, 0)), new VarRef(LOCATION, "_", 1, 1)));
std::shared_ptr<Value> make_list(const std::vector<std::shared_ptr<Value> > &values) {
  std::shared_ptr<Value> out(new Closure(eNill.get(), 0));
  for (auto i = values.rbegin(); i != values.rend(); ++i) {
    std::shared_ptr<Binding> binding(new Binding(0, 0));
    binding->future.push_back(std::shared_ptr<Future>(new Future(*i)));
    binding->future.push_back(std::shared_ptr<Future>(new Future(std::move(out))));
    out = std::shared_ptr<Value>(new Closure(ePair.get(), binding));
  }
  return out;
}
