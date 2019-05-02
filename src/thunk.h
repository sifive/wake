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

#ifndef THUNK_H
#define THUNK_H

#include <queue>
#include <memory>

// We need ~Callback
#include "heap.h"

struct Binding;
struct Expr;

struct Thunk {
  Expr *expr;
  std::shared_ptr<Binding> binding;
  std::unique_ptr<Receiver> receiver;

  Thunk(Expr *expr_, std::shared_ptr<Binding> &&binding_, std::unique_ptr<Receiver> receiver_)
   : expr(expr_), binding(std::move(binding_)), receiver(std::move(receiver_)) { }

  void eval(WorkQueue &queue);
};

struct Receive {
  std::unique_ptr<Callback> callback;
  std::shared_ptr<Value> value;

  Receive(std::unique_ptr<Callback> callback_, std::shared_ptr<Value> &&value_)
   : callback(std::move(callback_)), value(std::move(value_)) { }

  void eval(WorkQueue &queue);
};

struct WorkQueue {
  bool stack_trace;
  std::queue<Thunk> thunks;
  std::queue<Receive> receives;

  void run();

  void emplace(Expr *expr, std::shared_ptr<Binding> &&binding, std::unique_ptr<Receiver> receiver);
  void emplace(Expr *expr, const std::shared_ptr<Binding> &binding, std::unique_ptr<Receiver> receiver);
};

#endif
