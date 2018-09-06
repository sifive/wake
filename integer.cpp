#include "prim.h"
#include "value.h"
#include <iostream>
#include <gmp.h>

#define UNOP(name, fn)														\
static void prim_##name(void *data, std::vector<std::shared_ptr<Value> > &&args, std::unique_ptr<Action> &&completion) {	\
  EXPECT_ARGS(1);														\
  Integer *arg0 = GET_INTEGER(0);												\
  Integer *out = new Integer;													\
  fn(out->value, arg0->value);													\
  resume(std::move(completion), std::shared_ptr<Value>(out));									\
}

UNOP(com, mpz_com)
UNOP(abs, mpz_abs)
UNOP(neg, mpz_neg)

#define BINOP(name, fn)														\
static void prim_##name(void *data, std::vector<std::shared_ptr<Value> > &&args, std::unique_ptr<Action> &&completion) {	\
  EXPECT_ARGS(2);														\
  Integer *arg0 = GET_INTEGER(0);												\
  Integer *arg1 = GET_INTEGER(1);												\
  Integer *out = new Integer;													\
  fn(out->value, arg0->value, arg1->value);											\
  resume(std::move(completion), std::shared_ptr<Value>(out));									\
}

BINOP(add, mpz_add)
BINOP(sub, mpz_sub)
BINOP(mul, mpz_mul)
BINOP(div, mpz_tdiv_q)
BINOP(mod, mpz_tdiv_r)
BINOP(xor, mpz_xor)
BINOP(and, mpz_and)
BINOP(or,  mpz_ior)
BINOP(gcd, mpz_gcd)
BINOP(lcm, mpz_lcm)

#define BINOP_SI(name, fn)													\
static void prim_##name(void *data, std::vector<std::shared_ptr<Value> > &&args, std::unique_ptr<Action> &&completion) {	\
  EXPECT_ARGS(2);														\
  Integer *arg0 = GET_INTEGER(0);												\
  Integer *arg1 = GET_INTEGER(1);												\
  Integer *out = new Integer;													\
  if (mpz_sgn(arg1->value) < 0) {												\
    std::cerr << "prim_" #name " called with argument 2 = "									\
      << arg1 << ", which is not a positive integer." << std::endl;								\
    stack_trace(completion);													\
    exit(1);															\
  }																\
  if (!mpz_fits_slong_p(arg1->value) || (mpz_get_si(arg1->value) >> 20) != 0) {							\
    std::cerr << "prim_" #name " called with argument 2 = "									\
      << arg1 << ", which is unreasonably large." << std::endl;									\
    stack_trace(completion);													\
    exit(1);															\
  }																\
  fn(out->value, arg0->value, mpz_get_si(arg1->value));										\
  resume(std::move(completion), std::shared_ptr<Value>(out));									\
}

BINOP_SI(shl, mpz_mul_2exp)
BINOP_SI(shr, mpz_tdiv_q_2exp)
BINOP_SI(exp, mpz_pow_ui)
BINOP_SI(root,mpz_root)

static void prim_powm(void *data, std::vector<std::shared_ptr<Value> > &&args, std::unique_ptr<Action> &&completion) {
  EXPECT_ARGS(3);
  Integer *arg0 = GET_INTEGER(0);
  Integer *arg1 = GET_INTEGER(1);
  Integer *arg2 = GET_INTEGER(2);
  Integer *out = new Integer;
  if (mpz_sgn(arg1->value) <= 0) {
    std::cerr << "prim_powm called with argument 2 = "
      << arg1 << ", which is not a valid exponent > 0." << std::endl;
    stack_trace(completion);
    exit(1);
  }
  mpz_powm(out->value, arg0->value, arg1->value, arg2->value);
  resume(std::move(completion), std::shared_ptr<Value>(out));
}

static void prim_str(void *data, std::vector<std::shared_ptr<Value> > &&args, std::unique_ptr<Action> &&completion) {
  EXPECT_ARGS(2);
  Integer *arg0 = GET_INTEGER(0);
  Integer *arg1 = GET_INTEGER(1);
  long base;
  if (!mpz_fits_slong_p(arg0->value) || (base = mpz_get_si(arg0->value)) > 62 ||
      base < -36 || base == 0 || base == 1 || base == -1) {
    std::cerr << "prim_str called with argument 1 = "
      << arg0 << ", which is not a valid base; [-36,62] \\ [-1,1]." << std::endl;
    stack_trace(completion);
    exit(1);
  }
  resume(std::move(completion), std::shared_ptr<Value>(new String(arg1->str(base))));
}

static void prim_int(void *data, std::vector<std::shared_ptr<Value> > &&args, std::unique_ptr<Action> &&completion) {
  EXPECT_ARGS(2);
  Integer *arg0 = GET_INTEGER(0);
  String  *arg1 = GET_STRING(1);
  Integer *out  = new Integer;
  long base;
  if (!mpz_fits_slong_p(arg0->value) || (base = mpz_get_si(arg0->value)) > 62 || base < 0 || base == 1) {
    std::cerr << "prim_int called with argument 1 = "
      << arg0 << ", which is not a valid base; 0 or [2,62]." << std::endl;
    stack_trace(completion);
    exit(1);
  }
  mpz_set_str(out->value, arg1->value.c_str(), base);
  resume(std::move(completion), std::shared_ptr<Value>(out));
}

// popcount, scan0, scan1 ?

void prim_register_integer(PrimMap &pmap) {
  pmap["com"].first = prim_com;
  pmap["abs"].first = prim_abs;
  pmap["neg"].first = prim_neg;
  pmap["add"].first = prim_add;
  pmap["sub"].first = prim_sub;
  pmap["mul"].first = prim_mul;
  pmap["div"].first = prim_div;
  pmap["mod"].first = prim_mod;
  pmap["xor"].first = prim_xor;
  pmap["and"].first = prim_and;
  pmap["or" ].first = prim_or;
  pmap["gcd"].first = prim_gcd;
  pmap["lcm"].first = prim_lcm;
  pmap["shl"].first = prim_shl;
  pmap["shr"].first = prim_shr;
  pmap["exp"].first = prim_exp;
  pmap["root"].first= prim_root;
  pmap["powm"].first= prim_powm;
  pmap["str"].first = prim_str;
  pmap["int"].first = prim_int;
}
