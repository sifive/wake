#include <iostream>
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
#include "database.h"
#include "argagg.hpp"

static ThunkQueue queue;

struct Output : public Receiver {
  std::shared_ptr<Value> *save;
  Output(std::shared_ptr<Value> *save_) : save(save_) { }
  void receive(ThunkQueue &queue, std::shared_ptr<Value> &&value);
};

void Output::receive(ThunkQueue &queue, std::shared_ptr<Value> &&value) {
  *save = std::move(value);
}

int main(int argc, const char **argv) {
  const char *usage = "Usage: wake [OPTION] [--] [ADDED EXPRESSION]";
  argagg::parser argparser {{
    { "help", {"-h", "--help"},
      "shows this help message", 0},
    { "add", {"-a", "--add"},
      "add a build target to wake", 0},
    { "remove", {"-r", "--remove"},
      "remove a build target from wake", 1},
    { "list", {"-l", "--list"},
      "list builds targets registed with wake", 0},
    { "once", {"-o", "--once"},
      "add a one-shot build target", 0},
    { "jobs", {"-j", "--jobs"},
      "number of concurrent jobs to run", 1},
    { "verbose", {"-v", "--verbose"},
      "output progress information", 0},
    { "debug", {"-d", "--debug"},
      "simulate a stack for exceptions", 0},
    { "parse", {"-p", "--parse"},
      "parse wake files and print the AST", 0},
    { "init", {"-i", "--init"},
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

  Database db;
  std::string fail = db.open();
  if (!fail.empty()) {
    std::cerr << "Failed to open wake.db: " << fail << std::endl;
    return 1;
  }

  std::vector<std::string> targets = db.get_targets();
  if (args["list"]) {
    std::cout << "Active wake targets:" << std::endl;
    int j = 0;
    for (auto &i : targets)
      std::cout << "  " << j++ << " = " << i << std::endl;
  }

  if (args["remove"]) {
    int victim = args["remove"];
    if (victim < 0 || victim >= (int)targets.size()) {
      std::cerr << "Could not remove target " << victim << "; there are only " << targets.size() << std::endl;
      return 1;
    }
    if (args["verbose"]) std::cout << "Removed target " << victim << " = " << targets[victim] << std::endl;
    db.del_target(targets[victim]);
    targets.erase(targets.begin() + victim);
  }

  if (args["once"] || args["add"]) {
    if (args.pos.empty()) {
      std::cerr << "You must specify positional arguments to use for the wake bulid target" << std::endl;
      return 1;
    } else {
      std::stringstream expr;
      bool first = true;
      for (auto i : args.pos) {
        if (!first) expr << " ";
        first = false;
        expr << i;
      }
      targets.push_back(expr.str());
    }
  } else if (!args.pos.empty()) {
    std::cerr << "Unexpected positional arguments (did you forget -a ?):";
    for (auto i : args.pos) std::cerr << " " << i;
    std::cerr << std::endl;
    return 1;
  }

  bool ok = true;
  auto all_sources(find_all_sources());

  // Read all wake build files
  std::unique_ptr<Top> top(new Top);
  for (auto i : sources(all_sources, ".", "(.*/)?[^/]+\\.wake")) {
    Lexer lex(i->value.c_str());
    parse_top(*top.get(), lex);
    if (lex.fail) ok = false;
  }

  // Read all wake targets
  std::vector<std::string> target_names;
  Expr *body = new Lambda(LOCATION, "_", new Literal(LOCATION, "top"));
  for (size_t i = 0; i < targets.size(); ++i) {
    body = new Lambda(LOCATION, "_", body);
    target_names.emplace_back("<target-" + std::to_string(i) + "-expression>");
  }
  for (size_t i = 0; i < targets.size(); ++i) {
    Lexer lex(targets[i], target_names[i].c_str());
    body = new App(LOCATION, body, parse_command(lex));
    if (lex.fail) ok = false;
  }
  top->body = std::unique_ptr<Expr>(body);

  /* Primitives */
  JobTable jobtable(&db, jobs, verbose);
  PrimMap pmap;
  prim_register_string(pmap);
  prim_register_integer(pmap);
  prim_register_polymorphic(pmap);
  prim_register_regexp(pmap);
  prim_register_job(&jobtable, pmap);
  prim_register_sources(&all_sources, pmap);

  if (args["parse"]) std::cout << top.get();

  std::unique_ptr<Expr> root = bind_refs(std::move(top), pmap);
  if (!root) ok = false;

  if (!ok) {
    if (args["add"]) std::cerr << ">>> Expression not added to the active target list <<<" << std::endl;
    std::cerr << ">>> Aborting without execution <<<" << std::endl;
    return 1;
  }

  if (args["add"]) {
    db.add_target(targets.back());
    if (args["verbose"]) std::cout << "Added target " << (targets.size()-1) << " = " << targets.back() << std::endl;
  }
  if (args["parse"]) return 0;
  if (args["list"]) return 0;

  // Initialize expression hashes for memoize of closures
  root->hash();

  if (verbose) std::cerr << "Running " << jobs << " jobs at a time." << std::endl;
  db.prepare();
  std::shared_ptr<Value> output;
  queue.queue.emplace(root.get(), nullptr, std::unique_ptr<Receiver>(new Output(&output)));
  do { queue.run(); } while (jobtable.wait(queue));

  std::vector<std::shared_ptr<Value> > outputs;
  outputs.reserve(targets.size());
  Binding *iter = reinterpret_cast<Closure*>(output.get())->binding.get();
  for (size_t i = 0; i < targets.size(); ++i) {
    outputs.emplace_back(iter->future[0].output());
    iter = iter->next.get();
  }

  for (size_t i = 0; i < targets.size(); ++i) {
    Value *v = outputs[targets.size()-1-i].get();
    std::cout << targets[i] << " = ";
    if (v) std::cout << v; else std::cout << "MISSING FUTURE";
    std::cout << std::endl;
  }

  //std::cerr << "Computed in " << Action::next_serial << " steps." << std::endl;
  db.clean(args["verbose"]);
  return 0;
}
