#include "prim.h"
#include "value.h"
#include "action.h"
#include <gmp.h>

static void prim_cat(void *data, std::vector<std::shared_ptr<Value> > &&args, std::unique_ptr<Action> completion) {
  EXPECT(2);
  STRING(arg0, 0);
  STRING(arg1, 1);
  auto out = std::make_shared<String>(arg0->value + arg1->value);
  RETURN(out);
}

static void prim_len(void *data, std::vector<std::shared_ptr<Value> > &&args, std::unique_ptr<Action> completion) {
  EXPECT(1);
  STRING(arg0, 0);
  auto out = std::make_shared<Integer>(arg0->value.size());
  RETURN(out);
}

static void prim_cut(void *data, std::vector<std::shared_ptr<Value> > &&args, std::unique_ptr<Action> completion) {
  EXPECT(3);
  STRING(arg0, 0);
  INTEGER(arg1, 1);
  INTEGER(arg2, 2);
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
  auto out = std::make_shared<String>(arg0->value.substr(begin, end-begin));
  RETURN(out);
}

void prim_register_string(PrimMap &pmap) {
  pmap["cat"].first = prim_cat;
  pmap["len"].first = prim_len;
  pmap["cut"].first = prim_cut;
}
