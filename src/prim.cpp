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
#include <cstdlib>
#include <sstream>
#include <iosfwd>

std::unique_ptr<Receiver> require(const char *fn, WorkQueue &queue, std::unique_ptr<Receiver> completion, const std::shared_ptr<Binding> &binding, bool ok, const std::string &str_) {
  if (!ok) {
    std::stringstream str;
    str << fn << ": " << str_;
    Receiver::receive(queue, std::move(completion), std::make_shared<Exception>(str.str(), binding));
    return std::unique_ptr<Receiver>();
  }
  return completion;
}

std::unique_ptr<Receiver> expect_args(const char *fn, WorkQueue &queue, std::unique_ptr<Receiver> completion, const std::shared_ptr<Binding> &binding, const std::vector<std::shared_ptr<Value> > &args, int expect) {
  if (args.size() != (size_t)expect) {
    std::stringstream str;
    str << fn << " called on " << args.size() << "; was expecting " << expect;
    Receiver::receive(queue, std::move(completion), std::make_shared<Exception>(str.str(), binding));
    return std::unique_ptr<Receiver>();
  }

  // merge exceptions
  auto exception = std::make_shared<Exception>();
  for (auto &i : args) {
    if (i->type == &Exception::type) {
      (*exception) += *reinterpret_cast<Exception*>(i.get());
    }
  }

  if (exception->causes.empty()) {
    return completion;
  } else {
    Receiver::receive(queue, std::move(completion), std::move(exception));
    return std::unique_ptr<Receiver>();
  }
}

std::unique_ptr<Receiver> cast_string(WorkQueue &queue, std::unique_ptr<Receiver> completion, const std::shared_ptr<Binding> &binding, const std::shared_ptr<Value> &value, String **str) {
  if (value->type != &String::type) {
    std::stringstream str;
    str << value->to_str() << " is not a String";
    Receiver::receive(queue, std::move(completion), std::make_shared<Exception>(str.str(), binding));
    return std::unique_ptr<Receiver>();
  } else {
    *str = reinterpret_cast<String*>(value.get());
    return completion;
  }
}

std::unique_ptr<Receiver> cast_integer(WorkQueue &queue, std::unique_ptr<Receiver> completion, const std::shared_ptr<Binding> &binding, const std::shared_ptr<Value> &value, Integer **in) {
  if (value->type != &Integer::type) {
    std::stringstream str;
    str << value->to_str() << " is not an Integer";
    Receiver::receive(queue, std::move(completion), std::make_shared<Exception>(str.str(), binding));
    return std::unique_ptr<Receiver>();
  } else {
    *in = reinterpret_cast<Integer*>(value.get());
    return completion;
  }
}

std::unique_ptr<Receiver> cast_double(WorkQueue &queue, std::unique_ptr<Receiver> completion, const std::shared_ptr<Binding> &binding, const std::shared_ptr<Value> &value, Double **in) {
  if (value->type != &Double::type) {
    std::stringstream str;
    str << value->to_str() << " is not a Double";
    Receiver::receive(queue, std::move(completion), std::make_shared<Exception>(str.str(), binding));
    return std::unique_ptr<Receiver>();
  } else {
    *in = reinterpret_cast<Double*>(value.get());
    return completion;
  }
}

std::unique_ptr<Receiver> cast_regexp(WorkQueue &queue, std::unique_ptr<Receiver> completion, const std::shared_ptr<Binding> &binding, const std::shared_ptr<Value> &value, RegExp **reg) {
  if (value->type != &RegExp::type) {
    Receiver::receive(queue, std::move(completion), std::make_shared<Exception>(value->to_str() + " is not a RegExp", binding));
    return std::unique_ptr<Receiver>();
  } else {
    *reg = reinterpret_cast<RegExp*>(value.get());
    return completion;
  }
}

std::unique_ptr<Receiver> cast_data(WorkQueue &queue, std::unique_ptr<Receiver> completion, const std::shared_ptr<Binding> &binding, const std::shared_ptr<Value> &value, Data **in) {
  if (value->type != &Data::type) {
    std::stringstream str;
    str << value->to_str() << " is not a Data";
    Receiver::receive(queue, std::move(completion), std::make_shared<Exception>(str.str(), binding));
    return std::unique_ptr<Receiver>();
  } else {
    *in = reinterpret_cast<Data*>(value.get());
    return completion;
  }
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

void prim_register(PrimMap &pmap, const char *key, PrimFn fn, PrimType type, int flags, void *data) {
  pmap.insert(std::make_pair(key, PrimDesc(fn, type, flags, data)));
  // pmap.emplace(key, PrimDesc(fn, type, flags, data));
}
