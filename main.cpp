#include <iostream>
#include <cassert>
#include <cstring>
#include "parser.h"
#include "bind.h"
#include "symbol.h"
#include "value.h"
#include "action.h"
#include "expr.h"
#include "job.h"
#include "stack.h"

static ActionQueue queue;

void stack_trace(const std::unique_ptr<Action> &completion) {
  std::cerr << completion->stack.get();
}

void resume(std::unique_ptr<Action> completion, std::shared_ptr<Value> &&return_value) {
  std::unique_ptr<PrimRet> ret(reinterpret_cast<PrimRet*>(completion.release()));
  ret->future_input->complete(queue, return_value, ret->invoker_serial);
  queue.push(std::move(ret));
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

  std::unique_ptr<Action> main(new Eval(root.get()));
  std::shared_ptr<Future> result(main->future_result);
  queue.push(std::move(main));

  do while (!queue.empty()) {
    queue.pop()->execute(queue);
  } while (jobtable.wait());

  std::cerr << "Computed in " << Action::next_serial << " steps." << std::endl;
  std::shared_ptr<Value> out = result->get_value();
  assert (out);
  std::cout << out.get() << std::endl;

  return 0;
}
