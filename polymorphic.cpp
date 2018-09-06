#include "prim.h"
#include "value.h"
#include <gmp.h>
#include <cassert>

static void prim_lt(void *data, std::vector<std::shared_ptr<Value> > &&args, std::unique_ptr<Action> &&completion) {
  EXPECT_ARGS(2);
  int cmp;
  if (args[0]->type == Integer::type) {
    Integer *arg0 = GET_INTEGER(0);
    Integer *arg1 = GET_INTEGER(1);
    cmp = mpz_cmp(arg0->value, arg1->value);
  } else if (args[0]->type == String::type) {
    String *arg0 = GET_STRING(0);
    String *arg1 = GET_STRING(1);
    cmp = arg0->value < arg1->value ? -1 : 0;
  } else {
    assert(0 /* unreachable */);
  }
  resume(std::move(completion), cmp < 0 ? make_true() : make_false());
}

static void prim_eq(void *data, std::vector<std::shared_ptr<Value> > &&args, std::unique_ptr<Action> &&completion) {
  EXPECT_ARGS(2);
  int cmp;
  if (args[0]->type == Integer::type) {
    Integer *arg0 = GET_INTEGER(0);
    Integer *arg1 = GET_INTEGER(1);
    cmp = mpz_cmp(arg0->value, arg1->value);
  } else {
    String *arg0 = GET_STRING(0);
    String *arg1 = GET_STRING(1);
    cmp = arg0->value != arg1->value;
  }
  resume(std::move(completion), cmp == 0 ? make_true() : make_false());
}

static void prim_cmp(void *data, std::vector<std::shared_ptr<Value> > &&args, std::unique_ptr<Action> &&completion) {
  EXPECT_ARGS(2);
  int cmp;
  if (args[0]->type == Integer::type) {
    Integer *arg0 = GET_INTEGER(0);
    Integer *arg1 = GET_INTEGER(1);
    cmp = mpz_cmp(arg0->value, arg1->value);
  } else {
    String *arg0 = GET_STRING(0);
    String *arg1 = GET_STRING(1);
    cmp = arg0->value.compare(arg1->value);
  }
  // Normalize it
  int out = (cmp < 0) ? -1 : (cmp > 0) ? 1 : 0;
  resume(std::move(completion), std::shared_ptr<Value>(new Integer(out)));
}

void prim_register_polymorphic(PrimMap &pmap) {
  pmap["lt"].first = prim_lt;
  pmap["eq"].first = prim_eq;
  pmap["cmp"].first = prim_cmp;
}
