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

#include <sstream>
#include <unordered_map>

#include "prim.h"
#include "datatype.h"
#include "type.h"
#include "value.h"
#include "tuple.h"
#include "type.h"
#include "status.h"
#include "expr.h"

struct TargetValue {
  Hash subhash;
  Promise promise;

  TargetValue() { }
  TargetValue(const Hash &subhash_) : subhash(subhash_) { }
};

struct HashHasher {
  size_t operator()(const Hash &h) const { return h.data[0]; }
};

struct Target final : public GCObject<Target, DestroyableObject> {
  static bool report_future_targets;

  typedef GCObject<Target, DestroyableObject> Parent;

  HeapPointer<String> location;
  std::unordered_map<Hash, TargetValue, HashHasher> table;

  static TypeVar typeVar;
  Target(Heap &h, String *location_) : Parent(h), location(location_) { }
  Target(Target &&target) = default;
  ~Target();

  template <typename T, T (HeapPointerBase::*memberfn)(T x)>
  T recurse(T arg);

  void format(std::ostream &os, FormatState &state) const override;
  Hash shallow_hash() const override;
};

bool Target::report_future_targets = true;

void dont_report_future_targets() {
  Target::report_future_targets = false;
}

// This type is NOT exported; it should be invisible to users
TypeVar Target::typeVar("Target@builtin", 0);

template <typename T, T (HeapPointerBase::*memberfn)(T x)>
T Target::recurse(T arg) {
  arg = Parent::recurse<T, memberfn>(arg);
  arg = (location.*memberfn)(arg);
  for (auto &x : table)
    arg = x.second.promise.recurse<T, memberfn>(arg);
  return arg;
}

template <>
HeapStep Target::recurse<HeapStep, &HeapPointerBase::explore>(HeapStep step) {
  // For reproducible execution, pretend a target is always empty
  return step;
}

Target::~Target() {
  if (report_future_targets) for (auto &x : table) {
    if (!x.second.promise) {
      std::stringstream ss;
      ss << "Infinite recursion detected across " << location->c_str() << std::endl;
      status_write(STREAM_ERROR, ss.str());
      break;
    }
  }
}

void Target::format(std::ostream &os, FormatState &state) const {
  os << "Target";
}

Hash Target::shallow_hash() const {
  // For reproducible execution, pretend a target is always empty
  return Hash() ^ TYPE_TARGET;
}

#define TARGET(arg, i) do { HeapObject *arg = args[i]; REQUIRE(typeid(*arg) == typeid(Target)); } while(0); Target *arg = static_cast<Target*>(args[i]);

static PRIMTYPE(type_hash) {
  return out->unify(Integer::typeVar);
}

static PRIMFN(prim_hash) {
  runtime.heap.reserve(Tuple::fulfiller_pads + reserve_list(nargs) + reserve_hash());
  Continuation *continuation = scope->claim_fulfiller(runtime, output);
  Value *list = claim_list(runtime.heap, nargs, args);
  runtime.schedule(claim_hash(runtime.heap, list, continuation));
}

static PRIMTYPE(type_tnew) {
  return args.size() == 1 &&
    args[0]->unify(String::typeVar) &&
    out->unify(Target::typeVar);
}

static PRIMFN(prim_tnew) {
  EXPECT(1);
  STRING(location, 0);
  RETURN(Target::alloc(runtime.heap, runtime.heap, location));
}

static PRIMTYPE(type_tget) {
  return args.size() == 4 &&
    args[0]->unify(Target::typeVar) &&
    args[1]->unify(Integer::typeVar) &&
    args[2]->unify(Integer::typeVar) &&
    args[3]->unify(TypeVar(FN, 2)) &&
    (*args[3])[0].unify(Integer::typeVar) &&
    out->unify((*args[3])[1]);
}

struct CTarget final : public GCObject<CTarget, Continuation> {
  HeapPointer<Target> target;
  Hash hash;

  CTarget(Target *target_, Hash hash_)
   : target(target_), hash(hash_) { }

  template <typename T, T (HeapPointerBase::*memberfn)(T x)>
  T recurse(T arg) {
    arg = Continuation::recurse<T, memberfn>(arg);
    arg = (target.*memberfn)(arg);
    return arg;
  }

  void execute(Runtime &runtime) override;
};

void CTarget::execute(Runtime &runtime) {
  target->table[hash].promise.fulfill(runtime, value.get());
}

static PRIMFN(prim_tget) {
  EXPECT(4);
  TARGET(target, 0);
  INTEGER_MPZ(key, 1);
  INTEGER_MPZ(subkey, 2);
  CLOSURE(body, 3);

  runtime.heap.reserve(Tuple::fulfiller_pads + Runtime::reserve_apply(body->fun) + CTarget::reserve());
  Continuation *continuation = scope->claim_fulfiller(runtime, output);

  Hash hash;
  REQUIRE(mpz_sizeinbase(key, 2) <= 8*sizeof(hash.data));
  mpz_export(&hash.data[0], 0, 1, sizeof(hash.data[0]), 0, 0, key);

  Hash subhash;
  REQUIRE(mpz_sizeinbase(subkey, 2) <= 8*sizeof(subhash.data));
  mpz_export(&subhash.data[0], 0, 1, sizeof(subhash.data[0]), 0, 0, subkey);

  auto ref = target->table.insert(std::make_pair(hash, TargetValue(subhash)));
  ref.first->second.promise.await(runtime, continuation);

  if (!(ref.first->second.subhash == subhash)) {
    std::stringstream ss;
    ss << "ERROR: Target subkey mismatch for " << target->location->c_str() << std::endl;
    for (auto &x : scope->stack_trace())
      ss << "  from " << x << std::endl;
    ss << "To debug, rerun your wake command with these additional options:" << std::endl;
    ss << "  --debug-target=" << hash.data[0] << " to see the unique target arguments (before the '\\')" << std::endl;
    ss << "  --debug-target=" << ref.first->second.subhash.data[0] << " to see the first invocation's extra arguments" << std::endl;
    ss << "  --debug-target=" << subhash.data[0] << " to see the second invocation's extra arguments" << std::endl;
    status_write(STREAM_ERROR, ss.str());
    runtime.abort = true;
  }

  if (ref.second)
    runtime.claim_apply(body, args[1], CTarget::claim(runtime.heap, target, hash), scope);
}

void prim_register_target(PrimMap &pmap) {
  prim_register(pmap, "hash", prim_hash, type_hash, PRIM_PURE);
  prim_register(pmap, "tnew", prim_tnew, type_tnew, PRIM_ORDERED);
  prim_register(pmap, "tget", prim_tget, type_tget, PRIM_FNARG); // kind depends on function argument
}
