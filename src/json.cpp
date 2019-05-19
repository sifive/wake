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

#include "json5.h"
#include "prim.h"
#include "expr.h"
#include "value.h"
#include "parser.h"
#include <limits>
#include <sstream>
#include <assert.h>

typedef std::numeric_limits<double> dlimits;
static double nan() { return dlimits::quiet_NaN(); }
static double inf(char c) { return c == '+' ? dlimits::infinity() : -dlimits::infinity(); }

static std::unique_ptr<Lambda> eJValue(new Lambda(LOCATION, "_", nullptr));

static std::shared_ptr<Value> getJValue(std::shared_ptr<Value> &&value, int member) {
  auto bind = std::make_shared<Binding>(nullptr, nullptr, eJValue.get(), 1);
  bind->future[0].value = value;
  bind->state = 1;
  return std::make_shared<Data>(&JValue->members[member], std::move(bind));
}

static std::shared_ptr<Value> consume_jast(JAST &&jast) {
  switch (jast.kind) {
    case JSON_NULLVAL:  return std::make_shared<Data>(&JValue->members[4], nullptr);
    case JSON_TRUE:     return getJValue(make_bool(true), 3);
    case JSON_FALSE:    return getJValue(make_bool(false), 3);
    case JSON_INTEGER:  return getJValue(std::make_shared<Integer>(jast.value.c_str()), 1);
    case JSON_DOUBLE:   return getJValue(std::make_shared<Double>(jast.value.c_str()), 2);
    case JSON_INFINITY: return getJValue(std::make_shared<Double>(inf(jast.value[0])), 2);
    case JSON_NAN:      return getJValue(std::make_shared<Double>(nan()), 2);
    case JSON_STR:      return getJValue(std::make_shared<String>(std::move(jast.value)), 0);
    case JSON_OBJECT: {
      std::vector<std::shared_ptr<Value> > values;
      values.reserve(jast.children.size());
      for (auto &c : jast.children)
        values.emplace_back(make_tuple2(
          std::make_shared<String>(std::move(c.first)),
          consume_jast(std::move(c.second))));
      jast.children.clear();
      return getJValue(make_list(std::move(values)), 5);
    }
    case JSON_ARRAY: {
      std::vector<std::shared_ptr<Value> > values;
      values.reserve(jast.children.size());
      for (auto &c : jast.children)
        values.emplace_back(consume_jast(std::move(c.second)));
      jast.children.clear();
      return getJValue(make_list(std::move(values)), 6);
    }
    default: {
      assert(0);
      return nullptr;
    }
  }
}

static PRIMTYPE(type_json) {
  TypeVar result;
  Data::typeResult.clone(result);
  result[0].unify(Data::typeJValue);
  result[1].unify(String::typeVar);
  return args.size() == 1 &&
    args[0]->unify(String::typeVar) &&
    out->unify(result);
}

static PRIMFN(prim_json_file) {
  EXPECT(1);
  STRING(file, 0);
  std::stringstream errs;
  JAST jast;
  if (JAST::parse(file->value.c_str(), errs, jast)) {
    auto out = make_result(true, consume_jast(std::move(jast)));
    RETURN(out);
  } else {
    auto out = make_result(false, std::make_shared<String>(errs.str()));
    RETURN(out);
  }
}

static PRIMFN(prim_json_body) {
  EXPECT(1);
  STRING(body, 0);
  std::stringstream errs;
  JAST jast;
  if (JAST::parse(body->value, errs, jast)) {
    auto out = make_result(true, consume_jast(std::move(jast)));
    RETURN(out);
  } else {
    auto out = make_result(false, std::make_shared<String>(errs.str()));
    RETURN(out);
  }
}

static PRIMTYPE(type_jstr) {
  return args.size() == 1 &&
    args[0]->unify(String::typeVar) &&
    out->unify(String::typeVar);
}

static PRIMFN(prim_json_str) {
  EXPECT(1);
  STRING(str, 0);
  auto out = std::make_shared<String>(json_escape(str->value));
  RETURN(out);
}

void prim_register_json(PrimMap &pmap) {
  prim_register(pmap, "json_file", prim_json_file, type_json, PRIM_SHALLOW);
  prim_register(pmap, "json_body", prim_json_body, type_json, PRIM_SHALLOW|PRIM_PURE);
  prim_register(pmap, "json_str",  prim_json_str,  type_jstr, PRIM_SHALLOW|PRIM_PURE);
}
