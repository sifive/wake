#include <iostream>
#include "parser.h"

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

  auto root = new DefMap(defs, new VarRef("asd"));
  std::cout << root;

  return 0;
/*
  ActionQueue queue;
  while (!queue.empty()) {
    Action *doit = queue.front();
    queue.pop_front();
    doit->execute(queue);
  }
*/
}
