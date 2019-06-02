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
#include <cassert>

struct Target : public Value {
  std::map<Hash, Future> table;
  static const TypeDescriptor type;
  static TypeVar typeVar;
  Target() : Value(&type) { }

  void format(std::ostream &os, FormatState &state) const;
  TypeVar &getType();
  Hash hash() const;
};

const TypeDescriptor Target::type("_Target");

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
  return args.size() > 0 &&
    out->unify(Integer::typeVar);
}

static PRIMFN(prim_hash) {
  auto out = std::make_shared<Integer>();
  Hash h = binding->hash();
  mpz_import(out->value, sizeof(h.data)/sizeof(h.data[0]), 1, sizeof(h.data[0]), 0, 0, &h.data[0]);
  RETURN(out);
}

static PRIMTYPE(type_tnew) {
  return args.size() == 0 &&
    out->unify(Target::typeVar);
}

static PRIMFN(prim_tnew) {
  EXPECT(0);
  auto out = std::make_shared<Target>();
  RETURN(out);
}

static PRIMTYPE(type_tget) {
  return args.size() == 3 &&
    args[0]->unify(Target::typeVar) &&
    args[1]->unify(Integer::typeVar) &&
    args[2]->unify(TypeVar(FN, 2)) &&
    (*args[2])[0].unify(Integer::typeVar) &&
    out->unify((*args[2])[1]);
}

static PRIMFN(prim_tget) {
  EXPECT(3);
  TARGET(target, 0);
  INTEGER(hash, 1);
  CLOSURE(body, 2);

  Hash h;
  REQUIRE(mpz_sizeinbase(hash->value, 2) <= 8*sizeof(h.data));
  mpz_export(&h.data[0], 0, 1, sizeof(h.data[0]), 0, 0, hash->value);

  auto ref = target->table.insert(std::make_pair(h, Future()));
  ref.first->second.depend(queue, std::move(completion));

  if (ref.second) {
    auto bind = std::make_shared<Binding>(body->binding, queue.stack_trace?binding:nullptr, body->lambda, 1);
    bind->future[0].value = std::move(args[1]); // hash
    bind->state = 1;
    queue.emplace(body->lambda->body.get(), std::move(bind), ref.first->second.make_completer());
  }
}

void prim_register_target(PrimMap &pmap) {
  prim_register(pmap, "hash", prim_hash, type_hash, PRIM_PURE);
  prim_register(pmap, "tnew", prim_tnew, type_tnew, PRIM_SHALLOW);
  prim_register(pmap, "tget", prim_tget, type_tget, PRIM_SHALLOW);
}
