#ifndef VERSION
#include "version.h"
#endif
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define VERSION_STR TOSTRING(VERSION)

#include <iostream>
#include <thread>
#include <sstream>
#include <random>
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
#include "hash.h"

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

static void describe_human(const std::vector<JobReflection> &jobs, bool debug) {
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
      << "  Stdin:     " << job.stdin << std::endl
      << "Inputs:" << std::endl;
    for (auto &in : job.inputs)
      std::cout << "  " << in.hash << " " << in.path << std::endl;
    std::cout
      << "Outputs:" << std::endl;
    for (auto &out : job.outputs)
      std::cout << "  " << out.hash << " " << out.path << std::endl;
    if (debug) {
      // std::replace(job.stack.begin(), job.stack.end(), '\n', "\n  ");
      std::cout << "Stack:" << std::endl << "  " << job.stack;
    }
  }
}

static void escape(const std::string &x) {
  std::cout << "'";
  size_t j;
  for (size_t i = 0; i != std::string::npos; i = j) {
    j = x.find('\'', i);
    if (j != std::string::npos) {
      std::cout.write(x.data()+i, j-i);
      std::cout << "'\\''";
      ++j;
    } else {
      std::cout.write(x.data()+i, x.size()-i);
    }
  }
  std::cout << "'";
}

static void describe_shell(const std::vector<JobReflection> &jobs, bool debug) {
  std::cout << "#! /bin/sh -ex" << std::endl;

  for (auto &job : jobs) {
    std::cout << std::endl << "# Wake job " << job.job << ":" << std::endl;
    std::cout << "cd ";
    escape(get_workspace());
    std::cout << std::endl;
    if (job.directory != ".") {
      std::cout << "cd ";
      escape(job.directory);
      std::cout << std::endl;
    }
    std::cout << "env -i \\" << std::endl;
    for (auto &env : job.environment) {
      std::cout << "\t";
      escape(env);
      std::cout << " \\" << std::endl;
    }
    for (auto &arg : job.commandline) {
      escape(arg);
      std::cout << " \\" << std::endl << '\t';
    }
    std::cout << "< ";
    escape(job.stdin);
    std::cout
      << std::endl << std::endl
      << "# When wake ran this command:" << std::endl
      << "#   Built:     " << job.time << std::endl
      << "#   Runtime:   " << job.runtime << std::endl
      << "#   Status:    " << job.status << std::endl
      << "# Inputs:" << std::endl;
    for (auto &in : job.inputs)
      std::cout << "#   " << in.hash << " " << in.path << std::endl;
    std::cout
      << "# Outputs:" << std::endl;
    for (auto &out : job.outputs)
      std::cout << "#   " << out.hash << " " << out.path << std::endl;
  }
}

void describe(const std::vector<JobReflection> &jobs, bool rerun, bool debug) {
  if (rerun) {
    describe_shell(jobs, debug);
  } else {
    describe_human(jobs, debug);
  }
}

int main(int argc, const char **argv) {
  const char *usage = "Usage: wake [OPTION] [--] [ADDED EXPRESSION]";
  argagg::parser argparser {{
    { "help", {"-h", "--help"},
      "shows this help message", 0},
    { "add", {"-a", "--add"},
      "add command-line as a persistent build target", 0},
    { "subtract", {"-s", "--subtract"},
      "remove the index-specified persistent build target", 1},
    { "list", {"-l", "--list"},
      "list the registered persistent builds targets", 0},
    { "output", {"-o", "--output"},
      "query which jobs have the specified output file", 1},
    { "input", {"-i", "--input"},
      "query which jobs have the specified input file", 1},
    { "rerun", {"-r", "--rerun"},
      "output job descriptions as a runable shell script", 0},
    { "jobs", {"-j", "--jobs"},
      "set the number of concurrent jobs to run", 1},
    { "verbose", {"-v", "--verbose"},
      "output stdout of jobs", 0},
    { "version", {"--version"},
      "report the version of wake", 0},
    { "quiet", {"-q", "--quiet"},
      "surpress output of job commands and stderr", 0},
    { "debug", {"-d", "--debug"},
      "simulate a stack for exceptions and print closures", 0},
    { "debug-db", {"--debug-db"},
      "print all database query execution to stderr", 0},
    { "parse", {"-p", "--parse"},
      "parse wake files and print the AST", 0},
    { "typecheck", {"-t", "--typecheck"},
      "type-check wake files and print the typed AST", 0},
    { "check", {"-c", "--check"},
      "rebuild all outputs to check for build repeatability", 0},
    { "globals", {"-g", "--globals"},
      "print out all global variables", 0},
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

  if (args["version"]) {
    std::cerr << "wake " << VERSION_STR << std::endl;
    return 0;
  }

  int jobs = args["jobs"].as<int>(std::thread::hardware_concurrency());
  bool verbose = args["verbose"];
  bool quiet = args["quiet"];
  bool rerun = args["rerun"];
  bool check = args["check"];
  bool debugdb = args["debug-db"];
  bool debug = args["debug"];
  queue.stack_trace = debug;

  bool nodb = args["init"];
  bool noparse = nodb ||
    args["subtract"] || args["list"] ||
    args["output"] || args["input"];
  bool notype = noparse || args["parse"];
  bool noexecute = notype ||
    args["add"] || args["typecheck"] || args["globals"];

  // seed the keyed hash function
  {
    std::random_device rd;
    std::uniform_int_distribution<uint64_t> dist;
    sip_key[0] = dist(rd);
    sip_key[1] = dist(rd);
  }

  if (jobs < 1) {
    std::cerr << "Cannot run with less than 1 jobs!" << std::endl;
    return 1;
  }

  if (quiet && verbose) {
    std::cerr << "Cannot be both quiet and verbose!" << std::endl;
    return 1;
  }

  if (noparse && !args.pos.empty()) {
    std::cerr << "Unexpected positional arguments on the command-line!" << std::endl;
    return 1;
  }

  std::string prefix;
  if (args["init"]) {
    std::string dir = args["init"];
    if (!make_workspace(dir)) {
      std::cerr << "Unable to initialize a workspace in " << dir << std::endl;
      return 1;
    }
  } else if (!chdir_workspace(prefix)) {
    std::cerr << "Unable to locate wake.db in any parent directory." << std::endl;
    return 1;
  }

  if (nodb) return 0;

  Database db(debugdb);
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

  if (args["subtract"]) {
    int victim = args["subtract"];
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

  if (args["input"]) {
    std::string input = args["input"];
    describe(db.explain(prefix + input, 1), rerun, debug);
  }

  if (args["output"]) {
    std::string output = args["output"];
    describe(db.explain(prefix + output, 2), rerun, debug);
  }

  if (noparse) return 0;

  bool ok = true;
  auto all_sources(find_all_sources());

  // Read all wake build files
  std::unique_ptr<Top> top(new Top);
  for (auto i : sources(all_sources, ".", "(.*/)?[^/]+\\.wake")) {
    if (verbose && queue.stack_trace)
      std::cerr << "Parsing " << i << std::endl;
    Lexer lex(i->value.c_str());
    parse_top(*top.get(), lex);
    if (lex.fail) ok = false;
  }

  std::vector<std::string> globals;
  if (args["globals"])
    for (auto &g : top->globals)
      globals.push_back(g.first);

  // Read all wake targets
  std::vector<std::string> target_names;
  Expr *body = new Lambda(LOCATION, "_", new Literal(LOCATION, "top"));
  for (size_t i = 0; i < targets.size() + globals.size(); ++i) {
    body = new Lambda(LOCATION, "_", body);
    target_names.emplace_back("<target-" + std::to_string(i) + "-expression>");
  }
  TypeVar *types = &body->typeVar;
  for (size_t i = 0; i < targets.size(); ++i) {
    Lexer lex(targets[i], target_names[i].c_str());
    body = new App(LOCATION, body, parse_command(lex));
    if (lex.fail) ok = false;
  }
  for (auto &g : globals)
    body = new App(LOCATION, body, new VarRef(LOCATION, g));

  top->body = std::unique_ptr<Expr>(body);

  /* Primitives */
  JobTable jobtable(&db, jobs, verbose, quiet, check);
  PrimMap pmap;
  prim_register_string(pmap, VERSION_STR);
  prim_register_vector(pmap);
  prim_register_integer(pmap);
  prim_register_double(pmap);
  prim_register_exception(pmap);
  prim_register_regexp(pmap);
  prim_register_json(pmap);
  prim_register_job(&jobtable, pmap);
  prim_register_sources(&all_sources, pmap);

  if (args["parse"]) std::cout << top.get();

  if (notype) return ok?0:1;
  std::unique_ptr<Expr> root = bind_refs(std::move(top), pmap);
  if (!root) ok = false;

  if (!Boolean) {
    std::cerr << "Primitive data type Boolean not defined." << std::endl;
    ok = false;
  }

  if (!Order) {
    std::cerr << "Primitive data type Order not defined." << std::endl;
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

  if (!Unit) {
    std::cerr << "Primitive data type Unit not defined." << std::endl;
    ok = false;
  }

  if (!JValue) {
    std::cerr << "Primitive data type JValue not defined." << std::endl;
    ok = false;
  }

  if (!ok) {
    if (args["add"]) std::cerr << ">>> Expression not added to the active target list <<<" << std::endl;
    std::cerr << ">>> Aborting without execution <<<" << std::endl;
    return 1;
  }

  if (args["typecheck"]) std::cout << root.get();
  if (args["globals"]) {
    for (auto &g : globals) {
      Expr *e = root.get();
      while (e && e->type == DefBinding::type) {
        DefBinding *d = reinterpret_cast<DefBinding*>(e);
        e = d->body.get();
        auto i = d->order.find(g);
        if (i != d->order.end()) {
          int idx = i->second;
          Expr *v = idx < (int)d->val.size() ? d->val[idx].get() : d->fun[idx-d->val.size()].get();
          std::cout << g << ": ";
          v->typeVar.format(std::cout, v->typeVar);
          std::cout << " = <" << v->location << ">" << std::endl;
        }
      }
    }
  }

  if (args["add"]) {
    db.add_target(targets.back());
    if (args["verbose"]) std::cout << "Added target " << (targets.size()-1) << " = " << targets.back() << std::endl;
  }

  // Exit without execution for these arguments
  if (noexecute) return 0;

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

  bool pass = true;
  for (size_t i = 0; i < targets.size(); ++i) {
    Value *v = outputs[targets.size()-1-i].get();
    std::cout << targets[i] << ": ";
    (*types)[0].format(std::cout, body->typeVar);
    types = &(*types)[1];
    std::cout << " = ";
    if (v) {
      if (v->type == Exception::type) pass = false;
      if (args["debug"]) {
        v->format(std::cout, -1);
      } else {
        std::cout << v << std::endl;
      }
    } else {
      pass = false;
      std::cout << "MISSING FUTURE" << std::endl;
    }
  }

  //std::cerr << "Computed in " << Action::next_serial << " steps." << std::endl;
  db.clean();
  return pass?0:1;
}
