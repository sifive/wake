#include <iostream>
#include <cassert>
#include <cstring>
#include "parser.h"
#include "bind.h"
#include "symbol.h"
#include "value.h"
#include "action.h"

// resume should have an error report method with stack trace !!!
void string_concat(void *data, const std::vector<Value*> &args, Action *completion) {
  if (args.size() != 2) {
    std::cerr << "strcat called on " << args.size() << std::endl;
    stack_trace(completion);
  } else if (args[0]->type != String::type) {
    std::cerr << "strcat called with arg1=" << args[0] << " which is not a string." << std::endl;
    stack_trace(completion);
  } else if (args[1]->type != String::type) {
    std::cerr << "strcat called with arg2=" << args[1] << " which is not a string." << std::endl;
    stack_trace(completion);
  } else {
    String *arg0 = reinterpret_cast<String*>(args[0]);
    String *arg1 = reinterpret_cast<String*>(args[1]);
    resume(completion, new String(arg0->value + arg1->value));
    return;
  }
  resume(completion, new String("bad-value"));
  return;
}

void stack_trace(Action *failure) {
  for (Action *action = failure; action; action = action->invoker) {
    if (action->type == Thunk::type) {
      Thunk *thunk = reinterpret_cast<Thunk*>(action);
      std::cerr << "  from " << thunk->expr->location.str() << std::endl;
    }
  }
}

static ActionQueue queue;
void resume(Action *completion, Value *return_value) {
  PrimRet *ret = reinterpret_cast<PrimRet*>(completion);
  ret->input_value = return_value;
  queue.push_back(ret);
}

int main(int argc, const char **argv) {
  bool ok = true;

  DefMap::defs defs;
  for (int i = 1; i < argc; ++i) {
    Lexer lex(argv[i]);
    DefMap::defs file = parse_top(lex);
    if (lex.fail) ok = false;

    for (auto i = file.begin(); i != file.end(); ++i) {
      if (defs.find(i->first) != defs.end()) {
        std::cerr << "Duplicate def "
          << i->first << " at "
          << defs[i->first]->location.str() << " and "
          << i->second->location.str();
        ok = false;
      } else {
        defs[i->first] = std::move(i->second);
      }
    }
  }

  /* Primitives */
  PrimMap pmap;
  pmap["strcat"].first = string_concat;

  Location location("<init>");
  auto root = new DefMap(location, defs, new VarRef(location, "main"));
  if (!bind_refs(root, pmap)) ok = false;

  const char *slash = strrchr(argv[0], '/');
  const char *exec = slash ? slash + 1 : argv[0];
  int debug = !strcmp(exec, "wideml-debug");

  if (!strcmp(exec, "wideml-parse")) {
    std::cout << root;
    return 0;
  }

  if (!ok) {
    std::cerr << ">>> Aborting without execution <<<" << std::endl;
    return 1;
  }

  Thunk *top = new Thunk(0, root, 0);
  queue.push_back(top);
  unsigned long steps = 0, widest = 0;
  while (!queue.empty()) {
    Action *doit = queue.front();
    queue.pop_front();

    if (debug) {
      if (doit->type == Thunk::type) {
        Thunk *thunk = reinterpret_cast<Thunk*>(doit);
        std::cerr << "Executing " << thunk->expr->type << " @ " << thunk->expr->location.str() << std::endl;
      } else {
        std::cerr << "Executing " << doit->type << std::endl;
      }
      ++steps;
      if (queue.size() > widest) widest = queue.size();
    }

    doit->execute(queue);
  }

  if (debug) {
    std::cerr << "Computed in " << steps << " steps with " << widest << " in parallel." << std::endl;
  }
  assert (top->output());
  std::cout << top->output() << std::endl;

  return 0;
}
