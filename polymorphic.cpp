#include "prim.h"
#include "value.h"
#include "action.h"
#include <gmp.h>
#include <sstream>

static void prim_lt(void *data, std::vector<std::shared_ptr<Value> > &&args, std::unique_ptr<Action> &&completion) {
  EXPECT(2);
  int cmp;
  if (args[0]->type == Integer::type) {
    INTEGER(arg0, 0);
    INTEGER(arg1, 1);
    cmp = mpz_cmp(arg0->value, arg1->value);
  } else if (args[0]->type == String::type) {
    STRING(arg0, 0);
    STRING(arg1, 1);
    cmp = arg0->value < arg1->value ? -1 : 0;
  } else {
    std::stringstream str;
    str << args[0] << " and " << args[0] << "can not be compared";
    REQUIRE(false, str.str());
  }
  RETURN(cmp < 0 ? make_true() : make_false());
}

static void prim_eq(void *data, std::vector<std::shared_ptr<Value> > &&args, std::unique_ptr<Action> &&completion) {
  EXPECT(2);
  bool eq;
  if (args[0]->type == Integer::type) {
    INTEGER(arg0, 0);
    INTEGER(arg1, 1);
    eq = mpz_cmp(arg0->value, arg1->value) == 0;
  } else if (args[0]->type == String::type) {
    STRING(arg0, 0);
    STRING(arg1, 1);
    eq = arg0->value == arg1->value;
  } else {
    eq = false;
  }
  RETURN(eq ? make_true() : make_false());
}

static void prim_cmp(void *data, std::vector<std::shared_ptr<Value> > &&args, std::unique_ptr<Action> &&completion) {
  EXPECT(2);
  int cmp;
  if (args[0]->type == Integer::type) {
    INTEGER(arg0, 0);
    INTEGER(arg1, 1);
    cmp = mpz_cmp(arg0->value, arg1->value);
  } else if (args[0]->type == String::type) {
    STRING(arg0, 0);
    STRING(arg1, 1);
    cmp = arg0->value.compare(arg1->value);
  } else {
    std::stringstream str;
    str << args[0] << " and " << args[0] << "can not be compared";
    REQUIRE(false, str.str());
  }
  // Normalize it
  int out = (cmp < 0) ? -1 : (cmp > 0) ? 1 : 0;
  RETURN(new Integer(out));
}

static void prim_test(void *data, std::vector<std::shared_ptr<Value> > &&args, std::unique_ptr<Action> &&completion) {
  if (args.size() != 1) {
    resume(std::move(completion), std::shared_ptr<Value>(new Exception("prim_test called on " + std::to_string(args.size()) + "; was exepecting 1")));
  } else {
    resume(std::move(completion), args[0]->type == Exception::type ? make_true() : make_false());
  }
}

static void prim_catch(void *data, std::vector<std::shared_ptr<Value> > &&args, std::unique_ptr<Action> &&completion) {
  if (args.size() != 1 || args[0]->type != Exception::type) {
    resume(std::move(completion), std::shared_ptr<Value>(new Exception("prim_catch not called on an exception")));
  } else {
    Exception *exception = reinterpret_cast<Exception*>(args[0].get());
    std::vector<std::shared_ptr<Value> > out;
    for (auto &i : exception->causes)
      out.emplace_back(new String(i.reason));
    resume(std::move(completion), make_list(out));
  }
}

static void prim_raise(void *data, std::vector<std::shared_ptr<Value> > &&args, std::unique_ptr<Action> &&completion) {
  EXPECT(1);
  STRING(arg0, 0);
  resume(std::move(completion), std::shared_ptr<Value>(new Exception(arg0->value)));
}

void prim_register_polymorphic(PrimMap &pmap) {
  pmap["lt"].first = prim_lt;
  pmap["eq"].first = prim_eq;
  pmap["cmp"].first = prim_cmp;
  pmap["test"].first = prim_test;
  pmap["catch"].first = prim_catch;
  pmap["raise"].first = prim_raise;
}
