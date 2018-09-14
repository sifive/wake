#include <iostream>
#include <cassert>
#include <cstring>
#include <thread>
#include <sstream>
#include "parser.h"
#include "bind.h"
#include "symbol.h"
#include "value.h"
#include "thunk.h"
#include "heap.h"
#include "expr.h"
#include "job.h"
#include "sources.h"
#include "argagg.hpp"

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
  const char *usage = "Usage: wake [options] [--] EXPRESSION";
  argagg::parser argparser {{
    { "help", {"-h", "--help"},
      "shows this help message", 0},
    { "jobs", {"-j", "--jobs"},
      "number of concurrent jobs to run", 1},
    { "debug", {"-d", "--debug"},
      "simulate a stack for exceptions", 0},
    { "verbose", {"-v", "--verbose"},
      "output progress information", 0},
    { "init", {"--init"},
      "directory to configure as workspace top", 1},
  }};

  argagg::parser_results args;
  try { args = argparser.parse(argc, argv); }
  catch (const std::exception& e) { std::cerr << e.what() << std::endl; return 1; }

  if (args["help"]) {
    std::cerr << usage << std::endl << argparser;
    return 0;
  }

  int jobs = args["jobs"].as<int>(std::thread::hardware_concurrency());
  bool verbose = args["verbose"];
  queue.stack_trace = args["debug"];

  if (args["init"]) {
    std::string dir = args["init"];
    if (!make_workspace(dir)) {
      std::cerr << "Unable to initialize a workspace in " << dir << std::endl;
      return 1;
    }
  } else if (!chdir_workspace()) {
    std::cerr << "Unable to locate wake.db in any parent directory." << std::endl;
    return 1;
  }

  auto all_sources(find_all_sources());
  auto wakefiles(sources(all_sources, ".*/[^/]+\\.wake"));

  bool ok = true;
  std::unique_ptr<Top> top(new Top);
  for (auto i : wakefiles) {
    Lexer lex(i->value.c_str());
    parse_top(*top.get(), lex);
    if (lex.fail) ok = false;
  }
  wakefiles.clear();

  if (args.pos.empty()) {
    // read from stdin
    std::cerr << "Interactive mode not yet supported; supply batch commands on command-line" << std::endl;
    std::cerr << std::endl << usage << std::endl << argparser;
    return 1;
  } else {
    std::stringstream expr;
    for (auto i : args.pos) expr << i << " ";
    Lexer lex(expr.str());
    top->body = std::unique_ptr<Expr>(parse_command(lex));
    if (lex.fail) ok = false;
  }

  /* Primitives */
  JobTable jobtable(jobs, verbose);
  PrimMap pmap;
  prim_register_string(pmap);
  prim_register_integer(pmap);
  prim_register_polymorphic(pmap);
  prim_register_regexp(pmap);
  prim_register_job(&jobtable, pmap);
  pmap["sources"].first = prim_sources;
  pmap["sources"].second = &all_sources;

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

  if (verbose) std::cerr << "Running " << jobs << " jobs at a time." << std::endl;
  queue.queue.emplace(root.get(), nullptr, std::unique_ptr<Receiver>(new Output));
  do { queue.run(); } while (jobtable.wait());

  //std::cerr << "Computed in " << Action::next_serial << " steps." << std::endl;
  return 0;
}
