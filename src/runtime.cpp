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

#include "runtime.h"

Runtime::Runtime() : heap(), stack(heap.root<Work>(nullptr)) {
}

void Runtime::execute() {
  while (stack) {
    Work *w = stack.get();
    stack = w->next;
    try {
      w->execute(*this);
    } catch (GCNeededException gc) {
      // retry work after memory is available
      w->next = stack;
      stack = w;
      heap.GC(gc.needed);
    }
  }
}

void Future::fill(Runtime &runtime, HeapObject *obj) {
  if (value) {
    Continuation *c = static_cast<Continuation*>(value.get());
    while (c->next) {
      c->value = obj;
      c = static_cast<Continuation*>(c->next.get());
    }
    c->value = obj;
    c->next = runtime.stack;
    runtime.stack = value;
  }
  value = obj;
}
