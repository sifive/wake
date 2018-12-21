#include "prim.h"
#include "value.h"
#include "expr.h"
#include "heap.h"
#include "location.h"
#include "parser.h"
#include <cstdlib>
#include <sstream>

std::unique_ptr<Receiver> require(const char *fn, WorkQueue &queue, std::unique_ptr<Receiver> completion, const std::shared_ptr<Binding> &binding, bool ok, const std::string &str_) {
  if (!ok) {
    std::stringstream str;
    str << fn << ": " << str_ << std::endl;
    Receiver::receive(queue, std::move(completion), std::make_shared<Exception>(str.str(), binding));
    return std::unique_ptr<Receiver>();
  }
  return completion;
}

std::unique_ptr<Receiver> expect_args(const char *fn, WorkQueue &queue, std::unique_ptr<Receiver> completion, const std::shared_ptr<Binding> &binding, const std::vector<std::shared_ptr<Value> > &args, int expect) {
  if (args.size() != (size_t)expect) {
    std::stringstream str;
    str << fn << " called on " << args.size() << "; was expecting " << expect << std::endl;
    Receiver::receive(queue, std::move(completion), std::make_shared<Exception>(str.str(), binding));
    return std::unique_ptr<Receiver>();
  }

  // merge exceptions
  auto exception = std::make_shared<Exception>();
  for (auto &i : args) {
    if (i->type == Exception::type) {
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
  if (value->type != String::type) {
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
  if (value->type != Integer::type) {
    std::stringstream str;
    str << value->to_str() << " is not an Integer";
    Receiver::receive(queue, std::move(completion), std::make_shared<Exception>(str.str(), binding));
    return std::unique_ptr<Receiver>();
  } else {
    *in = reinterpret_cast<Integer*>(value.get());
    return completion;
  }
}

// true  x y = x
std::shared_ptr<Value> make_true() {
  return std::make_shared<Data>(&Bool->members[0], nullptr);
}

// false x y = y
std::shared_ptr<Value> make_false() {
  return std::make_shared<Data>(&Bool->members[1], nullptr);
}

// pair x y f = f x y # with x+y already bound
static std::unique_ptr<Lambda> ePair(new Lambda(LOCATION, "_", nullptr));
std::shared_ptr<Value> make_tuple(std::shared_ptr<Value> &&first, std::shared_ptr<Value> &&second) {
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
