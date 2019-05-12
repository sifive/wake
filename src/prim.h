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

#ifndef PRIM_H
#define PRIM_H

#include "type.h"
#include "primfn.h"
#include <map>
#include <string>
#include <vector>
#include <memory>
#include <assert.h>

struct Receiver;
struct Value;
struct String;
struct Integer;
struct Double;
struct RegExp;
struct Data;

/* Macros for handling inputs from wake */
#define RETURN(val) do {						\
  Receiver::receive(queue, std::move(completion), std::move(val));	\
  return;								\
} while (0)

#define REQUIRE(b) assert(b)
#define EXPECT(num) do {	\
  (void)data;			\
  REQUIRE(args.size() == num);	\
} while (0)

#define STRING(arg, i)  REQUIRE(args[i]->type == &String::type);  String  *arg = reinterpret_cast<String *>(args[i].get());
#define INTEGER(arg, i) REQUIRE(args[i]->type == &Integer::type); Integer *arg = reinterpret_cast<Integer*>(args[i].get());
#define DOUBLE(arg, i)  REQUIRE(args[i]->type == &Double::type);  Double  *arg = reinterpret_cast<Double *>(args[i].get());
#define REGEXP(arg, i)	REQUIRE(args[i]->type == &RegExp::type);  RegExp  *arg = reinterpret_cast<RegExp *>(args[i].get());
#define DATA(arg, i)    REQUIRE(args[i]->type == &Data::type);    Data    *arg = reinterpret_cast<Data   *>(args[i].get());

/* Useful expressions for primitives */
std::shared_ptr<Value> make_unit();
std::shared_ptr<Value> make_bool(bool x);
std::shared_ptr<Value> make_order(int x);
std::shared_ptr<Value> make_tuple2(std::shared_ptr<Value> &&first, std::shared_ptr<Value> &&second);
std::shared_ptr<Value> make_list(std::vector<std::shared_ptr<Value> > &&values);
std::shared_ptr<Value> make_result(bool ok, std::shared_ptr<Value> &&value);

#define PRIM_PURE	1	// has no side-effects (can be duplicated / removed)
#define PRIM_SHALLOW	2	// only wait for direct arguments (not children)

/* Register primitive functions */
struct PrimDesc {
  PrimFn   fn;
  PrimType type;
  int      flags;
  void    *data;

  PrimDesc(PrimFn fn_, PrimType type_, int flags_, void *data_ = 0)
   : fn(fn_), type(type_), flags(flags_), data(data_) { }
};

typedef std::map<std::string, PrimDesc> PrimMap;
struct JobTable;

void prim_register(PrimMap &pmap, const char *key, PrimFn fn, PrimType type, int flags, void *data = 0);
void prim_register_string(PrimMap &pmap, const char *version);
void prim_register_vector(PrimMap &pmap);
void prim_register_integer(PrimMap &pmap);
void prim_register_double(PrimMap &pmap);
void prim_register_exception(PrimMap &pmap);
void prim_register_regexp(PrimMap &pmap);
void prim_register_json(PrimMap &pmap);
void prim_register_job(JobTable *jobtable, PrimMap &pmap);
void prim_register_sources(std::vector<std::shared_ptr<String> > *sources, PrimMap &pmap);

#endif
