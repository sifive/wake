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

#include <stdlib.h>

#include <sstream>

#include "prim.h"
#include "status.h"
#include "types/data.h"
#include "types/type.h"
#include "value.h"

static PRIMTYPE(type_stack) {
  TypeVar list;
  Data::typeList.clone(list);
  list[0].unify(Data::typeString);
  return args.size() == 1 && args[0]->unify(Data::typeUnit) && out->unify(list);
}

static PRIMFN(prim_stack) {
  EXPECT(1);

  size_t need = 0;
  auto list = scope->stack_trace();
  for (auto &x : list) need += String::reserve(x.size());

  need += reserve_list(list.size());
  runtime.heap.reserve(need);

  std::vector<Value *> objs;
  objs.reserve(list.size());
  for (auto &s : list) objs.push_back(String::claim(runtime.heap, s));

  RETURN(claim_list(runtime.heap, objs.size(), objs.data()));
}

static PRIMTYPE(type_panic) {
  return args.size() == 1 && args[0]->unify(Data::typeString);
  (void)out;  // leave prim free
}

static PRIMFN(prim_panic) {
  EXPECT(1);
  STRING(arg0, 0);
  std::stringstream str;
  str << arg0->c_str() << std::endl;
  status_get_generic_stream(STREAM_ERROR) << str.str() << std::endl;
  require_fail("", 1, runtime, scope);
}

static PRIMTYPE(type_id) { return args.size() == 1 && args[0]->unify(*out); }

static PRIMFN(prim_id) {
  EXPECT(1);
  RETURN(args[0]);
}

static PRIMTYPE(type_true) { return args.size() == 1 && out->unify(Data::typeBoolean); }

static PRIMFN(prim_true) {
  EXPECT(1);
  runtime.heap.reserve(reserve_bool());
  RETURN(claim_bool(runtime.heap, true));
}

void prim_register_exception(PrimMap &pmap) {
  // These should not be evaluated in const prop, but can be removed
  prim_register(pmap, "stack", prim_stack, type_stack, PRIM_ORDERED);
  prim_register(pmap, "panic", prim_panic, type_panic, PRIM_IMPURE);
  // We have both panic and unreachable, because "unreachable" is considered to be optimizable away
  prim_register(pmap, "unreachable", prim_panic, type_panic, PRIM_ORDERED);
  prim_register(pmap, "use", prim_id, type_id, PRIM_IMPURE);
  prim_register(pmap, "true", prim_true, type_true, PRIM_PURE);
}
