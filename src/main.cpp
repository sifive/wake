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
#include "argagg.h"

static WorkQueue queue;

struct Output : public Receiver {
  std::shared_ptr<Value> *save;
  Output(std::shared_ptr<Value> *save_) : save(save_) { }
  void receive(WorkQueue &queue, std::shared_ptr<Value> &&value);
};

void Output::receive(WorkQueue &queue, std::shared_ptr<Value> &&value) {
  (void)queue; // we're done; no following actions to take!
  *save = std::move(value);
}

void describe(const std::vector<JobReflection> &jobs) {
  for (auto &job : jobs) {
    std::cout
      << "Job " << job.job << ":" << std::endl
      << "  Command-line:";
    for (auto &arg : job.commandline) std::cout << " " << arg;
    std::cout
      << std::endl
      << "  Environment:" << std::endl;
    for (auto &env : job.environment)
      std::cout << "    " << env << std::endl;
    std::cout
      << "  Directory: " << job.directory << std::endl
      << "  Built:     " << job.time << std::endl
      << "  Runtime:   " << job.runtime << std::endl
      << "  Status:    " << job.status << std::endl
      << "  Stdin:" << job.stdin << std::endl
      << "Inputs:" << std::endl;
    for (auto &in : job.inputs)
      std::cout << "  " << in.hash << " " << in.path << std::endl;
    std::cout
      << "Outputs:" << std::endl;
    for (auto &out : job.outputs)
      std::cout << "  " << out.hash << " " << out.path << std::endl;
  }
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
    { "output", {"-o", "--output"},
      "query which jobs have this output", 1},
    { "input", {"-i", "--input"},
      "query which jobs have this input", 1},
    { "jobs", {"-j", "--jobs"},
      "number of concurrent jobs to run", 1},
    { "verbose", {"-v", "--verbose"},
      "output progress information", 0},
    { "quiet", {"-q", "--quiet"},
      "quiet operation", 0},
    { "debug", {"-d", "--debug"},
      "simulate a stack for exceptions", 0},
    { "parse", {"-p", "--parse"},
      "parse wake files and print the AST", 0},
    { "typecheck", {"-t", "--typecheck"},
      "type-check wake files and print the typed AST", 0},
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
  bool quiet = args["quiet"];
  queue.stack_trace = args["debug"];

  if (quiet && verbose) {
    std::cerr << "Cannot be both quiet and verbose!" << std::endl;
    return 1;
  }

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

  if (args["add"] && args.pos.empty()) {
    std::cerr << "You must specify positional arguments to use for the wake bulid target" << std::endl;
    return 1;
  } else {
    if (!args.pos.empty()) {
      std::stringstream expr;
      bool first = true;
      for (auto i : args.pos) {
        if (!first) expr << " ";
        first = false;
        expr << i;
      }
      targets.push_back(expr.str());
    }
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
  TypeVar *types = &body->typeVar;
  for (size_t i = 0; i < targets.size(); ++i) {
    Lexer lex(targets[i], target_names[i].c_str());
    body = new App(LOCATION, body, parse_command(lex));
    if (lex.fail) ok = false;
  }
  top->body = std::unique_ptr<Expr>(body);

  /* Primitives */
  JobTable jobtable(&db, jobs, verbose, quiet);
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

  if (!Boolean) {
    std::cerr << "Primitive data type Boolean not defined." << std::endl;
    ok = false;
  }

  if (!List) {
    std::cerr << "Primitive data type List not defined." << std::endl;
    ok = false;
  }

  if (!Pair) {
    std::cerr << "Primitive data type Pair not defined." << std::endl;
    ok = false;
  }

  if (!ok) {
    if (args["add"]) std::cerr << ">>> Expression not added to the active target list <<<" << std::endl;
    std::cerr << ">>> Aborting without execution <<<" << std::endl;
    return 1;
  }

  if (args["typecheck"]) std::cout << root.get();

  if (args["add"]) {
    db.add_target(targets.back());
    if (args["verbose"]) std::cout << "Added target " << (targets.size()-1) << " = " << targets.back() << std::endl;
  }
  if (args["input"]) describe(db.explain(args["input"], 1));
  if (args["output"]) describe(db.explain(args["output"], 2));
  if (args["parse"]) return 0;
  if (args["list"]) return 0;
  if (args["input"]) return 0;
  if (args["output"]) return 0;

  // Initialize expression hashes for memoize of closures
  root->hash();

  if (verbose) std::cerr << "Running " << jobs << " jobs at a time." << std::endl;
  db.prepare();
  std::shared_ptr<Value> output;
  queue.emplace(root.get(), nullptr, std::unique_ptr<Receiver>(new Output(&output)));
  do { queue.run(); } while (jobtable.wait(queue));

  std::vector<std::shared_ptr<Value> > outputs;
  outputs.reserve(targets.size());
  Binding *iter = reinterpret_cast<Closure*>(output.get())->binding.get();
  for (size_t i = 0; i < targets.size(); ++i) {
    outputs.emplace_back(iter->future[0].value);
    iter = iter->next.get();
  }

  for (size_t i = 0; i < targets.size(); ++i) {
    Value *v = outputs[targets.size()-1-i].get();
    std::cout << targets[i] << ": " << (*types)[0] << " = ";
    types = &(*types)[1];
    if (v) {
      if (verbose) {
        v->format(std::cout, -1);
      } else {
        std::cout << v << std::endl;
      }
    } else {
      std::cout << "MISSING FUTURE" << std::endl;
    }
  }

  //std::cerr << "Computed in " << Action::next_serial << " steps." << std::endl;
  db.clean(args["verbose"]);
  return 0;
}
