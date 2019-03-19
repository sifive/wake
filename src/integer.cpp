#include "prim.h"
#include "value.h"
#include "heap.h"
#include <gmp.h>

#define UNOP(name, fn)				\
static PRIMFN(prim_##name) {			\
  EXPECT(1);					\
  INTEGER(arg0, 0);				\
  auto out = std::make_shared<Integer>();	\
  fn(out->value, arg0->value);			\
  RETURN(out);					\
}

UNOP(com, mpz_com)
UNOP(abs, mpz_abs)
UNOP(neg, mpz_neg)

#define BINOP(name, fn)				\
static PRIMFN(prim_##name) {			\
  EXPECT(2);					\
  INTEGER(arg0, 0);				\
  INTEGER(arg1, 1);				\
  auto out = std::make_shared<Integer>();	\
  fn(out->value, arg0->value, arg1->value);	\
  RETURN(out);					\
}

BINOP(add, mpz_add)
BINOP(sub, mpz_sub)
BINOP(mul, mpz_mul)
BINOP(xor, mpz_xor)
BINOP(and, mpz_and)
BINOP(or,  mpz_ior)
BINOP(gcd, mpz_gcd)
BINOP(lcm, mpz_lcm)

#define BINOP_ZERO(name, fn)					\
static PRIMFN(prim_##name) {					\
  EXPECT(2);							\
  INTEGER(arg0, 0);						\
  INTEGER(arg1, 1);						\
  REQUIRE(mpz_cmp_si(arg1->value, 0) != 0, "division by 0");	\
  auto out = std::make_shared<Integer>();			\
  fn(out->value, arg0->value, arg1->value);			\
  RETURN(out);							\
}

BINOP_ZERO(div, mpz_tdiv_q)
BINOP_ZERO(mod, mpz_tdiv_r)

#define BINOP_SI(name, fn)								\
static PRIMFN(prim_##name) {								\
  EXPECT(2);										\
  INTEGER(arg0, 0);									\
  INTEGER(arg1, 1);									\
  REQUIRE(mpz_sgn(arg1->value) >= 0, arg1->to_str() + " is negative");			\
  REQUIRE(mpz_cmp_si(arg1->value, 1<<20) < 0, arg1->to_str() + " is too large");	\
  auto out = std::make_shared<Integer>();						\
  fn(out->value, arg0->value, mpz_get_si(arg1->value));					\
  RETURN(out);										\
}

BINOP_SI(shl, mpz_mul_2exp)
BINOP_SI(shr, mpz_tdiv_q_2exp)
BINOP_SI(exp, mpz_pow_ui)
BINOP_SI(root,mpz_root)

static PRIMTYPE(type_powm) {
  return args.size() == 3 &&
    args[0]->unify(Integer::typeVar) &&
    args[1]->unify(Integer::typeVar) &&
    args[2]->unify(Integer::typeVar) &&
    out->unify(Integer::typeVar);
}

static PRIMFN(prim_powm) {
  EXPECT(3);
  INTEGER(arg0, 0);
  INTEGER(arg1, 1);
  INTEGER(arg2, 2);
  REQUIRE(mpz_sgn(arg1->value) >= 0, arg1->to_str() + " is negative");
  auto out = std::make_shared<Integer>();
  mpz_powm(out->value, arg0->value, arg1->value, arg2->value);
  RETURN(out);
}

static PRIMTYPE(type_str) {
  return args.size() == 2 &&
    args[0]->unify(Integer::typeVar) &&
    args[1]->unify(Integer::typeVar) &&
    out->unify(String::typeVar);
}

static PRIMFN(prim_str) {
  EXPECT(2);
  INTEGER(arg0, 0);
  INTEGER(arg1, 1);
  long base = 0;
  bool ok = mpz_fits_slong_p(arg0->value);
  if (ok) {
    base = mpz_get_si(arg0->value);
    ok &= base <= 62 && base >= -36 && base != 0 && base != 1 && base != -1;
  }
  REQUIRE(ok, arg0->to_str() + " is not a valid base; [-36,62] \\ [-1,1]");
  auto out = std::make_shared<String>(arg1->str(base));
  RETURN(out);
}

static PRIMTYPE(type_int) {
  return args.size() == 2 &&
    args[0]->unify(Integer::typeVar) &&
    args[1]->unify(String::typeVar) &&
    out->unify(Integer::typeVar);
}

static PRIMFN(prim_int) {
  EXPECT(2);
  INTEGER(arg0, 0);
  STRING(arg1, 1);
  long base = 0;
  bool ok = mpz_fits_slong_p(arg0->value);
  if (ok) {
    base = mpz_get_si(arg0->value);
    ok &= base <= 62 && base >= 0 && base != 1;
  }
  REQUIRE(ok, arg0->to_str() + " is not a valid base; 0 or [2,62]");
  auto out = std::make_shared<Integer>();
  if (mpz_set_str(out->value, arg1->value.c_str(), base)) {
    RAISE("String " + arg1->value + " is not in Integer format");
  } else {
    RETURN(out);
  }
}

// popcount, scan0, scan1 ?

static PRIMTYPE(type_unop) {
  return args.size() == 1 &&
    args[0]->unify(Integer::typeVar) &&
    out->unify(Integer::typeVar);
}

static PRIMTYPE(type_binop) {
  return args.size() == 2 &&
    args[0]->unify(Integer::typeVar) &&
    args[1]->unify(Integer::typeVar) &&
    out->unify(Integer::typeVar);
}

static PRIMTYPE(type_icmp) {
  return args.size() == 2 &&
    args[0]->unify(Integer::typeVar) &&
    args[1]->unify(Integer::typeVar) &&
    out->unify(Data::typeOrder);
}

static PRIMFN(prim_icmp) {
  EXPECT(2);
  INTEGER(arg0, 0);
  INTEGER(arg1, 1);
  auto out = make_order(mpz_cmp(arg0->value, arg1->value));
  RETURN(out);
}

void prim_register_integer(PrimMap &pmap) {
  pmap.emplace("com", PrimDesc(prim_com, type_unop,  PRIM_PURE|PRIM_SHALLOW));
  pmap.emplace("abs", PrimDesc(prim_abs, type_unop,  PRIM_PURE|PRIM_SHALLOW));
  pmap.emplace("neg", PrimDesc(prim_neg, type_unop,  PRIM_PURE|PRIM_SHALLOW));
  pmap.emplace("add", PrimDesc(prim_add, type_binop, PRIM_PURE|PRIM_SHALLOW));
  pmap.emplace("sub", PrimDesc(prim_sub, type_binop, PRIM_PURE|PRIM_SHALLOW));
  pmap.emplace("mul", PrimDesc(prim_mul, type_binop, PRIM_PURE|PRIM_SHALLOW));
  pmap.emplace("div", PrimDesc(prim_div, type_binop, PRIM_PURE|PRIM_SHALLOW));
  pmap.emplace("mod", PrimDesc(prim_mod, type_binop, PRIM_PURE|PRIM_SHALLOW));
  pmap.emplace("xor", PrimDesc(prim_xor, type_binop, PRIM_PURE|PRIM_SHALLOW));
  pmap.emplace("and", PrimDesc(prim_and, type_binop, PRIM_PURE|PRIM_SHALLOW));
  pmap.emplace("or",  PrimDesc(prim_or,  type_binop, PRIM_PURE|PRIM_SHALLOW));
  pmap.emplace("gcd", PrimDesc(prim_gcd, type_binop, PRIM_PURE|PRIM_SHALLOW));
  pmap.emplace("lcm", PrimDesc(prim_lcm, type_binop, PRIM_PURE|PRIM_SHALLOW));
  pmap.emplace("shl", PrimDesc(prim_shl, type_binop, PRIM_PURE|PRIM_SHALLOW));
  pmap.emplace("shr", PrimDesc(prim_shr, type_binop, PRIM_PURE|PRIM_SHALLOW));
  pmap.emplace("exp", PrimDesc(prim_exp, type_binop, PRIM_PURE|PRIM_SHALLOW));
  pmap.emplace("root",PrimDesc(prim_root,type_binop, PRIM_PURE|PRIM_SHALLOW));
  pmap.emplace("powm",PrimDesc(prim_powm,type_powm,  PRIM_PURE|PRIM_SHALLOW));
  pmap.emplace("str", PrimDesc(prim_str, type_str,   PRIM_PURE|PRIM_SHALLOW));
  pmap.emplace("int", PrimDesc(prim_int, type_int,   PRIM_PURE|PRIM_SHALLOW));
  pmap.emplace("icmp",PrimDesc(prim_icmp,type_icmp,  PRIM_PURE|PRIM_SHALLOW));
}
