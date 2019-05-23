/*
 * Copyright 2019 SiFive, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You should have received a copy of LICENSE.Apache2 along with
 * this software. If not, you may obtain a copy at
 *
 *    https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

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
#include <inttypes.h>
#include <stdlib.h>
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
#include "hash.h"
#include "status.h"
#include "gopt.h"

#define SHORT_HASH 8

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

static void indent(const std::string& tab, const std::string& body) {
  size_t i, j;
  for (i = 0; (j = body.find('\n', i)) != std::string::npos; i = j+1) {
    std::cout << "\n" << tab;
    std::cout.write(body.data()+i, j-i);
  }
  std::cout.write(body.data()+i, body.size()-i);
  std::cout << std::endl;
}

static void describe_human(const std::vector<JobReflection> &jobs, bool debug, bool verbose) {
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
      << "  Runtime:   " << job.usage.runtime << std::endl
      << "  CPUtime:   " << job.usage.cputime << std::endl
      << "  Mem bytes: " << job.usage.membytes << std::endl
      << "  In  bytes: " << job.usage.ibytes << std::endl
      << "  Out bytes: " << job.usage.obytes << std::endl
      << "  Status:    " << job.usage.status << std::endl
      << "  Stdin:     " << job.stdin << std::endl;
    if (verbose) {
      std::cout << "Visible:" << std::endl;
      for (auto &in : job.visible)
        std::cout << "  " << in.hash.substr(0, verbose?std::string::npos:SHORT_HASH)
                  << " " << in.path << std::endl;
    }
    std::cout << "Inputs:" << std::endl;
    for (auto &in : job.inputs)
      std::cout << "  " << in.hash.substr(0, verbose?std::string::npos:SHORT_HASH)
                << " " << in.path << std::endl;
    std::cout << "Outputs:" << std::endl;
    for (auto &out : job.outputs)
      std::cout << "  " << out.hash.substr(0, verbose?std::string::npos:SHORT_HASH)
                << " " << out.path << std::endl;
    if (debug) {
      std::cout << "Stack:";
      indent("  ", job.stack);
    }
    if (!job.stdout.empty()) {
      std::cout << "Stdout:";
      indent("  ", job.stdout);
    }
    if (!job.stderr.empty()) {
      std::cout << "Stderr:";
      indent("  ", job.stderr);
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

static void describe_shell(const std::vector<JobReflection> &jobs, bool debug, bool verbose) {
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
      << "#   Runtime:   " << job.usage.runtime << std::endl
      << "#   CPUtime:   " << job.usage.cputime << std::endl
      << "#   Mem bytes: " << job.usage.membytes << std::endl
      << "#   In  bytes: " << job.usage.ibytes << std::endl
      << "#   Out bytes: " << job.usage.obytes << std::endl
      << "#   Status:    " << job.usage.status << std::endl;
    if (verbose) {
      std::cout << "# Visible:" << std::endl;
      for (auto &in : job.visible)
        std::cout << "#  " << in.hash.substr(0, verbose?std::string::npos:SHORT_HASH)
                  << " " << in.path << std::endl;
    }
    std::cout
      << "# Inputs:" << std::endl;
    for (auto &in : job.inputs)
      std::cout << "#  " << in.hash.substr(0, verbose?std::string::npos:SHORT_HASH)
                << " " << in.path << std::endl;
    std::cout << "# Outputs:" << std::endl;
    for (auto &out : job.outputs)
      std::cout << "#  " << out.hash.substr(0, verbose?std::string::npos:SHORT_HASH)
                << " " << out.path << std::endl;
    if (debug) {
      std::cout << "# Stack:";
      indent("#   ", job.stack);
    }
    if (!job.stdout.empty()) {
      std::cout << "Stdout:";
      indent("#   ", job.stdout);
    }
    if (!job.stderr.empty()) {
      std::cout << "Stderr:";
      indent("#   ", job.stderr);
    }
  }
}

void describe(const std::vector<JobReflection> &jobs, bool script, bool debug, bool verbose) {
  if (script) {
    describe_shell(jobs, debug, verbose);
  } else {
    describe_human(jobs, debug, verbose);
  }
}

void print_help(const char *argv0) {
  std::cout << std::endl
    << "Usage: " << argv0 << " [-cdghioqsv] [-j NUM] [--] [arg0 ...]" << std::endl
    << std::endl
    << "  Flags affecting build execution:" << std::endl
    << "    --jobs=NUM -jNUM Schedule local job execution to use <= NUM CPU-bound tasks" << std::endl
    << "    --check    -c    Rerun all jobs and confirm their output is reproducible"    << std::endl
    << "    --verbose  -v    Report job standard output and hash progress"               << std::endl
    << "    --debug    -d    Report stack frame information for exceptions and closures" << std::endl
    << "    --quiet    -q    Surpress report of launched jobs and final expressions"     << std::endl
    << "    --no-tty         Surpress interactive build progress interface"              << std::endl
    << "    --no-wait        Do not wait to obtain database lock; fail immediately"      << std::endl
    << "    --no-workspace   Do not open a database or scan for sources files"           << std::endl
    << std::endl
    << "  Database introspection:" << std::endl
    << "    --input  -i FILE Report recorded meta-data for jobs which read FILES"        << std::endl
    << "    --output -o FILE Report recorded meta-data for jobs which wrote FILES"       << std::endl
    << "    --verbose  -v    Report recorded standard output and error of matching jobs" << std::endl
    << "    --debug    -d    Report recorded stack frame of matching jobs"               << std::endl
    << "    --script   -s    Format reported jobs as an executable shell script"         << std::endl
    << std::endl
    << "  Persistent tasks:" << std::endl
    << "    --init=DIR       Create or replace a wake.db in the specified directory"     << std::endl
    << "    --list-tasks     List all database-saved tasks which run on every build"     << std::endl
    << "    --add-task EXPR+ Add a persistent task to the database for future builds"    << std::endl
    << "    --remove-task=N  Remove persistent task #N from the database"                << std::endl
    << std::endl
    << "  Help functions:" << std::endl
    << "    --version        Print the version of wake on standard output"               << std::endl
    << "    --globals  -g    Print all global variables available to the command-line"   << std::endl
    << "    --help     -h    Print this help message and exit"                           << std::endl
    << std::endl;
    // debug-db, stop-after-* are secret undocumented options
}

static struct option *arg(struct option opts[], const char *name) {
  for (int i = 0; !(opts[i].flags & GOPT_LAST); ++i)
    if (!strcmp(opts[i].long_name, name))
      return opts + i;

  std::cerr << "Wake option parser bug: " << name << std::endl;
  exit(1);
}

int main(int argc, char **argv) {
  struct option options[] {
    { 'j', "jobs",                  GOPT_ARGUMENT_REQUIRED  | GOPT_ARGUMENT_NO_HYPHEN },
    { 'c', "check",                 GOPT_ARGUMENT_FORBIDDEN },
    { 'v', "verbose",               GOPT_ARGUMENT_FORBIDDEN | GOPT_REPEATABLE },
    { 'd', "debug",                 GOPT_ARGUMENT_FORBIDDEN },
    { 'q', "quiet",                 GOPT_ARGUMENT_FORBIDDEN },
    { 0,   "no-wait",               GOPT_ARGUMENT_FORBIDDEN },
    { 0,   "no-workspace",          GOPT_ARGUMENT_FORBIDDEN },
    { 0,   "no-tty",                GOPT_ARGUMENT_FORBIDDEN },
    { 'i', "input",                 GOPT_ARGUMENT_FORBIDDEN },
    { 'o', "output",                GOPT_ARGUMENT_FORBIDDEN },
    { 's', "script",                GOPT_ARGUMENT_FORBIDDEN },
    { 0,   "init",                  GOPT_ARGUMENT_REQUIRED  },
    { 0,   "list-tasks",            GOPT_ARGUMENT_FORBIDDEN },
    { 0,   "add-task",              GOPT_ARGUMENT_FORBIDDEN },
    { 0,   "remove-task",           GOPT_ARGUMENT_REQUIRED  | GOPT_ARGUMENT_NO_HYPHEN },
    { 0,   "version",               GOPT_ARGUMENT_FORBIDDEN },
    { 'g', "globals",               GOPT_ARGUMENT_FORBIDDEN },
    { 'h', "help",                  GOPT_ARGUMENT_FORBIDDEN },
    { 0,   "debug-db",              GOPT_ARGUMENT_FORBIDDEN },
    { 0,   "stop-after-parse",      GOPT_ARGUMENT_FORBIDDEN },
    { 0,   "stop-after-type-check", GOPT_ARGUMENT_FORBIDDEN },
    { 0,   0,                       GOPT_LAST}};

  argc = gopt(argv, options);
  gopt_errors(argv[0], options);

  bool check   = arg(options, "check"   )->count;
  bool verbose = arg(options, "verbose" )->count;
  bool debug   = arg(options, "debug"   )->count;
  bool quiet   = arg(options, "quiet"   )->count;
  bool wait    =!arg(options, "no-wait" )->count;
  bool workspace=!arg(options, "no-workspace")->count;
  bool tty     =!arg(options, "no-tty"  )->count;
  bool input   = arg(options, "input"   )->count;
  bool output  = arg(options, "output"  )->count;
  bool script  = arg(options, "script"  )->count;
  bool list    = arg(options, "list-tasks")->count;
  bool add     = arg(options, "add-task")->count;
  bool version = arg(options, "version" )->count;
  bool global  = arg(options, "globals" )->count;
  bool help    = arg(options, "help"    )->count;
  bool debugdb = arg(options, "debug-db")->count;
  bool parse   = arg(options, "stop-after-parse")->count;
  bool tcheck  = arg(options, "stop-after-type-check")->count;

  const char *jobs   = arg(options, "jobs"  )->argument;
  const char *init   = arg(options, "init"  )->argument;
  const char *remove = arg(options, "remove-task")->argument;

  queue.stack_trace = debug;

  if (help) {
    print_help(argv[0]);
    return 0;
  }

  if (version) {
    std::cout << "wake " << VERSION_STR << std::endl;
    return 0;
  }

  if (quiet && verbose) {
    std::cerr << "Cannot specify both -v and -q!" << std::endl;
    return 1;
  }

  term_init(tty);

  int njobs = std::thread::hardware_concurrency();
  if (jobs) {
    char *tail;
    njobs = strtol(jobs, &tail, 0);
    if (*tail || njobs < 1) {
      std::cerr << "Cannot run with " << jobs << " jobs!" << std::endl;
      return 1;
    }
  }

  bool nodb = init;
  bool noparse = nodb || remove || list || output || input;
  bool notype = noparse || parse;
  bool noexecute = notype || add || tcheck || global;

  if (noparse && argc < 1) {
    std::cerr << "Unexpected positional arguments on the command-line!" << std::endl;
    return 1;
  }

  std::string prefix;
  if (init) {
    if (!make_workspace(init)) {
      std::cerr << "Unable to initialize a workspace in " << init << std::endl;
      return 1;
    }
  } else if (workspace && !chdir_workspace(prefix)) {
    std::cerr << "Unable to locate wake.db in any parent directory." << std::endl;
    return 1;
  }

  if (nodb) return 0;

  Database db(debugdb);
  std::string fail = db.open(wait, !workspace);
  if (!fail.empty()) {
    std::cerr << "Failed to open wake.db: " << fail << std::endl;
    return 1;
  }

  // seed the keyed hash function
  {
    std::random_device rd;
    std::uniform_int_distribution<uint64_t> dist;
    sip_key[0] = dist(rd);
    sip_key[1] = dist(rd);
    db.entropy(&sip_key[0], 2);
  }

  std::vector<std::string> targets = db.get_targets();
  if (list) {
    std::cout << "Active wake targets:" << std::endl;
    int j = 0;
    for (auto &i : targets)
      std::cout << "  " << j++ << " = " << i << std::endl;
  }

  if (remove) {
    char *tail;
    int victim = strtol(remove, &tail, 0);
    if (*tail || victim < 0 || victim >= (int)targets.size()) {
      std::cerr << "Could not remove target " << remove << "; there are only " << targets.size() << std::endl;
      return 1;
    }
    if (verbose) std::cout << "Removed target " << victim << " = " << targets[victim] << std::endl;
    db.del_target(targets[victim]);
    targets.erase(targets.begin() + victim);
  }

  if (add && argc < 1) {
    std::cerr << "You must specify positional arguments to use for the wake bulid target" << std::endl;
    return 1;
  } else {
    if (argc > 1) {
      std::stringstream expr;
      for (int i = 1; i < argc; ++i) {
        if (i != 1) expr << " ";
        expr << argv[i];
      }
      targets.push_back(expr.str());
    }
  }

  if (input) {
    for (int i = 1; i < argc; ++i) {
      describe(db.explain(make_canonical(prefix + argv[i]), 1, verbose), script, debug, verbose);
    }
  }

  if (output) {
    for (int i = 1; i < argc; ++i) {
      describe(db.explain(make_canonical(prefix + argv[i]), 2, verbose), script, debug, verbose);
    }
  }

  if (noparse) return 0;

  bool ok = true;
  auto all_sources(find_all_sources(ok, workspace));
  if (!ok) std::cerr << "Source file enumeration failed" << std::endl;

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
  if (global)
    for (auto &g : top->globals)
      globals.push_back(g.first);

  // Read all wake targets
  std::vector<std::string> target_names;
  Expr *body = new Lambda(LOCATION, "_", new Literal(LOCATION, "top"));
  for (size_t i = 0; i < targets.size() + globals.size(); ++i) {
    body = new Lambda(LOCATION, "_", body);
    target_names.emplace_back("<target-" + std::to_string(i) + ">");
  }
  if (argc > 1) target_names.back() = "<command-line>";
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
  JobTable jobtable(&db, njobs, verbose, quiet, check);
  StringInfo info(verbose, debug, quiet, VERSION_STR);
  PrimMap pmap;
  prim_register_string(pmap, &info);
  prim_register_vector(pmap);
  prim_register_integer(pmap);
  prim_register_double(pmap);
  prim_register_exception(pmap);
  prim_register_regexp(pmap);
  prim_register_json(pmap);
  prim_register_job(&jobtable, pmap);
  prim_register_sources(&all_sources, pmap);

  if (parse) std::cout << top.get();

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

  if (!Result) {
    std::cerr << "Primitive data type Result not defined." << std::endl;
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
    if (add) std::cerr << ">>> Expression not added to the active target list <<<" << std::endl;
    std::cerr << ">>> Aborting without execution <<<" << std::endl;
    return 1;
  }

  if (tcheck) std::cout << root.get();
  for (auto &g : globals) {
    Expr *e = root.get();
    while (e && e->type == &DefBinding::type) {
      DefBinding *d = reinterpret_cast<DefBinding*>(e);
      e = d->body.get();
      auto i = d->order.find(g);
      if (i != d->order.end()) {
        int idx = i->second;
        Expr *v = idx < (int)d->val.size() ? d->val[idx].get() : d->fun[idx-d->val.size()].get();
        std::cout << g << ": ";
        v->typeVar.format(std::cout, v->typeVar);
        std::cout << " = <" << v->location.file() << ">" << std::endl;
      }
    }
  }

  if (add) {
    db.add_target(targets.back());
    if (verbose) std::cout << "Added target " << (targets.size()-1) << " = " << targets.back() << std::endl;
  }

  // Exit without execution for these arguments
  if (noexecute) return 0;

  // Initialize expression hashes for memoize of closures
  root->hash();

  db.prepare();
  std::shared_ptr<Value> value;
  queue.emplace(root.get(), nullptr, std::unique_ptr<Receiver>(new Output(&value)));

  // Flush buffered IO before we enter the main loop (which uses unbuffered IO exclusively)
  std::cout << std::flush;
  std::cerr << std::flush;
  fflush(stdout);
  fflush(stderr);

  queue.abort = false;

  status_init();
  do { queue.run(); } while (!queue.abort && jobtable.wait(queue));
  status_finish();

  bool pass = !queue.abort;
  if (JobTable::exit_now()) {
    std::cerr << "Early termination requested" << std::endl;
    pass = false;
  } else if (pass) {
    std::vector<std::shared_ptr<Value> > outputs;
    outputs.reserve(targets.size());
    Binding *iter = reinterpret_cast<Closure*>(value.get())->binding.get();
    for (size_t i = 0; i < targets.size(); ++i) {
      outputs.emplace_back(iter->future[0].value);
      iter = iter->next.get();
    }

    for (size_t i = 0; i < targets.size(); ++i) {
      Value *v = outputs[targets.size()-1-i].get();
      if (verbose) {
        std::cout << targets[i] << ": ";
        (*types)[0].format(std::cout, body->typeVar);
        types = &(*types)[1];
        std::cout << " = ";
      }
      if (!quiet) {
        Value::format(std::cout, v, debug, verbose?0:-1);
        if (v && v->type == &Closure::type)
          std::cout << ", " << term_red() << "AN UNEVALUATED FUNCTION" << term_normal();
        std::cout << std::endl;
      }
      if (!v) {
        pass = false;
      } else if (v->type == &Data::type) {
        Data *d = reinterpret_cast<Data*>(v);
        if (d->cons->index == 1) pass = false;
      }
    }
  }

  db.clean();
  return pass?0:1;
}
