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
#include "tuple.h"
#include "type.h"
#include "status.h"
#include "expr.h"
#include <sstream>
#include <unordered_map>

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
  typedef GCObject<Target, DestroyableObject> Parent;

  Location location;
  std::unordered_map<Hash, TargetValue, HashHasher> table;

  static TypeVar typeVar;
  Target(Heap &h, const Location &location_) : Parent(h), location(location_) { }
  Target(Target &&target) = default;
  ~Target();

  PadObject *recurse(PadObject *free);
  void format(std::ostream &os, FormatState &state) const override;
};

TypeVar Target::typeVar("_Target", 0);

PadObject *Target::recurse(PadObject *free) {
  free = Parent::recurse(free);
  for (auto &x : table)
    free = x.second.promise.moveto(free);
  return free;
}

Target::~Target() {
  for (auto &x : table) {
    if (!x.second.promise) {
      std::stringstream ss;
      ss << "Infinite recursion detected across " << location.text() << std::endl;
      auto str = ss.str();
      status_write(2, str.data(), str.size());
      break;
    }
  }
}

void Target::format(std::ostream &os, FormatState &state) const {
  os << "_Target";
}

#define TARGET(arg, i) do { HeapObject *arg = args[i]; REQUIRE(typeid(*arg) == typeid(Target)); } while(0); Target *arg = static_cast<Target*>(args[i]);

static PRIMTYPE(type_hash) {
  return out->unify(Integer::typeVar);
}

static PRIMFN(prim_hash) {
  // !!! FIX
  std::vector<uint64_t> codes;
  for (; scope; scope = scope->at(0)->coerce<Tuple>())
    Hash(scope->at(1)->coerce<HeapObject>()->to_str()).push(codes); // !!! coerce is bad
  Hash h(codes);
  MPZ out;
  mpz_import(out.value, sizeof(h.data)/sizeof(h.data[0]), 1, sizeof(h.data[0]), 0, 0, &h.data[0]);
  RETURN(Integer::alloc(runtime.heap, out));
}

static PRIMTYPE(type_tnew) {
  return args.size() == 1 &&
    out->unify(Target::typeVar);
}

static PRIMFN(prim_tnew) {
  EXPECT(1);
  RETURN(Target::alloc(runtime.heap, runtime.heap, static_cast<Expr*>(scope->meta)->location));
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

  PadObject *recurse(PadObject *free) {
    free = Continuation::recurse(free);
    free = target.moveto(free);
    return free;
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

  runtime.heap.reserve(Tuple::reserve(2) + Runtime::reserve_eval() + CTarget::reserve());

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
    ss << "ERROR: Target subkey mismatch for " << target->location.text() << std::endl;
    if (runtime.stack_trace)
      for (auto &x : scope->stack_trace())
        ss << "  from " << x.file() << std::endl;
    std::string str = ss.str();
    status_write(2, str.data(), str.size());
    runtime.abort = true;
  }

  if (ref.second) {
    Tuple *bind = Tuple::claim(runtime.heap, body->lambda, 2);
    bind->at(0)->instant_fulfill(body->scope.get());
    bind->at(1)->instant_fulfill(args[1]); // hash
    runtime.claim_eval(body->lambda->body.get(), bind, CTarget::claim(runtime.heap, target, hash));
  }
}

void prim_register_target(PrimMap &pmap) {
  prim_register(pmap, "hash", prim_hash, type_hash, PRIM_PURE);
  prim_register(pmap, "tnew", prim_tnew, type_tnew, PRIM_SHALLOW);
  prim_register(pmap, "tget", prim_tget, type_tget, PRIM_SHALLOW);
}
