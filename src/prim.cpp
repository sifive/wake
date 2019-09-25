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
#include "ssa.h"
#include "expr.h"
#include "tuple.h"
#include "location.h"
#include "parser.h"
#include "status.h"
#include <cstdlib>
#include <sstream>
#include <iosfwd>
#include <unordered_map>
#include <bitset>

void require_fail(const char *message, unsigned size, Runtime &runtime, const Scope *scope) {
  std::stringstream ss;
  ss.write(message, size-1);
  for (auto &x : scope->stack_trace())
    ss << "  from " << x << std::endl;
  std::string str = ss.str();
  status_write(2, str.data(), str.size());
  runtime.abort = true;
}

HeapObject *alloc_order(Heap &h, int x) {
  int m;
  if (x < 0) m = 0;
  else if (x > 0) m = 2;
  else m = 1;
  return Record::alloc(h, &Order->members[m], 0);
}

HeapObject *alloc_nil(Heap &h) {
  return Record::alloc(h, &List->members[0], 0);
}

HeapObject *claim_unit(Heap &h) {
  return Record::claim(h, &Unit->members[0], 0);
}

HeapObject *claim_bool(Heap &h, bool x) {
  return Record::claim(h, &Boolean->members[x?0:1], 0);
}

HeapObject *claim_tuple2(Heap &h, HeapObject *first, HeapObject *second) {
  Record *out = Record::claim(h, &Pair->members[0], 2);
  out->at(0)->instant_fulfill(first);
  out->at(1)->instant_fulfill(second);
  return out;
}

HeapObject *claim_result(Heap &h, bool ok, HeapObject *value) {
  Record *out = Record::claim(h, &Result->members[ok?0:1], 1);
  out->at(0)->instant_fulfill(value);
  return out;
}

HeapObject *claim_list(Heap &h, size_t elements, HeapObject** values) {
  Record *out = Record::claim(h, &List->members[0], 0);
  while (elements) {
    --elements;
    Record *next = Record::claim(h, &List->members[1], 2);
    next->at(0)->instant_fulfill(values[elements]);
    next->at(1)->instant_fulfill(out);
    out = next;
  }
  return out;
}

struct HeapHash {
  Hash code;
  Promise *broken;
};

HeapStep Closure::explore_escape(HeapStep step) {
  Scope *it = scope.get();
  for (size_t i = 0, size; i < applied; i += size) {
    size = it->size();
    for (size_t j = size; j > 0; --j)
      step = it->at(j-1)->template recurse<HeapStep, &HeapPointerBase::explore>(step);
  }
  size_t depth = 1;
  for (auto x: fun->escapes) {
    while (arg_depth(x) > depth) {
      it = it->next.get();
      ++depth;
    }
    step = it->at(arg_offset(x))->template recurse<HeapStep, &HeapPointerBase::explore>(step);
  }
  return step;
}

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

struct CHash final : public GCObject<CHash, Continuation> {
  HeapPointer<HeapObject> obj;
  HeapPointer<Continuation> cont;

  CHash(HeapObject *obj_, Continuation *cont_) : obj(obj_), cont(cont_) { }

  template <typename T, T (HeapPointerBase::*memberfn)(T x)>
  T recurse(T arg) {
    arg = Continuation::recurse<T, memberfn>(arg);
    arg = (obj.*memberfn)(arg);
    arg = (cont.*memberfn)(arg);
    return arg;
  }

  void execute(Runtime &runtime) override;
};

void CHash::execute(Runtime &runtime) {
  MPZ out("0xffffFFFFffffFFFFffffFFFFffffFFFF"); // 128 bit
  runtime.heap.reserve(Integer::reserve(out));

  auto hash = deep_hash(runtime, obj.get());
  if (hash.broken) {
    next = nullptr; // reschedule
    hash.broken->await(runtime, this);
  } else {
    Hash &h = hash.code;
    mpz_import(out.value, sizeof(h.data)/sizeof(h.data[0]), 1, sizeof(h.data[0]), 0, 0, &h.data[0]);
    cont->resume(runtime, Integer::claim(runtime.heap, out));
  }
}

size_t reserve_hash() {
  return CHash::reserve();
}

Work *claim_hash(Heap &h, HeapObject *value, Continuation *continuation) {
  return CHash::claim(h, value, continuation);
}

void prim_register(PrimMap &pmap, const char *key, PrimFn fn, PrimType type, int flags, void *data) {
  pmap.insert(std::make_pair(key, PrimDesc(fn, type, flags, data)));
}

PrimMap prim_register_all(StringInfo *info, JobTable *jobtable) {
  PrimMap pmap;
  prim_register_string(pmap, info);
  prim_register_vector(pmap);
  prim_register_integer(pmap);
  prim_register_double(pmap);
  prim_register_exception(pmap);
  prim_register_regexp(pmap);
  prim_register_target(pmap);
  prim_register_json(pmap);
  prim_register_job(jobtable, pmap);
  prim_register_sources(pmap);
  return pmap;
}
