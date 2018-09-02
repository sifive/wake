#include <iostream>
#include <cassert>
#include <cstring>
#include "parser.h"
#include "bind.h"
#include "symbol.h"
#include "value.h"
#include "action.h"

void stack_trace(Action *failure) {
  Location *trace = 0;
  for (Action *action = failure; action; action = action->invoker) {
    if (action->type == Thunk::type) {
      Thunk *thunk = reinterpret_cast<Thunk*>(action);
      if (trace && !thunk->expr->location.contains(*trace))
        std::cerr << "  from " << trace->str() << std::endl;
      trace = &thunk->expr->location;
    }
  }
  // last trace is always <init>:1:1; don't print it
}

static ActionQueue queue;
void resume(Action *completion, Value *return_value) {
  PrimRet *ret = reinterpret_cast<PrimRet*>(completion);
  ret->input_value = return_value;
  queue.push_back(ret);
}

Value *prim_true;
Value *prim_false;

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

  /* Initialize primitive bools */
  Location location("<init>");
  prim_true  = new Closure(new Lambda(location, "_", new VarRef(location, "_", 1, 0)), 0);
  prim_false = new Closure(new Lambda(location, "_", new VarRef(location, "_", 0, 0)), 0);

  /* Primitives */
  PrimMap pmap;
  prim_register_string(pmap);
  prim_register_integer(pmap);
  prim_register_polymorphic(pmap);

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
