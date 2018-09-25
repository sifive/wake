#include "prim.h"
#include "value.h"
#include "heap.h"
#include <gmp.h>
#include <sstream>

static PRIMFN(prim_lt) {
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
    RAISE(str.str());
  }
  auto out = cmp < 0 ? make_true() : make_false();
  RETURN(out);
}

static PRIMFN(prim_eq) {
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
  auto out = eq ? make_true() : make_false();
  RETURN(out);
}

static PRIMFN(prim_cmp) {
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
    RAISE(str.str());
  }
  // Normalize it
  auto out = std::make_shared<Integer>((cmp < 0) ? -1 : (cmp > 0) ? 1 : 0);
  RETURN(out);
}

static PRIMFN(prim_test) {
  if (args.size() != 1) {
    Receiver::receiveM(queue, std::move(completion),
      std::make_shared<Exception>("prim_test called on " + std::to_string(args.size()) + "; was exepecting 1", binding));
  } else {
    Receiver::receiveM(queue, std::move(completion), args[0]->type == Exception::type ? make_true() : make_false());
  }
}

static PRIMFN(prim_catch) {
  if (args.size() != 1 || args[0]->type != Exception::type) {
    Receiver::receiveM(queue, std::move(completion),
      std::make_shared<Exception>("prim_catch not called on an exception", binding));
  } else {
    Exception *exception = reinterpret_cast<Exception*>(args[0].get());
    std::vector<std::shared_ptr<Value> > out;
    for (auto &i : exception->causes)
      out.emplace_back(std::make_shared<String>(i->reason));
    Receiver::receiveM(queue, std::move(completion), make_list(std::move(out)));
  }
}

static PRIMFN(prim_raise) {
  EXPECT(1);
  STRING(arg0, 0);
  Receiver::receiveM(queue, std::move(completion), std::make_shared<Exception>(arg0->value, binding));
}

void prim_register_polymorphic(PrimMap &pmap) {
  pmap["lt"].first = prim_lt;
  pmap["eq"].first = prim_eq;
  pmap["cmp"].first = prim_cmp;
  pmap["test"].first = prim_test;
  pmap["catch"].first = prim_catch;
  pmap["raise"].first = prim_raise;
}
