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
#include "heap.h"
#include "location.h"
#include "parser.h"
#include "status.h"
#include "thunk.h"
#include <cstdlib>
#include <sstream>
#include <iosfwd>

void require_fail(const char *message, unsigned size, WorkQueue &queue, const Binding *binding) {
  std::stringstream ss;
  ss.write(message, size-1);
  if (queue.stack_trace) {
    for (auto &x : binding->stack_trace()) {
      ss << "  from " << x.file() << std::endl;
    }
  }
  std::string str = ss.str();
  status_write(2, str.data(), str.size());
  queue.abort = true;
}

std::shared_ptr<Value> make_unit() {
  return std::make_shared<Data>(&Unit->members[0], nullptr);
}

std::shared_ptr<Value> make_bool(bool x) {
  return std::make_shared<Data>(&Boolean->members[x?0:1], nullptr);
}

std::shared_ptr<Value> make_order(int x) {
  int m;
  if (x < 0) m = 0;
  else if (x > 0) m = 2;
  else m = 1;
  return std::make_shared<Data>(&Order->members[m], nullptr);
}

// pair x y f = f x y # with x+y already bound
static std::unique_ptr<Lambda> ePair(new Lambda(LOCATION, "_", nullptr));
std::shared_ptr<Value> make_tuple2(std::shared_ptr<Value> &&first, std::shared_ptr<Value> &&second) {
  auto bind0 = std::make_shared<Binding>(nullptr, nullptr, ePair.get(), 1);
  bind0->future[0].value = std::move(first);
  bind0->state = 1;
  auto bind1 = std::make_shared<Binding>(std::move(bind0), nullptr, ePair.get(), 1);
  bind1->future[0].value = std::move(second);
  bind1->state = 1;
  return std::make_shared<Data>(&Pair->members[0], std::move(bind1));
}

// nill x y z = y
static std::unique_ptr<Lambda> eList(new Lambda(LOCATION, "_", nullptr));
std::shared_ptr<Value> make_list(std::vector<std::shared_ptr<Value> > &&values) {
  auto out = std::make_shared<Data>(&List->members[0], nullptr);
  for (auto i = values.rbegin(); i != values.rend(); ++i) {
    auto bind0 = std::make_shared<Binding>(nullptr, nullptr, eList.get(), 1);
    bind0->future[0].value = std::move(*i);
    bind0->state = 1;
    auto bind1 = std::make_shared<Binding>(std::move(bind0), nullptr, eList.get(), 1);
    bind1->future[0].value = std::move(out);
    bind1->state = 1;
    out = std::make_shared<Data>(&List->members[1], std::move(bind1));
  }
  return out;
}

static std::unique_ptr<Lambda> eResult(new Lambda(LOCATION, "_", nullptr));
std::shared_ptr<Value> make_result(bool ok, std::shared_ptr<Value> &&value) {
  auto bind = std::make_shared<Binding>(nullptr, nullptr, eResult.get(), 1);
  bind->future[0].value = std::move(value);
  bind->state = 1;
  return std::make_shared<Data>(&Result->members[ok?0:1], std::move(bind));
}

void prim_register(PrimMap &pmap, const char *key, PrimFn fn, PrimType type, int flags, void *data) {
  pmap.insert(std::make_pair(key, PrimDesc(fn, type, flags, data)));
  // pmap.emplace(key, PrimDesc(fn, type, flags, data));
}
