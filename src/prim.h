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

#include "primfn.h"
#include "tuple.h"
#include <map>
#include <string>
#include <vector>

struct String;
struct Integer;
struct Double;
struct RegExp;
struct Tuple;

/* Macros for handling inputs from wake */
#define RETURN(val) do {						\
  continuation->resume(runtime, val);					\
  return;								\
} while (0)

void require_fail(const char *message, unsigned size, Runtime &runtime, const Tuple *scope);

#define STR(x) #x
#define STR2(x) STR(x)
#define REQUIRE(b) do {							\
  if (!(b)) {								\
    const char message[] =						\
      "Requirement " STR(b) " failed at " 				\
      __FILE__ ":" STR2(__LINE__) "\n";					\
    require_fail(message, sizeof(message), runtime, scope);		\
    return;								\
  }									\
} while (0)

#define EXPECT(num) do {	\
  (void)data;			\
  REQUIRE(nargs == num);	\
} while (0)

#define STRING(arg, i)  do { HeapObject *arg = args[i]; REQUIRE(typeid(*arg) == typeid(String));  } while(0); String  *arg = static_cast<String *>(args[i]);
#define INTEGER(arg, i) do { HeapObject *arg = args[i]; REQUIRE(typeid(*arg) == typeid(Integer)); } while(0); Integer *arg = static_cast<Integer*>(args[i]);
#define DOUBLE(arg, i)  do { HeapObject *arg = args[i]; REQUIRE(typeid(*arg) == typeid(Double));  } while(0); Double  *arg = static_cast<Double *>(args[i]);
#define REGEXP(arg, i)	do { HeapObject *arg = args[i]; REQUIRE(typeid(*arg) == typeid(RegExp));  } while(0); RegExp  *arg = static_cast<RegExp *>(args[i]);
#define TUPLE(arg, i)   do { HeapObject *arg = args[i]; REQUIRE(typeid(*arg) == typeid(Tuple));   } while(0); Tuple   *arg = static_cast<Tuple  *>(args[i]);
#define CLOSURE(arg, i) do { HeapObject *arg = args[i]; REQUIRE(typeid(*arg) == typeid(Closure)); } while(0); Closure *arg = static_cast<Closure*>(args[i]);

#define INTEGER_MPZ(arg, i) do { HeapObject *arg = args[i]; REQUIRE(typeid(*arg) == typeid(Integer)); } while(0); mpz_t arg = { static_cast<Integer*>(args[i])->wrap() };

/* Useful expressions for primitives */
HeapObject *alloc_order(Heap &h, int x);
HeapObject *alloc_nil(Heap &h);
inline size_t reserve_unit() { return Tuple::reserve(0); }
inline size_t reserve_bool() { return Tuple::reserve(0); }
inline size_t reserve_tuple2() { return Tuple::reserve(2); }
inline size_t reserve_result() { return Tuple::reserve(1); }
inline size_t reserve_list(size_t elements) { return Tuple::reserve(2) * elements + Tuple::reserve(0); }
HeapObject *claim_unit(Heap &h);
HeapObject *claim_bool(Heap &h, bool x);
HeapObject *claim_tuple2(Heap &h, HeapObject *first, HeapObject *second);
HeapObject *claim_result(Heap &h, bool ok, HeapObject *value);
HeapObject *claim_list(Heap &h, size_t elements, HeapObject** values);

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

struct StringInfo {
  bool verbose;
  bool debug;
  bool quiet;
  const char *version;
  StringInfo(bool v, bool d, bool q, const char *x) : verbose(v), debug(d), quiet(q), version(x) { }
};

void prim_register(PrimMap &pmap, const char *key, PrimFn fn, PrimType type, int flags, void *data = 0);
void prim_register_string(PrimMap &pmap, StringInfo *info);
void prim_register_vector(PrimMap &pmap);
void prim_register_integer(PrimMap &pmap);
void prim_register_double(PrimMap &pmap);
void prim_register_exception(PrimMap &pmap);
void prim_register_regexp(PrimMap &pmap);
void prim_register_target(PrimMap &pmap);
void prim_register_json(PrimMap &pmap);
void prim_register_job(JobTable *jobtable, PrimMap &pmap);
void prim_register_sources(RootPointer<Tuple> *sources, PrimMap &pmap);

#endif
