/*
 * Copyright 2019 SiFive, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You should have received a copy of LICENSE.Apache2 along with
 * this software. If not, you may obtain a copy at
 *
 *    https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "prim.h"
#include "datatype.h"
#include "type.h"
#include "value.h"
#include "heap.h"
#include "expr.h"
#include "type.h"
#include <cassert>

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
  REQUIRE(mpz_cmp_si(arg0->value, 0) >= 0);
  REQUIRE(mpz_cmp_si(arg0->value, 1024*1024*1024) < 0);
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
  DATA(vec, 0);
  INTEGER(arg1, 1);
  REQUIRE(mpz_cmp_si(arg1->value, 0) >= 0);
  REQUIRE(mpz_cmp_si(arg1->value, vec->binding->nargs) < 0);
  vec->binding->future[mpz_get_si(arg1->value)].depend(queue, std::move(completion));
}

static PRIMTYPE(type_vset) {
  TypeVar vec;
  vectorT.clone(vec);
  return args.size() == 3 &&
    args[0]->unify(vec) &&
    args[1]->unify(Integer::typeVar) &&
    args[2]->unify(vec[0]) &&
    out->unify(Data::typeUnit);
}

static PRIMFN(prim_vset) {
  EXPECT(3);
  DATA(vec, 0);
  INTEGER(arg1, 1);

  // Getting this wrong means vector.wake is buggy and the heap will crash
  assert(mpz_cmp_si(arg1->value, 0) >= 0);
  assert(mpz_cmp_si(arg1->value, vec->binding->nargs) < 0);
  assert(!vec->binding->future[mpz_get_si(arg1->value)].value);

  Receiver::receive(
    queue,
    Binding::make_completer(vec->binding, mpz_get_si(arg1->value)),
    std::move(args[2]));

  auto out = make_unit();
  RETURN(out);
}

void prim_register_vector(PrimMap &pmap) {
  // We cannot safely reorder vget, so it is not PURE
  prim_register(pmap, "vnew", prim_vnew, type_vnew, PRIM_SHALLOW);
  prim_register(pmap, "vget", prim_vget, type_vget, PRIM_SHALLOW);
  prim_register(pmap, "vset", prim_vset, type_vset, PRIM_SHALLOW);
}
