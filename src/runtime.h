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

#ifndef RUNTIME_H
#define RUNTIME_H

#include "gc.h"

struct Expr;
struct Runtime;
struct Continuation;
struct Tuple;

struct Work : public HeapObject {
  HeapPointer<Work> next;

  virtual void execute(Runtime &runtime) = 0;
  void format(std::ostream &os, FormatState &state) const override;

  PadObject *recurse(PadObject *free) {
    free = HeapObject::recurse(free);
    free = next.moveto(free);
    return free;
  }
};

struct Runtime {
  bool stack_trace;
  bool abort;
  Heap heap;
  RootPointer<Work> stack;
  RootPointer<HeapObject> output;

  Runtime();
  void run();

  void schedule(Work *work) {
    work->next = stack;
    stack = work;
  }

  void init(Expr *root);

  static size_t reserve_eval();
  void claim_eval(Expr *expr, Tuple *scope, Continuation *cont);
};

struct Continuation : public Work {
  HeapPointer<HeapObject> value;

  void resume(Runtime &runtime, HeapObject *obj) {
    value = obj;
    runtime.schedule(this);
  }

  PadObject *recurse(PadObject *free) {
    free = Work::recurse(free);
    free = value.moveto(free);
    return free;
  }
};

#endif
