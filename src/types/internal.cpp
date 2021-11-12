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

// Open Group Base Specifications Issue 7
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include "type.h"
#include "data.h"
#include "primfn.h"
#include "internal.h"

PRIMTYPE(type_rcat) {
  bool ok = out->unify(Data::typeRegExp);
  for (size_t i = 0; i < args.size(); ++i) {
    if (i % 2 == 0) {
      ok &= args[i]->unify(Data::typeString);
    } else {
      ok &= args[i]->unify(Data::typeRegExp);
    }
  }
  return ok;
}

PRIMTYPE(type_vcat) {
  bool ok = out->unify(Data::typeString);
  for (auto x : args) ok &= x->unify(Data::typeString);
  return ok;
}

PRIMTYPE(type_scmp) {
  return args.size() == 2 &&
    args[0]->unify(Data::typeString) &&
    args[1]->unify(Data::typeString) &&
    out->unify(Data::typeOrder);
}

PRIMTYPE(type_icmp) {
  return args.size() == 2 &&
    args[0]->unify(Data::typeInteger) &&
    args[1]->unify(Data::typeInteger) &&
    out->unify(Data::typeOrder);
}

PRIMTYPE(type_cmp_nan_lt) {
  return args.size() == 2 &&
    args[0]->unify(Data::typeDouble) &&
    args[1]->unify(Data::typeDouble) &&
    out->unify(Data::typeOrder);
}

PRIMTYPE(type_tget) {
  return args.size() >= 2 &&
    args[0]->unify(Data::typeTarget) &&
    args[1]->unify(TypeVar(FN, 2)) &&
    (*args[1])[0].unify(Data::typeTarget) &&
    out->unify((*args[1])[1]);
}

void prim_register(PrimMap &pmap, const char *key, PrimFn fn, PrimType type, int flags, void *data) {
  pmap.insert(std::make_pair(key, PrimDesc(fn, type, flags, data)));
}

PrimMap prim_register_internal() {
  PrimMap pmap;
  prim_register(pmap, "rcat",        nullptr, type_rcat,       PRIM_PURE);
  prim_register(pmap, "vcat",        nullptr, type_vcat,       PRIM_PURE);
  prim_register(pmap, "scmp",        nullptr, type_scmp,       PRIM_PURE);
  prim_register(pmap, "icmp",        nullptr, type_icmp,       PRIM_PURE);
  prim_register(pmap, "dcmp_nan_lt", nullptr, type_cmp_nan_lt, PRIM_PURE);
  prim_register(pmap, "tget",        nullptr, type_tget,       PRIM_FNARG); // kind depends on function argument
  return pmap;
}
