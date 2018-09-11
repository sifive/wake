#include "prim.h"
#include "value.h"
#include "heap.h"
#include <gmp.h>

#define UNOP(name, fn)													\
static void prim_##name(void *data, std::vector<std::shared_ptr<Value> > &&args, std::unique_ptr<Receiver> completion) {	\
  EXPECT(1);														\
  INTEGER(arg0, 0);													\
  auto out = std::make_shared<Integer>();										\
  fn(out->value, arg0->value);												\
  RETURN(out);														\
}

UNOP(com, mpz_com)
UNOP(abs, mpz_abs)
UNOP(neg, mpz_neg)

#define BINOP(name, fn)													\
static void prim_##name(void *data, std::vector<std::shared_ptr<Value> > &&args, std::unique_ptr<Receiver> completion) {	\
  EXPECT(2);														\
  INTEGER(arg0, 0);													\
  INTEGER(arg1, 1);													\
  auto out = std::make_shared<Integer>();										\
  fn(out->value, arg0->value, arg1->value);										\
  RETURN(out);														\
}

BINOP(add, mpz_add)
BINOP(sub, mpz_sub)
BINOP(mul, mpz_mul)
BINOP(xor, mpz_xor)
BINOP(and, mpz_and)
BINOP(or,  mpz_ior)
BINOP(gcd, mpz_gcd)
BINOP(lcm, mpz_lcm)

#define BINOP_ZERO(name, fn)												\
static void prim_##name(void *data, std::vector<std::shared_ptr<Value> > &&args, std::unique_ptr<Receiver> completion) {	\
  EXPECT(2);														\
  INTEGER(arg0, 0);													\
  INTEGER(arg1, 1);													\
  REQUIRE(mpz_cmp_si(arg1->value, 0) != 0, "division by 0");								\
  auto out = std::make_shared<Integer>();										\
  fn(out->value, arg0->value, arg1->value);										\
  RETURN(out);														\
}

BINOP_ZERO(div, mpz_tdiv_q)
BINOP_ZERO(mod, mpz_tdiv_r)

#define BINOP_SI(name, fn)												\
static void prim_##name(void *data, std::vector<std::shared_ptr<Value> > &&args, std::unique_ptr<Receiver> completion) {	\
  EXPECT(2);														\
  INTEGER(arg0, 0);													\
  INTEGER(arg1, 1);													\
  REQUIRE(mpz_sgn(arg1->value) >= 0, arg1->to_str() + " is negative");		 					\
  REQUIRE(mpz_cmp_si(arg1->value, 1<<20) < 0, arg1->to_str() + " is too large");					\
  auto out = std::make_shared<Integer>();										\
  fn(out->value, arg0->value, mpz_get_si(arg1->value));									\
  RETURN(out);														\
}

BINOP_SI(shl, mpz_mul_2exp)
BINOP_SI(shr, mpz_tdiv_q_2exp)
BINOP_SI(exp, mpz_pow_ui)
BINOP_SI(root,mpz_root)

static void prim_powm(void *data, std::vector<std::shared_ptr<Value> > &&args, std::unique_ptr<Receiver> completion) {
  EXPECT(3);
  INTEGER(arg0, 0);
  INTEGER(arg1, 1);
  INTEGER(arg2, 2);
  REQUIRE(mpz_sgn(arg1->value) >= 0, arg1->to_str() + " is negative");
  auto out = std::make_shared<Integer>();
  mpz_powm(out->value, arg0->value, arg1->value, arg2->value);
  RETURN(out);
}

static void prim_str(void *data, std::vector<std::shared_ptr<Value> > &&args, std::unique_ptr<Receiver> completion) {
  EXPECT(2);
  INTEGER(arg0, 0);
  INTEGER(arg1, 1);
  long base;
  bool ok = mpz_fits_slong_p(arg0->value);
  if (ok) {
    base = mpz_get_si(arg0->value);
    ok &= base <= 62 && base >= -36 && base != 0 && base != 1 && base != -1;
  }
  REQUIRE(ok, arg0->to_str() + " is not a valid base; [-36,62] \\ [-1,1]");
  auto out = std::make_shared<String>(arg1->str(base));
  RETURN(out);
}

static void prim_int(void *data, std::vector<std::shared_ptr<Value> > &&args, std::unique_ptr<Receiver> completion) {
  EXPECT(2);
  INTEGER(arg0, 0);
  STRING(arg1, 1);
  long base;
  bool ok = mpz_fits_slong_p(arg0->value);
  if (ok) {
    base = mpz_get_si(arg0->value);
    ok &= base <= 62 && base > 0 && base != 1;
  }
  REQUIRE(ok, arg0->to_str() + " is not a valid base; 0 or [2,62]");
  auto out = std::make_shared<Integer>();
  mpz_set_str(out->value, arg1->value.c_str(), base);
  RETURN(out);
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
