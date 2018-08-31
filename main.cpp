#include <iostream>
#include "parser.h"
#include "bind.h"

int main(int argc, const char **argv) {
  DefMap::defs defs;
  for (int i = 1; i < argc; ++i) {
    Lexer lex(argv[i]);
    DefMap::defs file = parse_top(lex);

    for (auto i = file.begin(); i != file.end(); ++i) {
      assert (defs.find(i->first) == defs.end());
      defs[i->first] = std::move(i->second);
    }
  }

  auto root = new DefMap(defs, new VarRef("main", "<start>"));
  if (!bind_refs(root)) {
    fprintf(stderr, "Variable resolution failure\n");
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
