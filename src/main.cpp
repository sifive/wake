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
#include <sstream>
#include <random>
#include <set>
#include <inttypes.h>
#include <stdlib.h>
#include "parser.h"
#include "bind.h"
#include "symbol.h"
#include "value.h"
#include "expr.h"
#include "job.h"
#include "sources.h"
#include "database.h"
#include "status.h"
#include "gopt.h"
#include "runtime.h"
#include "shell.h"
#include "markup.h"
#include "describe.h"
#include "profile.h"
#include "ssa.h"

void print_help(const char *argv0) {
  std::cout << std::endl
    << "Usage: " << argv0 << " [OPTIONS] [target] [target options ...]" << std::endl
    << std::endl
    << "  Flags affecting build execution:" << std::endl
    << "    -p PERCENT       Schedule local jobs for <= PERCENT of system (default 90)"  << std::endl
    << "    --check    -c    Rerun all jobs and confirm their output is reproducible"    << std::endl
    << "    --verbose  -v    Report hash progress and result expression types"           << std::endl
    << "    --debug    -d    Report stack frame information for exceptions and closures" << std::endl
    << "    --quiet    -q    Surpress report of launched jobs and final expressions"     << std::endl
    << "    --no-tty         Surpress interactive build progress interface"              << std::endl
    << "    --no-wait        Do not wait to obtain database lock; fail immediately"      << std::endl
    << "    --no-workspace   Do not open a database or scan for sources files"           << std::endl
    << "    --heap-factor X  Heap-size is X * live data after the last GC (default 4.0)" << std::endl
    << "    --profile-heap   Report memory consumption on every garbage collection"      << std::endl
    << "    --profile FILE   Report runtime breakdown by stack trace to HTML/JSON file"  << std::endl
    << "    --in      PKG    Evaluate command-line in package PKG (default current dir)" << std::endl
    << "    --exec -x EXPR   Execute expression EXPR instead of a target function"       << std::endl
    << std::endl
    << "  Database commands:" << std::endl
    << "    --init      DIR  Create or replace a wake.db in the specified directory"     << std::endl
    << "    --input  -i FILE Report recorded meta-data for jobs which read FILES"        << std::endl
    << "    --output -o FILE Report recorded meta-data for jobs which wrote FILES"       << std::endl
    << "    --last     -l    Report recorded meta-data for all jobs run by last build"   << std::endl
    << "    --failed   -f    Report recorded meta-data for jobs which failed last build" << std::endl
    << "    --verbose  -v    Report recorded standard output and error of matching jobs" << std::endl
    << "    --debug    -d    Report recorded stack frame of matching jobs"               << std::endl
    << "    --script   -s    Format reported jobs as an executable shell script"         << std::endl
    << std::endl
    << "  Help functions:" << std::endl
    << "    --version        Print the version of wake on standard output"               << std::endl
    << "    --html           Print all wake source files as cross-referenced HTML"       << std::endl
    << "    --globals -g     Print global symbols made available to all wake files"      << std::endl
    << "    --exports -e     Print symbols exported by the selected package (see --in)"  << std::endl
    << "    --help    -h     Print this help message and exit"                           << std::endl
    << std::endl;
    // debug-db, no-optimize, stop-after-* are secret undocumented options
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
    { 'p', "percent",               GOPT_ARGUMENT_REQUIRED  | GOPT_ARGUMENT_NO_HYPHEN },
    { 'c', "check",                 GOPT_ARGUMENT_FORBIDDEN },
    { 'v', "verbose",               GOPT_ARGUMENT_FORBIDDEN | GOPT_REPEATABLE },
    { 'd', "debug",                 GOPT_ARGUMENT_FORBIDDEN },
    { 'q', "quiet",                 GOPT_ARGUMENT_FORBIDDEN },
    { 0,   "no-wait",               GOPT_ARGUMENT_FORBIDDEN },
    { 0,   "no-workspace",          GOPT_ARGUMENT_FORBIDDEN },
    { 0,   "no-tty",                GOPT_ARGUMENT_FORBIDDEN },
    { 0,   "heap-factor",           GOPT_ARGUMENT_REQUIRED  | GOPT_ARGUMENT_NO_HYPHEN },
    { 0,   "profile-heap",          GOPT_ARGUMENT_FORBIDDEN | GOPT_REPEATABLE },
    { 0,   "profile",               GOPT_ARGUMENT_REQUIRED  },
    { 0,   "in",                    GOPT_ARGUMENT_REQUIRED  },
    { 'x', "exec",                  GOPT_ARGUMENT_REQUIRED  },
    { 'i', "input",                 GOPT_ARGUMENT_FORBIDDEN },
    { 'o', "output",                GOPT_ARGUMENT_FORBIDDEN },
    { 'l', "last",                  GOPT_ARGUMENT_FORBIDDEN },
    { 'f', "failed",                GOPT_ARGUMENT_FORBIDDEN },
    { 's', "script",                GOPT_ARGUMENT_FORBIDDEN },
    { 0,   "init",                  GOPT_ARGUMENT_REQUIRED  },
    { 0,   "version",               GOPT_ARGUMENT_FORBIDDEN },
    { 'g', "globals",               GOPT_ARGUMENT_FORBIDDEN },
    { 'e', "exports",               GOPT_ARGUMENT_FORBIDDEN },
    { 0,   "html",                  GOPT_ARGUMENT_FORBIDDEN },
    { 'h', "help",                  GOPT_ARGUMENT_FORBIDDEN },
    { 0,   "debug-db",              GOPT_ARGUMENT_FORBIDDEN },
    { 0,   "debug-target",          GOPT_ARGUMENT_REQUIRED  },
    { 0,   "stop-after-parse",      GOPT_ARGUMENT_FORBIDDEN },
    { 0,   "stop-after-type-check", GOPT_ARGUMENT_FORBIDDEN },
    { 0,   "stop-after-ssa",        GOPT_ARGUMENT_FORBIDDEN },
    { 0,   "no-optimize",           GOPT_ARGUMENT_FORBIDDEN },
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
  int  profileh= arg(options, "profile-heap")->count;
  bool input   = arg(options, "input"   )->count;
  bool output  = arg(options, "output"  )->count;
  bool last    = arg(options, "last"    )->count;
  bool failed  = arg(options, "failed"  )->count;
  bool script  = arg(options, "script"  )->count;
  bool version = arg(options, "version" )->count;
  bool html    = arg(options, "html"    )->count;
  bool global  = arg(options, "globals" )->count;
  bool help    = arg(options, "help"    )->count;
  bool debugdb = arg(options, "debug-db")->count;
  bool parse   = arg(options, "stop-after-parse")->count;
  bool tcheck  = arg(options, "stop-after-type-check")->count;
  bool dumpssa = arg(options, "stop-after-ssa")->count;
  bool optim   =!arg(options, "no-optimize")->count;
  bool exports = arg(options, "exports")->count;

  const char *percents= arg(options, "percent")->argument;
  const char *heapf   = arg(options, "heap-factor")->argument;
  const char *profile = arg(options, "profile")->argument;
  const char *init    = arg(options, "init")->argument;
  const char *hash    = arg(options, "debug-target")->argument;
  const char *in      = arg(options, "in")->argument;
  const char *exec    = arg(options, "exec")->argument;

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

  if (profile && !debug) {
    std::cerr << "Cannot profile without stack trace support (-d)!" << std::endl;
    return 1;
  }

  term_init(tty);

  if (!percents) {
    percents = getenv("WAKE_PERCENT");
  }

  double percent = 0.9;
  if (percents) {
    char *tail;
    percent = strtod(percents, &tail);
    percent /= 100.0;
    if (*tail || percent < 0.01 || percent > 0.99) {
      std::cerr << "Cannot run with " << percents << "%  (must be >= 0.01 and <= 0.99)!" << std::endl;
      return 1;
    }
  }

  double heap_factor = 4.0;
  if (heapf) {
    char *tail;
    heap_factor = strtod(heapf, &tail);
    if (*tail || heap_factor < 1.1) {
      std::cerr << "Cannot run with " << heapf << " heap-factor (must be >= 1.1)!" << std::endl;
      return 1;
    }
  }

  // Arguments are forbidden with these options
  bool noargs = init || last || failed || html || global || exports || exec;
  bool targets = argc == 1 && !noargs;

  bool nodb = init;
  bool noparse = nodb || output || input || last || failed;
  bool notype = noparse || parse;
  bool noexecute = notype || html || tcheck || dumpssa || global || exports || targets;

  if (noargs && argc > 1) {
    std::cerr << "Unexpected positional arguments on the command-line!" << std::endl;
    return 1;
  }

  std::string prefix; // "" | .+/
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

  if (last) {
    describe(db.last(verbose), script, debug, verbose);
  }

  if (failed) {
    describe(db.failed(verbose), script, debug, verbose);
  }

  if (noparse) return 0;

  bool ok = true;
  auto wakefiles = find_all_wakefiles(ok, workspace, verbose);
  if (!ok) std::cerr << "Workspace wake file enumeration failed" << std::endl;

  uint64_t target_hash = 0;
  if (hash) {
    char *tail;
    target_hash = strtoull(hash, &tail, 0);
    if (*tail) {
      std::cerr << "Cannot run with debug-target=" << hash << "  (must be a number)!" << std::endl;
      return 1;
    }
  }

  Profile tree;
  Runtime runtime(profile ? &tree : nullptr, profileh, heap_factor, target_hash);
  bool sources = find_all_sources(runtime, workspace);
  if (!sources) std::cerr << "Source file enumeration failed" << std::endl;
  ok &= sources;

  // Select a default package
  int longest_prefix = -1;
  bool warned_conflict = false;

  // Read all wake build files
  Scope::debug = debug;
  std::unique_ptr<Top> top(new Top);
  for (auto &i : wakefiles) {
    if (verbose && debug)
      std::cerr << "Parsing " << i << std::endl;
    Lexer lex(runtime.heap, i.c_str());
    auto package = parse_top(*top, lex);
    if (lex.fail) ok = false;
    // Does this file inform our choice of a default package?
    size_t slash = i.find_last_of('/');
    std::string dir(i, 0, slash==std::string::npos?0:(slash+1)); // "" | .+/
    if (prefix.compare(0, dir.size(), dir) == 0) { // dir = prefix or parent of prefix?
      int dirlen = dir.size();
      if (dirlen > longest_prefix) {
        longest_prefix = dirlen;
        top->def_package = package;
        warned_conflict = false;
      } else if (dirlen == longest_prefix) {
        if (top->def_package != package && !warned_conflict) {
          std::cerr << "Directory " << dir
            << " has wakefiles with both package '" << top->def_package
            << "' and '" << package
            << "'. This prevents default package selection;"
            << " defaulting to package 'wake'." << std::endl;
          top->def_package = "wake";
          warned_conflict = true;
        }
      }
    }
  }

  if (in) {
    auto it = top->packages.find(in);
    if (it == top->packages.end()) {
      std::cerr << "Package '" << in
        << "' selected by --in does not exist!" << std::endl;
      ok = false;
    } else {
      top->def_package = in;
    }
  }

  // No wake files in the path from workspace to the current directory
  if (!top->def_package) top->def_package = "wake";
  if (!flatten_exports(*top)) ok = false;

  std::vector<std::pair<std::string, std::string> > defs;
  std::set<std::string> types;

  if (global) {
    for (auto &g : top->globals.defs)
      defs.emplace_back(g.first, g.second.qualified);
    for (auto &t : top->globals.topics)
      defs.emplace_back("topic " + t.first, "topic " + t.second.qualified);
    for (auto &t : top->globals.types)
      types.insert(t.first);
  }

  if (exports) {
    auto it = top->packages.find(top->def_package);
    if (it != top->packages.end()) {
      for (auto &e : it->second->exports.defs)
        defs.emplace_back(e.first, e.second.qualified);
      for (auto &t : it->second->exports.topics)
        defs.emplace_back("topic " + t.first, "topic " + t.second.qualified);
      for (auto &t : it->second->exports.types)
        types.insert(t.first);
    }
  }

  if (targets) {
    auto it = top->packages.find(top->def_package);
    if (it != top->packages.end()) {
      for (auto &e : it->second->exports.defs)
        defs.emplace_back(e.first, e.second.qualified);
    }
  }

  Expr *body = new Lambda(LOCATION, "_", new Literal(LOCATION, String::literal(runtime.heap, "top"), &String::typeVar));
  for (size_t i = 0; i <= defs.size(); ++i) {
    body = new Lambda(LOCATION, "_", body);
  }

  TypeVar type = body->typeVar;
  std::string command;
  char *none = nullptr;
  char **cmdline = &none;
  if (exec) {
    command = exec;
    Lexer lex(runtime.heap, command, "<execute-argument>");
    body = new App(LOCATION, body, force_use(parse_command(lex)));
    if (lex.fail) ok = false;
  } else if (argc > 1) {
    command = argv[1];
    cmdline = argv+2;
    Lexer lex(runtime.heap, command, "<target-argument>");
    Expr *var = parse_command(lex);
    if (var->type == &VarRef::type) {
      body = new App(LOCATION, body, force_use(
        new App(LOCATION, var, new Prim(LOCATION, "cmdline"))));
    } else {
      std::cerr << "Specified target '" << argv[1] << "' is not a legal identifier" << std::endl;
      ok = false;
    }
  } else {
    body = new App(LOCATION, body, force_use(new VarRef(LOCATION, "Nil@wake")));
  }

  for (auto &d : defs)
    body = new App(LOCATION, body, force_use(new VarRef(LOCATION, d.second)));

  top->body = std::unique_ptr<Expr>(body);

  if (parse) std::cout << top.get();
  if (notype) return ok?0:1;

  /* Primitives */
  JobTable jobtable(&db, percent, verbose, quiet, check, !tty);
  StringInfo info(verbose, debug, quiet, VERSION_STR, make_canonical(prefix), cmdline);
  PrimMap pmap = prim_register_all(&info, &jobtable);

  std::unique_ptr<Expr> root = bind_refs(std::move(top), pmap);
  if (!root) ok = false;
  ok = ok && sums_ok();

  if (!ok) {
    std::cerr << ">>> Aborting without execution <<<" << std::endl;
    return 1;
  }

  if (tcheck) std::cout << root.get();
  if (html) markup_html(std::cout, root.get());

  if (!types.empty()) {
    std::cout << "types";
    for (auto &t : types) {
      std::cout << " ";
      if (t.compare(0, 7, "binary ") == 0) {
        std::cout << t.c_str() + 7;
      } else if (t.compare(0, 6, "unary ") == 0) {
        std::cout << t.c_str() + 6;
      } else {
        std::cout << t.c_str();
      }
    }
    std::cout << std::endl;
  }

  if (targets) std::cout << "Available wake targets:" << std::endl;

  for (auto &g : defs) {
    Expr *e = root.get();
    while (e && e->type == &DefBinding::type) {
      DefBinding *d = static_cast<DefBinding*>(e);
      e = d->body.get();
      auto i = d->order.find(g.second);
      if (i != d->order.end()) {
        int idx = i->second.index;
        Expr *v = idx < (int)d->val.size() ? d->val[idx].get() : d->fun[idx-d->val.size()].get();
        if (targets) {
          if (strcmp(v->typeVar      .getName(), FN))               continue;
          if (strcmp(v->typeVar[0]   .getName(), "List@wake"))      continue;
          if (strcmp(v->typeVar[0][0].getName(), "String@builtin")) continue;
          if (strcmp(v->typeVar[1]   .getName(), "Result@wake"))    continue;
          std::cout << "  " << g.first << std::endl;
        } else {
          std::cout << g.first << ": ";
          v->typeVar.format(std::cout, v->typeVar);
          std::cout << " = <" << v->location.file() << ">" << std::endl;
        }
      }
    }
  }

  // Convert AST to optimized SSA
  std::unique_ptr<Term> ssa = Term::fromExpr(std::move(root));
  if (optim) ssa = Term::optimize(std::move(ssa), runtime);
  ssa = Term::scope(std::move(ssa), runtime);

  // Upon request, dump out the SSA
  if (dumpssa) {
    TermFormat format(true);
    ssa->format(std::cout, format);
  }

  // Exit without execution for these arguments
  if (noexecute) return 0;

  db.prepare();
  runtime.init(static_cast<RFun*>(ssa.get()));

  // Flush buffered IO before we enter the main loop (which uses unbuffered IO exclusively)
  std::cout << std::flush;
  std::cerr << std::flush;
  fflush(stdout);
  fflush(stderr);

  runtime.abort = false;

  status_init();
  do { runtime.run(); } while (!runtime.abort && jobtable.wait(runtime));
  status_finish();

  runtime.heap.report();
  tree.report(profile, command);

  bool pass = true;
  if (runtime.abort) {
    dont_report_future_targets();
    pass = false;
  } else if (JobTable::exit_now()) {
    dont_report_future_targets();
    std::cerr << "Early termination requested" << std::endl;
    pass = false;
  } else {
    Promise *p = static_cast<Closure*>(runtime.output.get())->scope->at(0);
    HeapObject *v = *p ? p->coerce<HeapObject>() : nullptr;
    if (verbose) {
      std::cout << command << ": ";
      type[0].format(std::cout, type);
      std::cout << " = ";
    }
    if (!quiet) {
      HeapObject::format(std::cout, v, debug, verbose?0:-1);
      std::cout << std::endl;
    }
    if (!v) {
      pass = false;
    } else if (Record *r = dynamic_cast<Record*>(v)) {
      if (r->cons->ast.name == "Fail") pass = false;
    }
  }

  db.clean();
  return pass?0:1;
}
