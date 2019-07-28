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
#include <bitset>

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

  template <typename T, T (HeapPointerBase::*memberfn)(T x)>
  T recurse(T arg);

  template <>
  HeapStep recurse<HeapStep, &HeapPointerBase::explore>(HeapStep step);

  void format(std::ostream &os, FormatState &state) const override;
  Hash hash() const override;
};

TypeVar Target::typeVar("_Target", 0);

template <typename T, T (HeapPointerBase::*memberfn)(T x)>
T Target::recurse(T arg) {
  arg = Parent::recurse<T, memberfn>(arg);
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

Hash Target::hash() const {
  // For reproducible execution, pretend a target is always empty
  return Hash();
}

#define TARGET(arg, i) do { HeapObject *arg = args[i]; REQUIRE(typeid(*arg) == typeid(Target)); } while(0); Target *arg = static_cast<Target*>(args[i]);

struct HeapHash {
  Hash code;
  Promise *broken;
};

static HeapHash deep_hash(Runtime &runtime, HeapObject *obj) {
  std::unordered_map<uintptr_t, std::bitset<256> > explored;
  size_t max_objs = runtime.heap.used() / sizeof(PadObject);
  std::unique_ptr<HeapObject*[]> scratch(new HeapObject*[max_objs]);

  HeapStep step;
  scratch[0] = obj;
  step.found = &scratch[1];
  step.broken = nullptr;

  Hash code;
  for (HeapObject **done = scratch.get(); done != step.found; ++done) {
    HeapObject *head = *done;

    // Ensure we visit each object only once
    uintptr_t key = reinterpret_cast<uintptr_t>(static_cast<void*>(head));
    auto flag = explored[key>>8][key&0xFF];
    if (flag) continue;
    flag = true;

    // Hash this object and enqueue its children for hashing
    step = head->explore(step);
    code = code + head->hash();
  }

  HeapHash out;
  out.code = code;
  out.broken = step.broken;
  return out;
}

static PRIMTYPE(type_hash) {
  return out->unify(Integer::typeVar);
}

struct CHash final : public GCObject<CHash, Continuation> {
  HeapPointer<HeapObject> list;
  HeapPointer<Continuation> cont;

  CHash(HeapObject *list_, Continuation *cont_) : list(list_), cont(cont_) { }

  template <typename T, T (HeapPointerBase::*memberfn)(T x)>
  T recurse(T arg) {
    arg = Continuation::recurse<T, memberfn>(arg);
    arg = (list.*memberfn)(arg);
    arg = (cont.*memberfn)(arg);
    return arg;
  }

  void execute(Runtime &runtime) override;
};

void CHash::execute(Runtime &runtime) {
  MPZ out("0xffffFFFFffffFFFFffffFFFFffffFFFF"); // 128 bit
  runtime.heap.reserve(Integer::reserve(out));

  auto hash = deep_hash(runtime, list.get());
  if (hash.broken) {
    hash.broken->await(runtime, this);
  } else {
    Hash &h = hash.code;
    mpz_import(out.value, sizeof(h.data)/sizeof(h.data[0]), 1, sizeof(h.data[0]), 0, 0, &h.data[0]);
    cont->resume(runtime, Integer::claim(runtime.heap, out));
  }
}

static PRIMFN(prim_hash) {
  runtime.heap.reserve(reserve_list(nargs) + CHash::reserve());
  HeapObject *list = claim_list(runtime.heap, nargs, args);
  runtime.schedule(CHash::claim(runtime.heap, list, continuation));
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
