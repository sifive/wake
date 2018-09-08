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

void prim_register_polymorphic(PrimMap &pmap) {
  pmap["lt"].first = prim_lt;
  pmap["eq"].first = prim_eq;
  pmap["cmp"].first = prim_cmp;
}
