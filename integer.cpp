#include "prim.h"
#include "value.h"
#include <iostream>
#include <gmpxx.h>

#define BINOP(name, op)										\
static void prim_##name(void *data, const std::vector<Value*> &args, Action *completion) {	\
  EXPECT_ARGS(2);										\
  Integer *arg0 = GET_INTEGER(0);								\
  Integer *arg1 = GET_INTEGER(1);								\
  resume(completion, new Integer(arg0->value op arg1->value));					\
}

BINOP(add, +)
BINOP(sub, -)
BINOP(mul, *)
BINOP(div, /)
BINOP(mod, %)
BINOP(xor, ^)
BINOP(and, &)
BINOP(or,  |)

#define BINFN(name)										\
static void prim_##name(void *data, const std::vector<Value*> &args, Action *completion) {	\
  EXPECT_ARGS(2);										\
  Integer *arg0 = GET_INTEGER(0);								\
  Integer *arg1 = GET_INTEGER(1);								\
  resume(completion, new Integer(name(arg0->value, arg1->value)));				\
}

BINFN(gcd)
BINFN(lcm)

#define BINOP_SI(name, op)									\
static void prim_##name(void *data, const std::vector<Value*> &args, Action *completion) {	\
  EXPECT_ARGS(2);										\
  Integer *arg0 = GET_INTEGER(0);								\
  Integer *arg1 = GET_INTEGER(1);								\
  if (sgn(arg1->value) < 0) {									\
    std::cerr << "prim_shl called with argument 2 = "						\
      << arg1 << ", which is not a positive integer." << std::endl;				\
    stack_trace(completion);									\
    exit(1);											\
  }												\
  if (!arg1->value.fits_slong_p() || (arg1->value.get_si() >> 20) != 0) {			\
    std::cerr << "prim_shl called with argument 2 = "						\
      << arg1 << ", which is unreasonably large." << std::endl;					\
    stack_trace(completion);									\
    exit(1);											\
  }												\
  resume(completion, new Integer(arg0->value op arg1->value.get_si()));				\
}

BINOP_SI(shl, <<)
BINOP_SI(shr, >>)

void prim_register_integer(PrimMap& pmap) {
  pmap["add"].first = prim_add;
  pmap["sub"].first = prim_sub;
  pmap["mul"].first = prim_mul;
  pmap["div"].first = prim_div;
  pmap["mod"].first = prim_mod;
  pmap["shl"].first = prim_shl;
  pmap["shr"].first = prim_shr;
  pmap["xor"].first = prim_xor;
  pmap["and"].first = prim_and;
  pmap["or" ].first = prim_or;
  pmap["gcd"].first = prim_gcd;
  pmap["lcm"].first = prim_lcm;
}
