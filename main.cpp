#include <string.h>
#include <iostream>
#include "parser.h"
#include "bind.h"
#include "symbol.h"
#include "value.h"
#include "action.h"

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

  Location location("<init>");
  auto root = new DefMap(location, defs, new VarRef(location, "main"));
  if (!bind_refs(root)) ok = false;

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
  ActionQueue queue;
  queue.push_back(top);
  long steps = 0, widest = 0;
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
