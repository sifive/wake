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

void stack_trace(Action *completion) {
  std::cerr << completion->stack.get();
}

void resume(Action *completion, Value *return_value) {
  PrimRet *ret = reinterpret_cast<PrimRet*>(completion);
  ret->future_input->complete(queue, return_value, ret->invoker_serial);
  queue.push(ret);
}

int main(int argc, const char **argv) {
  bool ok = true;

  JobTable jobtable(4);

  Top top;
  for (int i = 1; i < argc; ++i) {
    Lexer lex(argv[i]);
    parse_top(top, lex);
    if (lex.fail) ok = false;
  }

  /* Primitives */
  PrimMap pmap;
  prim_register_string(pmap);
  prim_register_integer(pmap);
  prim_register_polymorphic(pmap);
  prim_register_job(&jobtable, pmap);

  if (!bind_refs(&top, pmap)) ok = false;

  const char *slash = strrchr(argv[0], '/');
  const char *exec = slash ? slash + 1 : argv[0];
  if (!strcmp(exec, "wake-parse")) {
    std::cout << &top;
    return 0;
  }

  if (!ok) {
    std::cerr << ">>> Aborting without execution <<<" << std::endl;
    return 1;
  }

  Eval *main = new Eval(0, &top, 0);
  queue.push(main);
  std::shared_ptr<Future> result = main->future_result;

  unsigned long steps = 0;
  do while (!queue.empty()) {
    std::unique_ptr<Action> doit = queue.pop();
    doit->execute(queue);
    ++steps;
  } while (jobtable.wait());

  std::cerr << "Computed in " << steps << " steps." << std::endl;
  std::shared_ptr<Value> out = result->get_value();
  assert (out);
  std::cout << out.get() << std::endl;

  return 0;
}
