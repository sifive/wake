/*
 * Copyright 2019 SiFive, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef HEAP_H
#define HEAP_H

#include "hash.h"
#include <memory>
#include <vector>
#include <iosfwd>

struct Value;
struct WorkQueue;
struct Expr;
struct Location;

struct Callback {
  virtual ~Callback();
};

// Wait for a value
struct Receiver : public Callback {
  std::unique_ptr<Receiver> next; // for wait Q

  static void receive(WorkQueue &queue, std::unique_ptr<Receiver> receiver, std::shared_ptr<Value> &&value);
  static void receive(WorkQueue &queue, std::unique_ptr<Receiver> receiver, const std::shared_ptr<Value> &value);

protected:
  virtual void receive(WorkQueue &queue, std::shared_ptr<Value> &&value) = 0;

private:
friend struct Receive;
};

// Wait for a binding to be recursively finished
struct Finisher : public Callback {
  static void finish(WorkQueue &queue, std::unique_ptr<Finisher> finisher);

protected:
  virtual void finish(WorkQueue &queue) = 0;

private:
  std::unique_ptr<Finisher> next;
friend struct Binding;
friend struct Receive;
};

struct Future {
  Future() { }

  std::shared_ptr<Value> value;
  void depend(WorkQueue &queue, std::unique_ptr<Receiver> receiver) {
    if (value) {
      Receiver::receive(queue, std::move(receiver), value);
    } else {
      receiver->next = std::move(waiting);
      waiting = std::move(receiver);
    }
  }

  // for use with Memoize::values
  std::unique_ptr<Receiver> make_completer();

private:
  void broadcast(WorkQueue &queue, std::shared_ptr<Value> &&value_);
  std::unique_ptr<Receiver> waiting;
friend struct Completer;
};

#define FLAG_PRINTED	1
#define FLAG_HASH_PRE	2
#define FLAG_HASH_POST	4
#define FLAG_FLATTENED	8

struct Binding {
  std::shared_ptr<Binding> next;      // lexically enclosing scope
  std::shared_ptr<Binding> invoker;   // for stack tracing
  std::unique_ptr<Finisher> finisher; // list of callbacks awaiting finished values
  std::unique_ptr<Future[]> future;   // variable values
  mutable Hash hashcode;
  Expr *expr;
  int nargs;
  int state; // count first completions, then finishes
  mutable int flags;

  Binding(const std::shared_ptr<Binding> &next_, const std::shared_ptr<Binding> &invoker_, Expr *expr_, int nargs_);
  ~Binding();

  static std::unique_ptr<Receiver> make_completer(const std::shared_ptr<Binding> &binding, int arg);

  std::vector<Location> stack_trace() const;

  static void wait(Binding *iter, WorkQueue &queue, std::unique_ptr<Finisher> finisher);
  Hash hash() const; // call only after 'wait'

  void future_completed(WorkQueue &queue);
  void future_finished(WorkQueue &queue);
};

#endif
