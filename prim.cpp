#include "prim.h"
#include "value.h"
#include <iostream>
#include <cstdlib>

void expect_args(const char *fn, Action *completion, const std::vector<Value*> &args, int expect) {
  if (args.size() != expect) {
    std::cerr << fn << " called on " << args.size() << "; was expecting " << expect << std::endl;
    stack_trace(completion);
    exit(1);
  }
}

String *expect_string (const char *fn, Action *completion, Value *value, int index) {
  if (value->type != String::type) {
    std::cerr << fn << " called with argument "
      << index << " = "
      << value << ", which is not a String." << std::endl;
    stack_trace(completion);
    exit(1);
  }
  return reinterpret_cast<String*>(value);
}

Integer *expect_integer(const char *fn, Action *completion, Value *value, int index) {
  if (value->type != Integer::type) {
    std::cerr << fn << " called with argument "
      << index << " = "
      << value << ", which is not an Integer." << std::endl;
    stack_trace(completion);
    exit(1);
  }
  return reinterpret_cast<Integer*>(value);
}
