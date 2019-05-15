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
#include "status.h"
#include "location.h"
#include <stdlib.h>
#include <sstream>

static PRIMTYPE(type_stack) {
  TypeVar list;
  Data::typeList.clone(list);
  list[0].unify(String::typeVar);
  return args.size() == 1 &&
    args[0]->unify(Data::typeUnit) &&
    out->unify(list);
}

static PRIMFN(prim_stack) {
  EXPECT(1);
  std::vector<std::shared_ptr<Value> > list;

  if (queue.stack_trace) for (auto &x : binding->stack_trace()) {
    std::stringstream str;
    str << x.file();
    list.emplace_back(std::make_shared<String>(str.str()));
  }

  auto out = make_list(std::move(list));
  RETURN(out);
}

static PRIMTYPE(type_panic) {
  return args.size() == 1 &&
    args[0]->unify(String::typeVar);
  (void)out; // leave prim free
}

static PRIMFN(prim_panic) {
  EXPECT(1);
  STRING(arg0, 0);
  std::stringstream str;
  str << "PANIC: " << arg0->value << std::endl;
  std::string message = str.str();
  status_write(2, message.data(), message.size());
  bool panic_called = true;
  REQUIRE(!panic_called);
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
  prim_register(pmap, "stack",    prim_stack, type_stack, PRIM_PURE|PRIM_SHALLOW);
  prim_register(pmap, "panic",    prim_panic, type_panic, PRIM_PURE|PRIM_SHALLOW);
  prim_register(pmap, "wait_one", prim_unit,  type_unit,  PRIM_PURE|PRIM_SHALLOW);
  prim_register(pmap, "wait_all", prim_unit,  type_unit,  PRIM_PURE);
}
