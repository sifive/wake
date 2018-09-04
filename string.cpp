#include "prim.h"
#include "value.h"
#include <gmp.h>

static void prim_cat(void *data, const std::vector<Value*> &args, Action *completion) {
  EXPECT_ARGS(2);
  String *arg0 = GET_STRING(0);
  String *arg1 = GET_STRING(1);
  resume(completion, new String(arg0->value + arg1->value));
}

static void prim_len(void *data, const std::vector<Value*> &args, Action *completion) {
  EXPECT_ARGS(1);
  String *arg0 = GET_STRING(0);
  Integer *out = new Integer;
  mpz_set_ui(out->value, arg0->value.size());
  resume(completion, out);
}

static void prim_cut(void *data, const std::vector<Value*> &args, Action *completion) {
  EXPECT_ARGS(3);
  String  *arg0 = GET_STRING(0);
  Integer *arg1 = GET_INTEGER(1);
  Integer *arg2 = GET_INTEGER(2);
  size_t begin, end, len = arg0->value.size();

  if (mpz_sgn(arg1->value) < 0) {
    begin = 0;
  } else if (mpz_cmp_ui(arg1->value, len) >= 0) {
    begin = len;
  } else {
    begin = mpz_get_ui(arg1->value);
  }

  if (mpz_sgn(arg2->value) < 0) {
    end = 0;
  } else if (mpz_cmp_ui(arg2->value, len) >= 0) {
    end = len;
  } else {
    end = mpz_get_ui(arg2->value);
  }

  if (begin > end) begin = end;
  resume(completion, new String(arg0->value.substr(begin, end-begin)));
}

void prim_register_string(PrimMap &pmap) {
  pmap["cat"].first = prim_cat;
  pmap["len"].first = prim_len;
  pmap["cut"].first = prim_cut;
}
