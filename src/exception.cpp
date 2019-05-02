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
#include <sstream>

static PRIMTYPE(type_test) {
  return args.size() == 1 &&
    // leave arg0 free
    out->unify(Data::typeBoolean);
}

static PRIMFN(prim_test) {
  (void)data; // silence unused variable warning (EXPECT not called)
  REQUIRE(args.size() == 1, "prim_test not called on 1 argument");
  auto out = make_bool(args[0]->type == &Exception::type);
  RETURN(out);
}

static PRIMTYPE(type_catch) {
  TypeVar list;
  Data::typeList.clone(list);
  list[0].unify(String::typeVar);
  return args.size() == 1 &&
    // leave arg0 free
    out->unify(list);
}

static PRIMFN(prim_catch) {
  (void)data; // silence unused variable warning (EXPECT not called)
  REQUIRE(args.size() == 1, "prim_catch not called on 1 argument");
  REQUIRE(args[0]->type == &Exception::type, "prim_catch not called on an Exception");

  Exception *exception = reinterpret_cast<Exception*>(args[0].get());

  std::vector<std::shared_ptr<Value> > v;
  for (auto &i : exception->causes)
    v.emplace_back(std::make_shared<String>(i->reason));
  auto out = make_list(std::move(v));

  RETURN(out);
}

static PRIMTYPE(type_raise) {
  return args.size() == 1 &&
    args[0]->unify(String::typeVar);
  (void)out; // leave prim free
}

static PRIMFN(prim_raise) {
  EXPECT(1);
  STRING(arg0, 0);
  auto out = std::make_shared<Exception>(arg0->value, binding);
  RETURN(out);
}

static PRIMTYPE(type_cast) {
  return args.size() == 1;
  // leave arg0 free
  (void)out; // leave prim free
}

static PRIMFN(prim_cast) {
  EXPECT(1); // re-raise the exception argument
  RAISE("Attempt to cast a non-exception");
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
  prim_register(pmap, "test",  prim_test,  type_test,  PRIM_PURE|PRIM_SHALLOW);
  prim_register(pmap, "catch", prim_catch, type_catch, PRIM_PURE|PRIM_SHALLOW);
  prim_register(pmap, "raise", prim_raise, type_raise, PRIM_PURE|PRIM_SHALLOW);
  prim_register(pmap, "cast",  prim_cast,  type_cast,  PRIM_PURE|PRIM_SHALLOW);

  prim_register(pmap, "wait_one", prim_unit, type_unit, PRIM_PURE|PRIM_SHALLOW);
  prim_register(pmap, "wait_all", prim_unit, type_unit, PRIM_PURE);
}
