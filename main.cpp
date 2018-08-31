#include <iostream>
#include "parser.h"
#include "bind.h"
#include "symbol.h"
#include "value.h"

int main(int argc, const char **argv) {
  bool ok = true;

  DefMap::defs defs;
  for (int i = 1; i < argc; ++i) {
    Lexer lex(argv[i]);
    DefMap::defs file = parse_top(lex);
    if (lex.fail) ok = false;

    for (auto i = file.begin(); i != file.end(); ++i) {
      if (defs.find(i->first) != defs.end()) {
        fprintf(stderr, "Duplicate def %s at %s and %s\n",
          i->first.c_str(),
          defs[i->first]->location.str().c_str(),
          i->second->location.str().c_str());
        ok = false;
      } else {
        defs[i->first] = std::move(i->second);
      }
    }
  }

  auto root = new DefMap(Location(), defs, new VarRef("main", "<start>"));
  if (!bind_refs(root)) ok = false;

  std::cout << root;

  if (!ok) {
    fprintf(stderr, ">>> Aborting without execution <<<\n");
    return 1;
  }

/*
  ActionQueue queue;
  while (!queue.empty()) {
    Action *doit = queue.front();
    queue.pop_front();
    doit->execute(queue);
  }
*/

  return 0;
}
