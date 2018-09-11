#include <iostream>
#include <cassert>
#include <cstring>
#include "parser.h"
#include "bind.h"
#include "symbol.h"
#include "value.h"
#include "thunk.h"
#include "heap.h"
#include "expr.h"
#include "job.h"
#include "stack.h"

static ThunkQueue queue;

void resume(std::unique_ptr<Receiver> completion, std::shared_ptr<Value> &&return_value) {
  Receiver::receiveM(queue, std::move(completion), std::move(return_value));
}

struct Output : public Receiver {
  void receive(ThunkQueue &queue, std::shared_ptr<Value> &&value);
};

void Output::receive(ThunkQueue &queue, std::shared_ptr<Value> &&value) {
  std::cout << value.get() << std::endl;
}

int main(int argc, const char **argv) {
  bool ok = true;

  JobTable jobtable(4);

  std::unique_ptr<Top> top(new Top);
  for (int i = 1; i < argc; ++i) {
    Lexer lex(argv[i]);
    parse_top(*top.get(), lex);
    if (lex.fail) ok = false;
  }

  /* Primitives */
  PrimMap pmap;
  prim_register_string(pmap);
  prim_register_integer(pmap);
  prim_register_polymorphic(pmap);
  prim_register_job(&jobtable, pmap);

  const char *slash = strrchr(argv[0], '/');
  const char *exec = slash ? slash + 1 : argv[0];
  if (!strcmp(exec, "wake-parse")) {
    std::cout << top.get();
    return 0;
  }

  std::unique_ptr<Expr> root = bind_refs(std::move(top), pmap);
  if (!root) ok = false;

  if (!ok) {
    std::cerr << ">>> Aborting without execution <<<" << std::endl;
    return 1;
  }

  queue.queue.emplace(root.get(), nullptr, std::unique_ptr<Receiver>(new Output));
  do { queue.run(); } while (jobtable.wait());

  //std::cerr << "Computed in " << Action::next_serial << " steps." << std::endl;
  return 0;
}
