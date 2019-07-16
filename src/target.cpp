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
#include "heap.h"
#include "expr.h"
#include "type.h"
#include "status.h"
#include <sstream>
#include <unordered_map>

struct TargetValue {
  Hash subhash;
  Future future;

  TargetValue() { }
  TargetValue(const Hash &subhash_) : subhash(subhash_) { }
};

struct HashHasher {
  size_t operator()(const Hash &h) const { return h.data[0]; }
};

struct Target : public Value {
  Location location;
  std::unordered_map<Hash, TargetValue, HashHasher> table;

  static const TypeDescriptor type;
  static TypeVar typeVar;
  Target(const Location &location_) : Value(&type), location(location_) { }
  ~Target();

  void format(std::ostream &os, FormatState &state) const;
  TypeVar &getType();
  Hash hash() const;
};

const TypeDescriptor Target::type("_Target");

Target::~Target() {
  for (auto &x : table) {
    if (!x.second.future.value) {
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

TypeVar Target::typeVar("_Target", 0);
TypeVar &Target::getType() {
  return typeVar;
}

Hash Target::hash() const {
  return type.hashcode;
}

#define TARGET(arg, i) REQUIRE(args[i]->type == &Target::type); Target *arg = reinterpret_cast<Target*>(args[i].get());

static PRIMTYPE(type_hash) {
  return out->unify(Integer::typeVar);
}

static PRIMFN(prim_hash) {
  Hash h;
  if (binding) h = binding->hash();
  auto out = std::make_shared<Integer>();
  mpz_import(out->value, sizeof(h.data)/sizeof(h.data[0]), 1, sizeof(h.data[0]), 0, 0, &h.data[0]);
  RETURN(out);
}

static PRIMTYPE(type_tnew) {
  return args.size() == 1 &&
    out->unify(Target::typeVar);
}

static PRIMFN(prim_tnew) {
  EXPECT(1);
  auto out = std::make_shared<Target>(binding->expr->location);
  RETURN(out);
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

struct TargetReceiver : public Receiver {
  std::shared_ptr<Value> target;
  Hash hash;
  void receive(WorkQueue &queue, std::shared_ptr<Value> &&value);
  TargetReceiver(const std::shared_ptr<Value> &target_, Hash hash_)
   : target(target_), hash(hash_) { }
};

void TargetReceiver::receive(WorkQueue &queue, std::shared_ptr<Value> &&value) {
  Target *t = reinterpret_cast<Target*>(target.get());
  t->table[hash].future.broadcast(queue, std::move(value));
}

static PRIMFN(prim_tget) {
  EXPECT(4);
  TARGET(target, 0);
  INTEGER(key, 1);
  INTEGER(subkey, 2);
  CLOSURE(body, 3);

  Hash hash;
  REQUIRE(mpz_sizeinbase(key->value, 2) <= 8*sizeof(hash.data));
  mpz_export(&hash.data[0], 0, 1, sizeof(hash.data[0]), 0, 0, key->value);

  Hash subhash;
  REQUIRE(mpz_sizeinbase(subkey->value, 2) <= 8*sizeof(subhash.data));
  mpz_export(&subhash.data[0], 0, 1, sizeof(subhash.data[0]), 0, 0, subkey->value);

  auto ref = target->table.insert(std::make_pair(hash, TargetValue(subhash)));
  ref.first->second.future.depend(queue, std::move(completion));

  if (!(ref.first->second.subhash == subhash)) {
    std::stringstream ss;
    ss << "ERROR: Target subkey mismatch for " << target->location.text() << std::endl;
    if (queue.stack_trace)
      for (auto &x : binding->stack_trace())
        ss << "  from " << x.file() << std::endl;
    std::string str = ss.str();
    status_write(2, str.data(), str.size());
    queue.abort = true;
  }

  if (ref.second) {
    auto bind = std::make_shared<Binding>(body->binding, queue.stack_trace?binding:nullptr, body->lambda, 1);
    bind->future[0].value = std::move(args[1]); // hash
    bind->state = 1;
    queue.emplace(body->lambda->body.get(), std::move(bind), std::unique_ptr<Receiver>(
      new TargetReceiver(args[0], hash)));
  }
}

void prim_register_target(PrimMap &pmap) {
  prim_register(pmap, "hash", prim_hash, type_hash, PRIM_PURE);
  prim_register(pmap, "tnew", prim_tnew, type_tnew, PRIM_SHALLOW);
  prim_register(pmap, "tget", prim_tget, type_tget, PRIM_SHALLOW);
}
