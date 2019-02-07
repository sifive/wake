#include "prim.h"
#include "value.h"
#include "heap.h"
#include <cmath>
#include <ctgmath>
#include <cstdlib>
#include <gmp.h>

static PRIMTYPE(type_unop) {
  return args.size() == 1 &&
    args[0]->unify(Double::typeVar) &&
    out->unify(Double::typeVar);
}

#define UNOP(name, fn)				\
static PRIMFN(prim_##name) {			\
  EXPECT(1);					\
  DOUBLE(arg0, 0);				\
  auto out = std::make_shared<Double>();	\
  out->value = fn(arg0->value);			\
  RETURN(out);					\
}

static double neg(double x) { return -x; }
UNOP(abs,   std::abs)
UNOP(neg,   neg)
UNOP(cos,   std::cos)
UNOP(sin,   std::sin)
UNOP(tan,   std::tan)
UNOP(acos,  std::acos)
UNOP(asin,  std::asin)
UNOP(exp,   std::exp)
UNOP(log,   std::log)
UNOP(expm1, std::expm1)
UNOP(log1p, std::log1p)
UNOP(erf,   std::erf)
UNOP(erfc,  std::erfc)
UNOP(tgamma,std::tgamma)
UNOP(lgamma,std::lgamma)

static PRIMTYPE(type_binop) {
  return args.size() == 2 &&
    args[0]->unify(Double::typeVar) &&
    args[1]->unify(Double::typeVar) &&
    out->unify(Double::typeVar);
}

#define BINOP(name, fn)				\
static PRIMFN(prim_##name) {			\
  EXPECT(2);					\
  DOUBLE(arg0, 0);				\
  DOUBLE(arg1, 1);				\
  auto out = std::make_shared<Double>();	\
  out->value = fn(arg0->value, arg1->value);	\
  RETURN(out);					\
}

static double add(double x, double y) { return x + y; }
static double sub(double x, double y) { return x - y; }
static double mul(double x, double y) { return x * y; }
static double div(double x, double y) { return x / y; }

BINOP(add, add)
BINOP(sub, sub)
BINOP(mul, mul)
BINOP(div, div)
BINOP(pow, pow)
BINOP(atan, std::atan2)

static PRIMTYPE(type_fma) {
  return args.size() == 3 &&
    args[0]->unify(Double::typeVar) &&
    args[1]->unify(Double::typeVar) &&
    args[2]->unify(Double::typeVar) &&
    out->unify(Double::typeVar);
}

static PRIMFN(prim_fma) {
  EXPECT(3);
  DOUBLE(arg0, 0);
  DOUBLE(arg1, 1);
  DOUBLE(arg2, 2);
  auto out = std::make_shared<Double>(std::fma(arg0->value, arg1->value, arg2->value));
  RETURN(out);
}

static PRIMTYPE(type_str) {
  return args.size() == 3 &&
    args[0]->unify(Integer::typeVar) &&
    args[1]->unify(Integer::typeVar) &&
    args[2]->unify(Double::typeVar) &&
    out->unify(String::typeVar);
}

static PRIMFN(prim_str) {
  EXPECT(3);
  INTEGER(arg0, 0);
  INTEGER(arg1, 1);
  DOUBLE(arg2, 2);
  long format, precision;

  bool ok = mpz_fits_slong_p(arg0->value);
  if (ok) {
    format = mpz_get_si(arg0->value);
    ok &= format >= 0 && format <= 3;
  }
  REQUIRE(ok, arg0->to_str() + " is not a valid format [0,3]");

  ok = mpz_fits_slong_p(arg1->value);
  if (ok) {
    precision = mpz_get_si(arg1->value);
    ok &= precision >= 1 && precision <= 40;
  }
  REQUIRE(ok, arg1->to_str() + " is not a valid precision [1,40]");

  auto out = std::make_shared<String>(arg2->str(format, precision));
  RETURN(out);
}

static PRIMTYPE(type_dbl) {
  return args.size() == 1 &&
    args[0]->unify(String::typeVar) &&
    out->unify(Double::typeVar);
}

static PRIMFN(prim_dbl) {
  EXPECT(1);
  STRING(arg0, 0);
  char *end;
  auto out = std::make_shared<Double>(strtod(arg0->value.c_str(), &end));
  if (*end) {
    RAISE("String " + arg0->value + " is not in Double format");
  } else {
    RETURN(out);
  }
}

static PRIMTYPE(type_cmp) {
  return args.size() == 2 &&
    args[0]->unify(Double::typeVar) &&
    args[1]->unify(Double::typeVar) &&
    out->unify(Data::typeOrder);
}

static PRIMFN(prim_cmp) {
  EXPECT(2);
  DOUBLE(arg0, 0);
  DOUBLE(arg1, 1);
  if (std::isnan(arg0->value) || std::isnan(arg1->value)) {
    RAISE("cannot order nan");
  } else {
    int x = (arg0->value > arg1->value) - (arg0->value < arg1->value);
    auto out = make_order(x);
    RETURN(out);
  }
}

static PRIMTYPE(type_class) {
  return args.size() == 1 &&
    args[0]->unify(Double::typeVar) &&
    out->unify(Integer::typeVar);
}

static PRIMFN(prim_class) {
  EXPECT(1);
  DOUBLE(arg0, 0);
  int code;
  switch (std::fpclassify(arg0->value)) {
    case FP_INFINITE:  code = 1; break;
    case FP_NAN:       code = 2; break;
    case FP_ZERO:      code = 3; break;
    case FP_SUBNORMAL: code = 4; break;
    default:           code = 5; break;
  }
  auto out = std::make_shared<Integer>(code);
  RETURN(out);
}

static PRIMTYPE(type_frexp) {
  TypeVar pair;
  Data::typePair.clone(pair);
  pair[0].unify(Double::typeVar);
  pair[1].unify(Integer::typeVar);
  return args.size() == 1 &&
    args[0]->unify(Double::typeVar) &&
    out->unify(pair);
}

static PRIMFN(prim_frexp) {
  EXPECT(1);
  DOUBLE(arg0, 0);
  int exp;
  double frac = std::frexp(arg0->value, &exp);
  auto out = make_tuple2(
    std::make_shared<Double>(frac),
    std::make_shared<Integer>(exp));
  RETURN(out);
}

static PRIMTYPE(type_ldexp) {
  return args.size() == 2 &&
    args[0]->unify(Double::typeVar) &&
    args[1]->unify(Integer::typeVar) &&
    out->unify(Double::typeVar);
}

static PRIMFN(prim_ldexp) {
  EXPECT(2);
  DOUBLE(arg0, 0);
  INTEGER(arg1, 1);

  if (mpz_cmp_si(arg1->value, -10000) < 0) {
    auto out = std::make_shared<Double>(0.0);
    RETURN(out);
  } else if (mpz_cmp_si(arg1->value, 10000) > 0) {
    auto out = std::make_shared<Double>(arg0->value / 0.0);
    RETURN(out);
  } else {
    auto res = std::ldexp(arg0->value, mpz_get_si(arg1->value));
    auto out = std::make_shared<Double>(res);
    RETURN(out);
  }
}

static PRIMTYPE(type_modf) {
  TypeVar pair;
  Data::typePair.clone(pair);
  pair[0].unify(Integer::typeVar);
  pair[1].unify(Double::typeVar);
  return args.size() == 1 &&
    args[0]->unify(Double::typeVar) &&
    out->unify(pair);
}

static PRIMFN(prim_modf) {
  EXPECT(1);
  DOUBLE(arg0, 0);
  double intpart;
  double frac = std::modf(arg0->value, &intpart);
  auto i = std::make_shared<Integer>();
  mpz_set_d(i->value, arg0->value);
  auto out = make_tuple2(
    std::move(i),
    std::make_shared<Double>(frac));
  RETURN(out);
}

void prim_register_double(PrimMap &pmap) {
  // basic functions
  pmap.emplace("dabs", PrimDesc(prim_abs, type_unop));
  pmap.emplace("dneg", PrimDesc(prim_neg, type_unop));
  pmap.emplace("dadd", PrimDesc(prim_add, type_binop));
  pmap.emplace("dsub", PrimDesc(prim_sub, type_binop));
  pmap.emplace("dmul", PrimDesc(prim_mul, type_binop));
  pmap.emplace("ddiv", PrimDesc(prim_div, type_binop));
  pmap.emplace("dpow", PrimDesc(prim_pow, type_binop));
  pmap.emplace("dfma", PrimDesc(prim_fma, type_fma));
  pmap.emplace("dcmp", PrimDesc(prim_cmp, type_cmp));
  pmap.emplace("dstr", PrimDesc(prim_str, type_str));
  pmap.emplace("ddbl", PrimDesc(prim_dbl, type_dbl));

  // integer/double interop
  pmap.emplace("dclass", PrimDesc(prim_class, type_class));
  pmap.emplace("dfrexp", PrimDesc(prim_frexp, type_frexp));
  pmap.emplace("dldexp", PrimDesc(prim_ldexp, type_ldexp));
  pmap.emplace("dmodf",  PrimDesc(prim_modf,  type_modf));

  // handy numeric functions
  pmap.emplace("dcos",   PrimDesc(prim_cos,   type_unop));
  pmap.emplace("dsin",   PrimDesc(prim_sin,   type_unop));
  pmap.emplace("dtan",   PrimDesc(prim_tan,   type_unop));
  pmap.emplace("dacos",  PrimDesc(prim_acos,  type_unop));
  pmap.emplace("dasin",  PrimDesc(prim_asin,  type_unop));
  pmap.emplace("dexp",   PrimDesc(prim_exp,   type_unop));
  pmap.emplace("dlog",   PrimDesc(prim_log,   type_unop));
  pmap.emplace("dexpm1", PrimDesc(prim_expm1, type_unop));
  pmap.emplace("dlog1p", PrimDesc(prim_log1p, type_unop));
  pmap.emplace("derf",   PrimDesc(prim_erf,   type_unop));
  pmap.emplace("derfc",  PrimDesc(prim_erfc,  type_unop));
  pmap.emplace("dtgamma",PrimDesc(prim_tgamma,type_unop));
  pmap.emplace("dlgamma",PrimDesc(prim_lgamma,type_unop));
  pmap.emplace("datan",  PrimDesc(prim_atan,  type_binop));
}
