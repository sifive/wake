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
#include "value.h"
#include "heap.h"
#include "type.h"
#include <stdlib.h>
#include <iostream>

static PRIMTYPE(type_panic) {
  return args.size() == 1 &&
    args[0]->unify(String::typeVar);
  (void)out; // leave prim free
}

static PRIMFN(prim_panic) {
  EXPECT(1);
  STRING(arg0, 0);
  std::cerr << "PANIC: " << arg0->value << std::endl;
  exit(1); // !!! exit more cleanly than this
}

static PRIMTYPE(type_unit) {
  return args.size() == 1 &&
    out->unify(Data::typeUnit);
}

static PRIMFN(prim_unit) {
  (void)data; // silence unused variable warning (EXPECT not called)
  (void)binding;
  (void)args;
  auto out = make_unit();
  RETURN(out);
}

void prim_register_exception(PrimMap &pmap) {
  prim_register(pmap, "panic",    prim_panic, type_panic, PRIM_PURE|PRIM_SHALLOW);
  prim_register(pmap, "wait_one", prim_unit,  type_unit,  PRIM_PURE|PRIM_SHALLOW);
  prim_register(pmap, "wait_all", prim_unit,  type_unit,  PRIM_PURE);
}
