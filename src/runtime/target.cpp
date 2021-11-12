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

#include "types/datatype.h"
#include "types/type.h"
#include "types/data.h"
#include "types/internal.h"
#include "value.h"
#include "tuple.h"
#include "status.h"
#include "prim.h"

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
  long keyargs;
  std::unordered_map<Hash, TargetValue, HashHasher> table;
  std::vector<HeapPointer<String> > argnames;

  Target(Heap &h, String *location_, long keyargs_) : Parent(h), location(location_), keyargs(keyargs_) { }
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

template <typename T, T (HeapPointerBase::*memberfn)(T x)>
T Target::recurse(T arg) {
  arg = Parent::recurse<T, memberfn>(arg);
  arg = (location.*memberfn)(arg);
  for (auto &x : argnames)
    arg = (x.*memberfn)(arg);
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
  return out->unify(Data::typeInteger);
}

static PRIMFN(prim_hash) {
  runtime.heap.reserve(Tuple::fulfiller_pads + reserve_list(nargs) + reserve_hash());
  Continuation *continuation = scope->claim_fulfiller(runtime, output);
  Value *list = claim_list(runtime.heap, nargs, args);
  runtime.schedule(claim_hash(runtime.heap, list, continuation));
}

static PRIMTYPE(type_tnew) {
  bool ok = true;
  for (size_t i = 2; i < args.size(); ++i)
    ok = ok && args[i]->unify(Data::typeString);
  return ok && args.size() >= 2 &&
    args[0]->unify(Data::typeString) &&
    args[1]->unify(Data::typeInteger) &&
    out->unify(Data::typeTarget);
}

static PRIMFN(prim_tnew) {
  REQUIRE(nargs >= 2);
  STRING(location, 0);
  INTEGER_MPZ(keyargs, 1);

  REQUIRE(mpz_cmp_si(keyargs, 0) >= 0);
  REQUIRE(mpz_cmp_si(keyargs, 1000) <= 0);

  Target *t = Target::alloc(runtime.heap, runtime.heap, location, mpz_get_si(keyargs));
  for (size_t i = 2; i < nargs; ++i) {
    STRING(argn, i);
    t->argnames.emplace_back(argn);
  }

  RETURN(t);
}

struct CTargetFill final : public GCObject<CTargetFill, Continuation> {
  HeapPointer<Target> target;
  Hash hash;

  CTargetFill(Target *target_, Hash hash_)
   : target(target_), hash(hash_) { }

  template <typename T, T (HeapPointerBase::*memberfn)(T x)>
  T recurse(T arg) {
    arg = Continuation::recurse<T, memberfn>(arg);
    arg = (target.*memberfn)(arg);
    return arg;
  }

  void execute(Runtime &runtime) override;
};

void CTargetFill::execute(Runtime &runtime) {
  target->table[hash].promise.fulfill(runtime, value.get());
}

struct CTargetArgs final : public GCObject<CTargetArgs, Continuation> {
  HeapPointer<Target> target;
  HeapPointer<Closure> body;
  HeapPointer<Value> list;
  HeapPointer<Scope> caller;
  HeapPointer<Continuation> cont;

  CTargetArgs(Target *target_, Closure *body_, Value *list_, Scope *caller_, Continuation *cont_)
   : target(target_), body(body_), list(list_), caller(caller_), cont(cont_) { }

  template <typename T, T (HeapPointerBase::*memberfn)(T x)>
  T recurse(T arg) {
    arg = Continuation::recurse<T, memberfn>(arg);
    arg = (target.*memberfn)(arg);
    arg = (body.*memberfn)(arg);
    arg = (list.*memberfn)(arg);
    arg = (caller.*memberfn)(arg);
    arg = (cont.*memberfn)(arg);
    return arg;
  }

  void execute(Runtime &runtime) override;
};

void CTargetArgs::execute(Runtime &runtime) {
  // value = hash(list) ... which we will ignore

  runtime.heap.reserve(Runtime::reserve_apply(body->fun) + CTargetFill::reserve());

  long i = 0;
  std::vector<uint64_t> hashes, subhashes;

  for (Record *item = static_cast<Record*>(list.get()); item->size() == 2; item = item->at(1)->coerce<Record>()) {
    Hash h = item->at(0)->coerce<Value>()->deep_hash(runtime.heap);
    if (++i <= target->keyargs) {
      h.push(hashes);
    } else {
      h.push(subhashes);
    }
  }

  Hash hash(hashes), subhash(subhashes);

  auto ref = target->table.insert(std::make_pair(hash, TargetValue(subhash)));
  ref.first->second.promise.await(runtime, cont.get());

  if (!(ref.first->second.subhash == subhash)) {
    std::stringstream ss;
    ss << "ERROR: Target subkey mismatch for " << target->location->c_str() << std::endl;
    for (auto &x : caller->stack_trace())
      ss << "  from " << x << std::endl;
    ss << "To debug, rerun your wake command with these additional options:" << std::endl;
    ss << "  --debug-target=" << hash.data[0] << " to see the unique target arguments (before the '\\')" << std::endl;
    ss << "  --debug-target=" << ref.first->second.subhash.data[0] << " to see the first invocation's extra arguments" << std::endl;
    ss << "  --debug-target=" << subhash.data[0] << " to see the second invocation's extra arguments" << std::endl;
    status_write(STREAM_ERROR, ss.str());
    runtime.abort = true;
  }

  if (ref.second)
    runtime.claim_apply(body.get(), target.get(),
      CTargetFill::claim(runtime.heap, target.get(), hash), caller.get());
}

static PRIMFN(prim_tget) {
  REQUIRE(nargs >= 2);
  TARGET(target, 0);
  CLOSURE(body, 1);
  REQUIRE(nargs == target->argnames.size() + 2);

  runtime.heap.reserve(
    Tuple::fulfiller_pads
    + reserve_list(target->argnames.size())
    + reserve_hash()
    + CTargetArgs::reserve());

  Continuation *cont = scope->claim_fulfiller(runtime, output);
  Value *list = claim_list(runtime.heap, target->argnames.size(), args+2);

  runtime.schedule(claim_hash(runtime.heap, list,
    CTargetArgs::claim(runtime.heap, target, body, list, scope, cont)));
}

void prim_register_target(PrimMap &pmap) {
  prim_register(pmap, "hash", prim_hash, type_hash, PRIM_PURE);
  prim_register(pmap, "tnew", prim_tnew, type_tnew, PRIM_ORDERED);
  prim_register(pmap, "tget", prim_tget, type_tget, PRIM_FNARG); // kind depends on function argument
}
