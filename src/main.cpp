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
    << "Usage in script: #! /usr/bin/env wake [OPTIONS] -:target" << std::endl
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
    << "    --fatal-warnings Do not execute if there are any warnings"                   << std::endl
    << "    --heap-factor X  Heap-size is X * live data after the last GC (default 4.0)" << std::endl
    << "    --profile-heap   Report memory consumption on every garbage collection"      << std::endl
    << "    --profile  FILE  Report runtime breakdown by stack trace to HTML/JSON file"  << std::endl
    << "    --chdir -C PATH  Locate database and default package starting from PATH"     << std::endl
    << "    --in       PKG   Evaluate command-line in package PKG (default is chdir)"    << std::endl
    << "    --exec -x  EXPR  Execute expression EXPR instead of a target function"       << std::endl
    << std::endl
    << "  Database commands:" << std::endl
    << "    --init      DIR  Create or replace a wake.db in the specified directory"     << std::endl
    << "    --input  -i FILE Report recorded meta-data for jobs which read FILES"        << std::endl
    << "    --output -o FILE Report recorded meta-data for jobs which wrote FILES"       << std::endl
    << "    --job       JOB  Report recorded meta-data for the specified job id"         << std::endl
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
    { 0,   "fatal-warnings",        GOPT_ARGUMENT_FORBIDDEN },
    { 0,   "heap-factor",           GOPT_ARGUMENT_REQUIRED  | GOPT_ARGUMENT_NO_HYPHEN },
    { 0,   "profile-heap",          GOPT_ARGUMENT_FORBIDDEN | GOPT_REPEATABLE },
    { 0,   "profile",               GOPT_ARGUMENT_REQUIRED  },
    { 'C', "chdir",                 GOPT_ARGUMENT_REQUIRED  },
    { 0,   "in",                    GOPT_ARGUMENT_REQUIRED  },
    { 'x', "exec",                  GOPT_ARGUMENT_REQUIRED  },
    { 0,   "job",                   GOPT_ARGUMENT_REQUIRED  },
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
    { 0,   "tag-dag",               GOPT_ARGUMENT_REQUIRED  },
    { 0,   "tag",                   GOPT_ARGUMENT_REQUIRED  },
    { 0,   "export-api",            GOPT_ARGUMENT_REQUIRED  },
    { 0,   "stdout",                GOPT_ARGUMENT_REQUIRED  },
    { 0,   "stderr",                GOPT_ARGUMENT_REQUIRED  },
    { 0,   "fd:3",                  GOPT_ARGUMENT_REQUIRED  },
    { 0,   "fd:4",                  GOPT_ARGUMENT_REQUIRED  },
    { 0,   "fd:5",                  GOPT_ARGUMENT_REQUIRED  },
    { ':', "shebang",               GOPT_ARGUMENT_REQUIRED  },
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
  bool fwarning= arg(options, "fatal-warnings")->count;
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
  const char *chdir   = arg(options, "chdir")->argument;
  const char *in      = arg(options, "in")->argument;
  const char *exec    = arg(options, "exec")->argument;
  const char *job     = arg(options, "job")->argument;
  char       *shebang = arg(options, "shebang")->argument;
  const char *tagdag  = arg(options, "tag-dag")->argument;
  const char *tag     = arg(options, "tag")->argument;
  const char *api     = arg(options, "export-api")->argument;
  const char *fd1     = arg(options, "stdout")->argument;
  const char *fd2     = arg(options, "stderr")->argument;
  const char *fd3     = arg(options, "fd:3")->argument;
  const char *fd4     = arg(options, "fd:4")->argument;
  const char *fd5     = arg(options, "fd:5")->argument;

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

  if (shebang && chdir) {
    std::cerr << "Cannot specify chdir and shebang simultaneously!" << std::endl;
    return 1;
  }

  if (shebang && argc < 2) {
    std::cerr << "Shebang invocation requires a script name as the first non-option argument" << std::endl;
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

  // Change directory to the location of the invoked script
  // and execute the specified target function
  if (shebang) {
    chdir = argv[1];
    argv[1] = shebang;
  }

  // Arguments are forbidden with these options
  bool noargs = init || last || failed || html || global || exports || api || exec;
  bool targets = argc == 1 && !noargs;

  bool nodb = init;
  bool noparse = nodb || job || output || input || last || failed || tagdag;
  bool notype = noparse || parse;
  bool noexecute = notype || html || tcheck || dumpssa || global || exports || api || targets;

  if (noargs && argc > 1) {
    std::cerr << "Unexpected positional arguments on the command-line!" << std::endl;
    return 1;
  }

  // wake_cwd is the path where wake was invoked, relative to the workspace root (may have leading ../)
  // src_dir is the chdir path (-C) used to select the default package, relative to the workspace root (always a subdir)
  std::string wake_cwd, src_dir; // form: "" | .+/
  if (init) {
    if (!make_workspace(init)) {
      std::cerr << "Unable to initialize a workspace in " << init << std::endl;
      return 1;
    }
  } else if (workspace && !chdir_workspace(chdir, wake_cwd, src_dir)) {
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

  if (job) {
    auto hits = db.explain(std::atol(job), verbose || tag);
    describe(hits, script, debug, verbose, tag);
    if (hits.empty()) std::cerr << "Job '" << job << "' was not found in the database!" << std::endl;
  }

  if (input) {
    for (int i = 1; i < argc; ++i) {
      describe(db.explain(make_canonical(wake_cwd + argv[i]), 1, verbose || tag), script, debug, verbose, tag);
    }
  }

  if (output) {
    for (int i = 1; i < argc; ++i) {
      describe(db.explain(make_canonical(wake_cwd + argv[i]), 2, verbose || tag), script, debug, verbose, tag);
    }
  }

  if (last) {
    describe(db.last(verbose || tag), script, debug, verbose, tag);
  }

  if (failed) {
    describe(db.failed(verbose || tag), script, debug, verbose, tag);
  }

  if (tagdag) {
    JAST json = create_tagdag(db, tagdag);
    std::cout << json << std::endl;
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
  int longest_src_dir = -1;
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
    if (src_dir.compare(0, dir.size(), dir) == 0) { // dir = prefix or parent of src_dir?
      int dirlen = dir.size();
      if (dirlen > longest_src_dir) {
        longest_src_dir = dirlen;
        top->def_package = package;
        warned_conflict = false;
      } else if (dirlen == longest_src_dir) {
        if (top->def_package != package && !warned_conflict) {
          std::cerr << "Directory " << dir
            << " has wakefiles with both package '" << top->def_package
            << "' and '" << package
            << "'. This prevents default package selection;"
            << " defaulting to no package." << std::endl;
          top->def_package = nullptr;
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
  if (!top->def_package) top->def_package = "nothing";
  std::string export_package = top->def_package;
  if (!flatten_exports(*top)) ok = false;

  std::vector<std::pair<std::string, std::string> > defs;
  std::set<std::string> types;

  if (targets) {
    auto it = top->packages.find(top->def_package);
    if (it != top->packages.end()) {
      for (auto &e : it->second->exports.defs)
        defs.emplace_back(e.first, e.second.qualified);
    }
    if (defs.empty()) {
      ok = false;
      std::cerr
        << "No targets were found to recommend for use on the command-line."      << std::endl << std::endl
        << "Potential solutions include:"                                         << std::endl
        << "  cd project-directory; wake # lists targets for current directory"   << std::endl
        << "  wake --in project          # lists targets for a specific project"  << std::endl << std::endl
        << "If you are a developer, you should also consider adding:"             << std::endl
        << "  export target build string_list = ... # to your wake build scripts" << std::endl << std::endl;
    }
  }

  if (global) {
    for (auto &g : top->globals.defs)
      defs.emplace_back(g.first, g.second.qualified);
    for (auto &t : top->globals.topics)
      defs.emplace_back("topic " + t.first, "topic " + t.second.qualified);
    for (auto &t : top->globals.types)
      types.insert(t.first);
  }

  if (exports || api) {
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

  std::string command;
  char *none = nullptr;
  char **cmdline = &none;
  if (exec) {
    command = exec;
    Lexer lex(runtime.heap, command, "<execute-argument>");
    top->body = std::unique_ptr<Expr>(parse_command(lex));
    if (lex.fail) ok = false;
  } else if (argc > 1) {
    command = argv[1];
    cmdline = argv+2;
    Lexer lex(runtime.heap, command, "<target-argument>");
    std::unique_ptr<Expr> var(parse_command(lex));
    if (var) {
      top->body = std::unique_ptr<Expr>(new App(LOCATION, var.release(), new Prim(LOCATION, "cmdline")));
    } else {
      top->body = std::unique_ptr<Expr>(new Prim(LOCATION, "cmdline"));
      ok = false;
    }
  } else {
    top->body = std::unique_ptr<Expr>(new VarRef(LOCATION, "Nil@wake"));
  }

  TypeVar type = top->body->typeVar;

  if (parse) std::cout << top.get();
  if (notype) return ok?0:1;

  /* Setup logging streams */
  if (debug   && !fd1) fd1 = "debug,info,echo,warning,error";
  if (verbose && !fd1) fd1 = "info,echo,warning,error";
  if (quiet   && !fd1) fd1 = "error";
  if (!tty    && !fd1) fd1 = "info,echo,warning,error";
  if (!fd1) fd1 = "warning,error";
  if (!fd2) fd2 = "error";

  status_set_bulk_fd(1, fd1);
  status_set_bulk_fd(2, fd2);
  status_set_bulk_fd(3, fd3);
  status_set_bulk_fd(4, fd4);
  status_set_bulk_fd(5, fd5);

  /* Primitives */
  JobTable jobtable(&db, percent, verbose, quiet, check, !tty);
  StringInfo info(verbose, debug, quiet, VERSION_STR, make_canonical(wake_cwd), cmdline);
  PrimMap pmap = prim_register_all(&info, &jobtable);

  std::unique_ptr<Expr> root = bind_refs(std::move(top), pmap);
  if (!root) ok = false;
  ok = ok && sums_ok();

  if (!ok || (fwarning && warnings)) {
    std::cerr << ">>> Aborting without execution <<<" << std::endl;
    return 1;
  }

  if (tcheck) std::cout << root.get();
  if (html) markup_html(std::cout, root.get());

  if (api) {
    std::vector<std::string> mixed(types.begin(), types.end());
    std::cout << "package " << api << std::endl;
    format_reexports(std::cout, export_package.c_str(), "type", mixed);
  } else if (!types.empty()) {
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

  if (api) {
    std::vector<std::string> def, topic;
    for (auto &d: defs) {
      if (d.first.compare(0, 6, "topic ") == 0) {
        topic.emplace_back(d.first.substr(6));
      } else {
        def.emplace_back(d.first);
      }
    }
    format_reexports(std::cout, export_package.c_str(), "def", def);
    format_reexports(std::cout, export_package.c_str(), "topic", topic);
  } else {
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
            TypeVar clone;
            v->typeVar.clone(clone);
            TypeVar fn1(FN, 2);
            TypeVar fn2(FN, 2);
            TypeVar list;
            Data::typeList.clone(list);
            fn1[0].unify(list);
            list[0].unify(String::typeVar);
            if (!clone.tryUnify(fn1)) continue;   // must accept List String
            if (clone[1].tryUnify(fn2)) continue; // and not return a function
            std::cout << "  " << g.first << std::endl;
          } else {
            std::cout << g.first << ": ";
            v->typeVar.format(std::cout, v->typeVar);
            std::cout << " = <" << v->location.file() << ">" << std::endl;
          }
        }
      }
    }
  }

  // Convert AST to optimized SSA
  std::unique_ptr<Term> ssa = Term::fromExpr(std::move(root));
  if (optim) ssa = Term::optimize(std::move(ssa), runtime);

  // Upon request, dump out the SSA
  if (dumpssa) {
    TermFormat format;
    ssa->format(std::cout, format);
  }

  // Implement scope
  ssa = Term::scope(std::move(ssa), runtime);

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
    HeapObject *v = runtime.output.get();
    if (verbose) {
      std::cout << command << ": ";
      type.format(std::cout, type);
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
