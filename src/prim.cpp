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
#include "expr.h"
#include "tuple.h"
#include "location.h"
#include "parser.h"
#include "status.h"
#include <cstdlib>
#include <sstream>
#include <iosfwd>

void require_fail(const char *message, unsigned size, Runtime &runtime, const Tuple *scope) {
  std::stringstream ss;
  ss.write(message, size-1);
  if (runtime.stack_trace) {
    for (auto &x : scope->stack_trace()) {
      ss << "  from " << x.file() << std::endl;
    }
  }
  std::string str = ss.str();
  status_write(2, str.data(), str.size());
  runtime.abort = true;
}

HeapObject *alloc_order(Heap &h, int x) {
  int m;
  if (x < 0) m = 0;
  else if (x > 0) m = 2;
  else m = 1;
  return Tuple::alloc(h, &Order->members[m], 0);
}

HeapObject *alloc_nil(Heap &h) {
  return Tuple::alloc(h, &List->members[0], 0);
}

HeapObject *claim_unit(Heap &h) {
  return Tuple::claim(h, &Unit->members[0], 0);
}

HeapObject *claim_bool(Heap &h, bool x) {
  return Tuple::claim(h, &Boolean->members[x?0:1], 0);
}

HeapObject *claim_tuple2(Heap &h, HeapObject *first, HeapObject *second) {
  Tuple *out = Tuple::claim(h, &Pair->members[0], 2);
  out->at(0)->instant_fulfill(first);
  out->at(1)->instant_fulfill(second);
  return out;
}

HeapObject *claim_result(Heap &h, bool ok, HeapObject *value) {
  Tuple *out = Tuple::claim(h, &Result->members[ok?0:1], 1);
  out->at(0)->instant_fulfill(value);
  return out;
}

HeapObject *claim_list(Heap &h, size_t elements, HeapObject** values) {
  Tuple *out = Tuple::claim(h, &List->members[0], 0);
  while (elements) {
    --elements;
    Tuple *next = Tuple::claim(h, &List->members[1], 2);
    next->at(0)->instant_fulfill(values[elements]);
    next->at(1)->instant_fulfill(out);
    out = next;
  }
  return out;
}

void prim_register(PrimMap &pmap, const char *key, PrimFn fn, PrimType type, int flags, void *data) {
  pmap.insert(std::make_pair(key, PrimDesc(fn, type, flags, data)));
}
