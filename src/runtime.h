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
struct Record;
struct Scope;
struct Deferral;

struct Work : public HeapObject {
  HeapPointer<Work> next;

  virtual void execute(Runtime &runtime) = 0;
  void format(std::ostream &os, FormatState &state) const override;
  Hash hash() const override;
  Category category() const override;

  template <typename T, T (HeapPointerBase::*memberfn)(T x)>
  T recurse(T arg) {
    arg = HeapObject::recurse<T, memberfn>(arg);
    arg = (next.*memberfn)(arg);
    return arg;
  }
};

struct Runtime {
  bool abort;
  Heap heap;
  RootPointer<Work> stack;
  RootPointer<HeapObject> output;
  RootPointer<Record> sources; // Vector String

  Runtime(int profile_heap, double heap_factor);
  void run();

  void schedule(Work *work) {
#ifdef DEBUG_GC
    assert(!work->next);
#endif
    work->next = stack;
    stack = work;
  }

  void init(Expr *root);

  static size_t reserve_eval();
  void claim_eval(Expr *expr, Scope *scope, Continuation *cont);
};

struct Continuation : public Work {
  HeapPointer<HeapObject> value;

  // Should the deferral be demanded?
  virtual void consider(Runtime &runtime, Deferral *def);

  void resume(Runtime &runtime, HeapObject *obj) {
    value = obj;
    runtime.schedule(this);
  }

  template <typename T, T (HeapPointerBase::*memberfn)(T x)>
  T recurse(T arg) {
    arg = Work::recurse<T, memberfn>(arg);
    arg = (value.*memberfn)(arg);
    return arg;
  }
};

struct Deferral final : public GCObject<Deferral, Continuation> {
  HeapPointer<Work> work; // nullptr => work already scheduled
  HeapPointer<Continuation> uses; // nullptr => already executed
  // HeapPointer<Deferral> strict;
  // ... if we want full eager evaluation link deferrals to runtime
  // ... to control memory, ensure no two executed deferrals are neighbours

  Category category() const override;
  void execute(Runtime &runtime) override;

  void demand(Runtime &runtime) {
#ifdef DEBUG_GC
    assert (work);
#endif
    runtime.schedule(work.get());
    work = nullptr;
  }

  template <typename T, T (HeapPointerBase::*memberfn)(T x)>
  T recurse(T arg) {
    arg = Continuation::recurse<T, memberfn>(arg);
    arg = (work.*memberfn)(arg);
    arg = (uses.*memberfn)(arg);
    return arg;
  }
};

#endif
