#include "prim.h"
#include "datatype.h"
#include "type.h"
#include "value.h"
#include "heap.h"
#include "expr.h"
#include "type.h"

static Constructor vectorC(AST(LOCATION, "Array"));
static const TypeVar vectorT("Array", 1);

static PRIMTYPE(type_vnew) {
  TypeVar vec;
  vectorT.clone(vec);
  return args.size() == 1 &&
    args[0]->unify(Integer::typeVar) &&
    out->unify(vec);
}

static PRIMFN(prim_vnew) {
  EXPECT(1);
  INTEGER(arg0, 0);
  REQUIRE(mpz_cmp_si(arg0->value, 0) >= 0, "vnew too small (< 0)");
  REQUIRE(mpz_cmp_si(arg0->value, 1024*1024*1024) < 0, "vnew too large (> 1G)");
  auto out = std::make_shared<Data>(&vectorC,
    std::make_shared<Binding>(nullptr, nullptr, nullptr, mpz_get_si(arg0->value)));
  RETURN(out);
}

static PRIMTYPE(type_vget) {
  TypeVar vec;
  vectorT.clone(vec);
  return args.size() == 2 &&
    args[0]->unify(vec) &&
    args[1]->unify(Integer::typeVar) &&
    out->unify(vec[0]);
}

static PRIMFN(prim_vget) {
  EXPECT(2);
  INTEGER(arg1, 1);
  Data *vec = reinterpret_cast<Data*>(args[0].get());
  REQUIRE(mpz_cmp_si(arg1->value, 0) >= 0, "vget too small (< 0)");
  REQUIRE(mpz_cmp_si(arg1->value, vec->binding->nargs) < 0, "vget too large");
  vec->binding->future[mpz_get_si(arg1->value)].depend(queue, std::move(completion));
}

static PRIMTYPE(type_vset) {
  TypeVar vec;
  vectorT.clone(vec);
  return args.size() == 3 &&
    args[0]->unify(vec) &&
    args[1]->unify(Integer::typeVar) &&
    args[2]->unify(vec[0]) &&
    out->unify(Data::typeBoolean);
}

static PRIMFN(prim_vset) {
//  EXPECT(3); allow Exceptions to be written into vectors
  (void)data;
  INTEGER(arg1, 1);
  Data *vec = reinterpret_cast<Data*>(args[0].get());
  REQUIRE(mpz_cmp_si(arg1->value, 0) >= 0, "vset too small (< 0)");
  REQUIRE(mpz_cmp_si(arg1->value, vec->binding->nargs) < 0, "vset too large");
  Receiver::receive(
    queue,
    Binding::make_completer(vec->binding, mpz_get_si(arg1->value)),
    std::move(args[2]));
  auto out = make_true();
  RETURN(out);
}

void prim_register_vector(PrimMap &pmap) {
  pmap.emplace("vnew", PrimDesc(prim_vnew, type_vnew));
  pmap.emplace("vget", PrimDesc(prim_vget, type_vget, 0, PRIM_SHALLOW|PRIM_PURE));
  pmap.emplace("vset", PrimDesc(prim_vset, type_vset, 0, PRIM_SHALLOW));
}
