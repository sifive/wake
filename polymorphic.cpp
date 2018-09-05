#include "prim.h"
#include "value.h"
#include <gmp.h>

static void prim_lt(void *data, const std::vector<Value*> &args, Action *completion) {
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
  resume(completion, cmp < 0 ? prim_true : prim_false);
}

static void prim_eq(void *data, const std::vector<Value*> &args, Action *completion) {
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
  resume(completion, cmp == 0 ? prim_true : prim_false);
}

static void prim_cmp(void *data, const std::vector<Value*> &args, Action *completion) {
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
  Integer *out = new Integer;
  mpz_set_si(out->value, (cmp < 0) ? -1 : (cmp > 0) ? 1 : 0);
  resume(completion, out);
}

void prim_register_polymorphic(PrimMap &pmap) {
  pmap["lt"].first = prim_lt;
  pmap["eq"].first = prim_eq;
  pmap["cmp"].first = prim_cmp;
}
