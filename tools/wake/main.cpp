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

// Open Group Base Specifications Issue 7
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <inttypes.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wcl/defer.h>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <random>
#include <set>
#include <sstream>

#include "describe.h"
#include "dst/bind.h"
#include "dst/expr.h"
#include "dst/todst.h"
#include "gopt/gopt-arg.h"
#include "gopt/gopt.h"
#include "job-cache/job-cache.h"
#include "markup.h"
#include "optimizer/ssa.h"
#include "parser/cst.h"
#include "parser/parser.h"
#include "parser/syntax.h"
#include "parser/wakefiles.h"
#include "runtime/database.h"
#include "runtime/job.h"
#include "runtime/prim.h"
#include "runtime/profile.h"
#include "runtime/runtime.h"
#include "runtime/sources.h"
#include "runtime/status.h"
#include "runtime/tuple.h"
#include "runtime/value.h"
#include "timeline.h"
#include "types/data.h"
#include "types/sums.h"
#include "util/diagnostic.h"
#include "util/execpath.h"
#include "util/file.h"
#include "util/shell.h"
#include "util/term.h"

#ifndef VERSION
#include "version.h"
#endif
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define VERSION_STR TOSTRING(VERSION)

static CPPFile cppFile(__FILE__);

void seed_keyed_hash(Database &db) {
  std::random_device rd;
  std::uniform_int_distribution<uint64_t> dist;
  sip_key[0] = dist(rd);
  sip_key[1] = dist(rd);
  db.entropy(&sip_key[0], 2);
}

class CommandLineOptions {
 public:
  CommandLineOptions(int argc, char **argv) {
    original_command_line = shell_escape(argv[0]);
    for (int i = 1; i < argc; ++i) original_command_line += " " + shell_escape(argv[i]);

    program_name = argv[0];

    // clang-format off
    struct option options[] = {
      {'p', "percent", GOPT_ARGUMENT_REQUIRED | GOPT_ARGUMENT_NO_HYPHEN},
      {'j', "jobs", GOPT_ARGUMENT_REQUIRED | GOPT_ARGUMENT_NO_HYPHEN},
      {'m', "memory", GOPT_ARGUMENT_REQUIRED | GOPT_ARGUMENT_NO_HYPHEN},
      {'c', "check", GOPT_ARGUMENT_FORBIDDEN},
      {'v', "verbose", GOPT_ARGUMENT_FORBIDDEN | GOPT_REPEATABLE},
      {'d', "debug", GOPT_ARGUMENT_FORBIDDEN},
      {'q', "quiet", GOPT_ARGUMENT_FORBIDDEN},
      {0, "no-wait", GOPT_ARGUMENT_FORBIDDEN},
      {0, "no-workspace", GOPT_ARGUMENT_FORBIDDEN},
      {0, "no-tty", GOPT_ARGUMENT_FORBIDDEN},
      {0, "fatal-warnings", GOPT_ARGUMENT_FORBIDDEN},
      {0, "heap-factor", GOPT_ARGUMENT_REQUIRED | GOPT_ARGUMENT_NO_HYPHEN},
      {0, "profile-heap", GOPT_ARGUMENT_FORBIDDEN | GOPT_REPEATABLE},
      {0, "profile", GOPT_ARGUMENT_REQUIRED},
      {'C', "chdir", GOPT_ARGUMENT_REQUIRED},
      {0, "in", GOPT_ARGUMENT_REQUIRED},
      {'x', "exec", GOPT_ARGUMENT_REQUIRED},
      {0, "job", GOPT_ARGUMENT_REQUIRED},
      {'i', "input", GOPT_ARGUMENT_FORBIDDEN},
      {'o', "output", GOPT_ARGUMENT_FORBIDDEN},
      {'l', "last", GOPT_ARGUMENT_FORBIDDEN},
      {0, "lsp", GOPT_ARGUMENT_FORBIDDEN},
      {'f', "failed", GOPT_ARGUMENT_FORBIDDEN},
      {'s', "script", GOPT_ARGUMENT_FORBIDDEN},
      {0, "init", GOPT_ARGUMENT_REQUIRED},
      {0, "version", GOPT_ARGUMENT_FORBIDDEN},
      {'g', "globals", GOPT_ARGUMENT_FORBIDDEN},
      {'e', "exports", GOPT_ARGUMENT_FORBIDDEN},
      {0, "html", GOPT_ARGUMENT_FORBIDDEN},
      {0, "timeline", GOPT_ARGUMENT_OPTIONAL},
      {'h', "help", GOPT_ARGUMENT_FORBIDDEN},
      {0, "debug-db", GOPT_ARGUMENT_FORBIDDEN},
      {0, "stop-after-parse", GOPT_ARGUMENT_FORBIDDEN},
      {0, "stop-after-type-check", GOPT_ARGUMENT_FORBIDDEN},
      {0, "stop-after-ssa", GOPT_ARGUMENT_FORBIDDEN},
      {0, "no-optimize", GOPT_ARGUMENT_FORBIDDEN},
      {0, "tag-dag", GOPT_ARGUMENT_REQUIRED},
      {0, "tag", GOPT_ARGUMENT_REQUIRED},
      {0, "export-api", GOPT_ARGUMENT_REQUIRED},
      {0, "stdout", GOPT_ARGUMENT_REQUIRED},
      {0, "stderr", GOPT_ARGUMENT_REQUIRED},
      {0,   "clean", GOPT_ARGUMENT_FORBIDDEN },
      {0,   "list-outputs", GOPT_ARGUMENT_FORBIDDEN },
      {0, "fd:3", GOPT_ARGUMENT_REQUIRED},
      {0, "fd:4", GOPT_ARGUMENT_REQUIRED},
      {0, "fd:5", GOPT_ARGUMENT_REQUIRED},
      {':', "shebang", GOPT_ARGUMENT_REQUIRED},
      {0, 0, GOPT_LAST}
    };
    // clang-format on

    remaining_argc = gopt(argv, options);
    remaining_argv = argv;

    gopt_errors(argv[0], options);

    // bools
    check = arg(options, "check")->count;
    verbose = arg(options, "verbose")->count;
    debug = arg(options, "debug")->count;
    quiet = arg(options, "quiet")->count;
    wait = !arg(options, "no-wait")->count;
    workspace = !arg(options, "no-workspace")->count;
    tty = !arg(options, "no-tty")->count;
    fwarning = arg(options, "fatal-warnings")->count;
    input = arg(options, "input")->count;
    output = arg(options, "output")->count;
    last = arg(options, "last")->count;
    lsp = arg(options, "lsp")->count;
    failed = arg(options, "failed")->count;
    script = arg(options, "script")->count;
    version = arg(options, "version")->count;
    html = arg(options, "html")->count;
    global = arg(options, "globals")->count;
    help = arg(options, "help")->count;
    debugdb = arg(options, "debug-db")->count;
    parse = arg(options, "stop-after-parse")->count;
    tcheck = arg(options, "stop-after-type-check")->count;
    dumpssa = arg(options, "stop-after-ssa")->count;
    optim = !arg(options, "no-optimize")->count;
    exports = arg(options, "exports")->count;
    timeline = arg(options, "timeline")->count;
    clean = arg(options, "clean")->count;
    list_outputs = arg(options, "list-outputs")->count;

    // int
    profileh = arg(options, "profile-heap")->count;

    // const char *s
    percent_str = arg(options, "percent")->argument;
    jobs_str = arg(options, "jobs")->argument;
    memory_str = arg(options, "memory")->argument;
    heapf = arg(options, "heap-factor")->argument;
    profile = arg(options, "profile")->argument;
    init = arg(options, "init")->argument;
    chdir = arg(options, "chdir")->argument;
    in = arg(options, "in")->argument;
    exec = arg(options, "exec")->argument;
    job = arg(options, "job")->argument;
    shebang = arg(options, "shebang")->argument;
    tagdag = arg(options, "tag-dag")->argument;
    tag = arg(options, "tag")->argument;
    api = arg(options, "export-api")->argument;
    fd1 = arg(options, "stdout")->argument;
    fd2 = arg(options, "stderr")->argument;
    fd3 = arg(options, "fd:3")->argument;
    fd4 = arg(options, "fd:4")->argument;
    fd5 = arg(options, "fd:5")->argument;

    if (!percent_str) {
      percent_str = getenv("WAKE_PERCENT");
    }

    if (!memory_str) {
      memory_str = getenv("WAKE_MEMORY");
    }

    if (!jobs_str) {
      jobs_str = getenv("WAKE_JOBS");
    }
  }

  bool is_valid() {
    if (quiet && verbose) {
      std::cerr << "Cannot specify both -v and -q!" << std::endl;
      return false;
    }

    if (profile && !debug) {
      std::cerr << "Cannot profile without stack trace support (-d)!" << std::endl;
      return false;
    }

    if (shebang && chdir) {
      std::cerr << "Cannot specify chdir and shebang simultaneously!" << std::endl;
      return false;
    }

    // TODO: fix this
    if (shebang /*&& argc < 2*/) {
      // std::cerr << "Shebang invocation requires a script name as the first non-option argument"
      //           << std::endl;
      return false;
    }

    struct stat sbuf;

    if (fstat(1, &sbuf) != 0) {
      std::cerr << "Wake must be run with an open standard output (file descriptor 1)" << std::endl;
      return false;
    }

    if (fstat(2, &sbuf) != 0) {
      std::cout << "Wake must be run with an open standard error (file descriptor 2)" << std::endl;
      return false;
    }

    if (fd3 && fstat(3, &sbuf) != 0) {
      std::cerr << "Cannot specify --fd:3 unless file descriptor 3 is already open" << std::endl;
      return false;
    }

    if (fd4 && fstat(4, &sbuf) != 0) {
      std::cerr << "Cannot specify --fd:4 unless file descriptor 4 is already open" << std::endl;
      return false;
    }

    if (fd5 && fstat(5, &sbuf) != 0) {
      std::cerr << "Cannot specify --fd:5 unless file descriptor 5 is already open" << std::endl;
      return false;
    }

    if (percent_str) {
      char *tail;
      percent = strtod(percent_str, &tail);
      percent /= 100.0;
      if (*tail || percent < 0.01 || percent > 0.99) {
        std::cerr << "Cannot run with " << percent_str << "%  (must be >= 0.01 and <= 0.99)!"
                  << std::endl;
        return false;
      }
    }

    if (heapf) {
      char *tail;
      heap_factor = strtod(heapf, &tail);
      if (*tail || heap_factor < 1.1) {
        std::cerr << "Cannot run with " << heapf << " heap-factor (must be >= 1.1)!" << std::endl;
        return false;
      }
    }

    // Arguments are forbidden with these options
    bool noargs =
        init || job || last || failed || tagdag || html || global || exports || api || exec;
    if (noargs && remaining_argc > 1) {
      std::cerr << "Unexpected positional arguments on the command-line!" << std::endl;
      return false;
    }

    return true;
  }

  int profileh;

  bool check;
  bool verbose;
  bool debug;
  bool quiet;
  bool wait;
  bool workspace;
  bool tty;
  bool fwarning;
  bool input;
  bool output;
  bool last;
  bool lsp;
  bool failed;
  bool script;
  bool version;
  bool html;
  bool global;
  bool help;
  bool debugdb;
  bool parse;
  bool tcheck;
  bool dumpssa;
  bool optim;
  bool exports;
  bool timeline;
  bool clean;
  bool list_outputs;

  const char *percent_str;
  const char *jobs_str;
  const char *memory_str;
  const char *heapf;
  const char *profile;
  const char *init;
  const char *chdir;
  const char *in;
  const char *exec;
  const char *job;
  const char *shebang;
  const char *tagdag;
  const char *tag;
  const char *api;
  const char *fd1;
  const char *fd2;
  const char *fd3;
  const char *fd4;
  const char *fd5;

  const char *program_name;
  std::string original_command_line;

  double percent = 0.9;
  double heap_factor = 4.0;

  int remaining_argc;
  char **remaining_argv;

 private:
};

int print_help(const char *argv0) {
  // clang-format off
  std::cout << std::endl
    << "Usage: " << argv0 << " [OPTIONS] [target] [target options ...]" << std::endl
    << "Usage in script: #! /usr/bin/env wake [OPTIONS] -:target" << std::endl
    << std::endl
    << "  Flags affecting build execution:" << std::endl
    << "    --jobs=N   -jN   Schedule local jobs for N cores or N% of CPU (default 90%)" << std::endl
    << "    --memory=M -mM   Schedule local jobs for M bytes or M% of RAM (default 90%)" << std::endl
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
    << "    --timeline       Print the timeline of wake jobs as HTML"                    << std::endl
    << "    --clean          Delete all job outputs"                                     << std::endl
    << "    --list-outputs   List all job outputs"                                       << std::endl
    << std::endl
    << "  Help functions:" << std::endl
    << "    --version        Print the version of wake on standard output"               << std::endl
    << "    --html           Print all wake source files as cross-referenced HTML"       << std::endl
    << "    --globals -g     Print global symbols made available to all wake files"      << std::endl
    << "    --exports -e     Print symbols exported by the selected package (see --in)"  << std::endl
    << "    --help    -h     Print this help message and exit"                           << std::endl
    << std::endl;
    // debug-db, no-optimize, stop-after-* are secret undocumented options
  // clang-format on

  return 0;
}

int print_version() {
  std::cout << "wake " << VERSION_STR << std::endl;
  return 0;
}

int lsp() {
  std::string lsp = make_canonical(find_execpath() + "/../lib/wake/lsp-wake");
  execl(lsp.c_str(), "lsp-wake", nullptr);
  std::cerr << "exec(" << lsp << "): " << strerror(errno) << std::endl;
  return 1;
}

int wake_init(const CommandLineOptions &clo) {
  if (!make_workspace(clo.init)) {
    std::cerr << "Unable to initialize a workspace in " << clo.init << std::endl;
    return 1;
  }
  return 0;
}

// If the user asked to list all files we *would* clean.
// This is the same as asking for all output files.
int wake_clean_list_outputs(const Database &db) {
  // Find all the file we would need to delete.
  auto files = db.get_outputs();

  // print them all out
  for (const auto &file : files) {
    std::cout << file << std::endl;
  }

  return 0;
}

int wake_clean(Database &db) {
  // Clean up the database of unwanted info. Jobs must
  // be cleared before outputs are removed to avoid foreign key
  // constraint issues.
  auto paths = db.clear_jobs();

  // Sort them so that child directories come before parent directories
  std::sort(paths.begin(), paths.end(), [&](const std::string &a, const std::string &b) -> bool {
    return a.size() > b.size();
  });

  // Delete all the files
  for (const auto &path : paths) {
    // Don't delete the root directory
    // - Certain writes will create the parent dir "." which shouldn't be deleted
    if (path == ".") {
      continue;
    }

    // First we try to unlink the file
    if (unlink(path.c_str()) == -1) {
#if defined(__linux__)
      bool is_dir = (errno == EISDIR);
#else
      bool is_dir = (errno == EPERM || errno == EACCES);
#endif

      // If it was actually a directory we remove it instead
      if (is_dir) {
        if (rmdir(path.c_str()) == -1) {
          if (errno == ENOTEMPTY) continue;
          std::cerr << "error: rmdir(" << path << "): " << strerror(errno) << std::endl;
          return 1;
        }
        continue;
      }

      // If the entry doesn't exist then nothing to delete
      if (errno == ENOENT) continue;

      // If it wasn't a directory then we fail
      std::cerr << "error: unlink(" << path << "): " << strerror(errno) << std::endl;
      return 1;
    }
  }

  return 0;
}

// TODO: Shouldn't --timeline just have an optional arg
// instead of manually parsing out the value?
int timeline(const CommandLineOptions &clo, const Database &db) {
  if (clo.remaining_argc == 1) {
    get_and_write_timeline(std::cout, db);
    return 0;
  }

  char *timeline_str = clo.remaining_argv[1];
  if (strcmp(timeline_str, "job-reflections") == 0) {
    get_and_write_job_reflections(std::cout, db);
    return 0;
  }

  if (strcmp(timeline_str, "file-accesses") == 0) {
    get_and_write_file_accesses(std::cout, db);
    return 0;
  }

  std::cerr << "Unrecognized option after --timeline" << std::endl;
  return 1;
}

int describe_job(const CommandLineOptions &clo, Database &db) {
  auto hits = db.explain(std::atol(clo.job), clo.verbose || clo.tag);
  describe(hits, clo.script, clo.debug, clo.verbose, clo.tag);
  if (hits.empty())
    std::cerr << "Job '" << clo.job << "' was not found in the database!" << std::endl;
  return 0;
}

int describe_input(const CommandLineOptions &clo, Database &db, const std::string &wake_cwd) {
  for (int i = 1; i < clo.remaining_argc; ++i) {
    describe(
        db.explain(make_canonical(wake_cwd + clo.remaining_argv[i]), 1, clo.verbose || clo.tag),
        clo.script, clo.debug, clo.verbose, clo.tag);
  }
  return 0;
}
int describe_output(const CommandLineOptions &clo, Database &db, const std::string &wake_cwd) {
  for (int i = 1; i < clo.remaining_argc; ++i) {
    describe(
        db.explain(make_canonical(wake_cwd + clo.remaining_argv[i]), 2, clo.verbose || clo.tag),
        clo.script, clo.debug, clo.verbose, clo.tag);
  }
  return 0;
}
int describe_last(const CommandLineOptions &clo, Database &db) {
  describe(db.last(clo.verbose || clo.tag), clo.script, clo.debug, clo.verbose, clo.tag);
  return 0;
}
int describe_failed(const CommandLineOptions &clo, Database &db) {
  describe(db.failed(clo.verbose || clo.tag), clo.script, clo.debug, clo.verbose, clo.tag);
  return 0;
}
int tagdag(const CommandLineOptions &clo, Database &db) {
  JAST json = create_tagdag(db, clo.tagdag);
  std::cout << json << std::endl;
  return 0;
}

bool manually_set_package(const CommandLineOptions &clo, const std::unique_ptr<Top> &top) {
  auto it = top->packages.find(clo.in);
  if (it == top->packages.end()) {
    std::cerr << "Package '" << clo.in << "' selected by --in does not exist!" << std::endl;
    return false;
  }

  top->def_package = clo.in;
  return true;
}

DiagnosticReporter *reporter;
class TerminalReporter : public DiagnosticReporter {
 public:
  TerminalReporter() : errors(false), warnings(false) {}
  bool errors;
  bool warnings;

 private:
  std::string last;

  void report(Diagnostic diagnostic) {
    if (diagnostic.getSeverity() == S_ERROR) errors = true;
    if (diagnostic.getSeverity() == S_WARNING) warnings = true;

    if (last != diagnostic.getMessage()) {
      last = diagnostic.getMessage();
      std::cerr << diagnostic.getLocation() << ": ";
      if (diagnostic.getSeverity() == S_WARNING) std::cerr << "(warning) ";
      std::cerr << diagnostic.getMessage() << std::endl;
    }
  }
};

int main(int argcc, char **argvv) {
  auto start = std::chrono::steady_clock::now();

  TerminalReporter terminalReporter;
  reporter = &terminalReporter;

  CommandLineOptions clo(argcc, argvv);  // TODO: revert the names after confirming not used
  if (!clo.is_valid()) {
    return 1;
  }

  if (clo.help) {
    return print_help(clo.program_name);
  }

  if (clo.version) {
    return print_version();
  }

  if (clo.lsp) {
    return lsp();
  }

  bool tty = term_init(clo.tty);
  ResourceBudget memory_budget(clo.percent);
  ResourceBudget cpu_budget(clo.percent);

  if (clo.memory_str) {
    if (auto error = ResourceBudget::parse(clo.memory_str, memory_budget)) {
      std::cerr << "Option '-m" << clo.memory_str << "' is illegal; " << error << std::endl;
      return 1;
    }
  }

  if (clo.jobs_str) {
    if (auto error = ResourceBudget::parse(clo.jobs_str, cpu_budget)) {
      std::cerr << "Option '-j" << clo.jobs_str << "' is illegal; " << error << std::endl;
      return 1;
    }
  }

  // Change directory to the location of the invoked script
  // and execute the specified target function
  // if (shebang) {
  //   chdir = argv[1];
  //   argv[1] = shebang;
  // }

  if (clo.init) {
    return wake_init(clo);
  }

  // wake_cwd is the path where wake was invoked, relative to the workspace root (may have leading
  // ../) src_dir is the chdir path (-C) used to select the default package, relative to the
  // workspace root (always a subdir)
  std::string wake_cwd, src_dir;  // form: "" | .+/

  if (clo.workspace && !chdir_workspace(clo.chdir, wake_cwd, src_dir)) {
    std::cerr << "Unable to locate wake.db in any parent directory." << std::endl;
    return 1;
  }

  // check that the .wakeroot is compatible with the wake version
  std::string version_check = check_version(clo.workspace, VERSION_STR);
  if (!version_check.empty()) {
    std::cerr << ".wakeroot: " << version_check << std::endl;
    return 1;
  }

  Database db(clo.debugdb);
  std::string fail = db.open(clo.wait, !clo.workspace, tty);
  if (!fail.empty()) {
    std::cerr << "Failed to open wake.db: " << fail << std::endl;
    return 1;
  }

  // Open the job-cache if it exists
  std::unique_ptr<job_cache::Cache> cache;
  const char *job_cache_dir = getenv("WAKE_EXPERIMENTAL_JOB_CACHE");
  if (job_cache_dir != nullptr) {
    cache = std::make_unique<job_cache::Cache>(job_cache_dir);
    set_job_cache(cache.get());
  }

  if (clo.list_outputs) {
    return wake_clean_list_outputs(db);
  }

  if (clo.clean) {
    return wake_clean(db);
  }

  seed_keyed_hash(db);

  if (clo.timeline) {
    return timeline(clo, db);
  }

  if (clo.job) {
    return describe_job(clo, db);
  }

  if (clo.input) {
    return describe_input(clo, db, wake_cwd);
  }

  if (clo.output) {
    return describe_output(clo, db, wake_cwd);
  }

  if (clo.last) {
    return describe_last(clo, db);
  }

  if (clo.failed) {
    return describe_failed(clo, db);
  }

  if (clo.tagdag) {
    return tagdag(clo, db);
  }

  std::vector<std::string> wakefilenames;
  std::string libdir = make_canonical(find_execpath() + "/../share/wake/lib");
  {
    FILE *user_warn = stdout;
    wcl::opt_defer user_warn_defer;
    if (clo.quiet) {
      user_warn = fopen("/dev/null", "w");
      user_warn_defer = wcl::make_opt_defer([&]() { fclose(user_warn); });
    }

    bool enumok = true;
    wakefilenames = find_all_wakefiles(enumok, clo.workspace, clo.verbose, libdir, ".", user_warn);
    if (!enumok) {
      if (clo.verbose) std::cerr << "Workspace wake file enumeration failed" << std::endl;
      // Try to run the build anyway; if wake files are missing, it will fail later
      // The unreadable location might be irrelevant to the build
    }
  }

  Profile tree;
  Runtime runtime(clo.profile ? &tree : nullptr, clo.profileh, clo.heap_factor);
  {
    bool sources = find_all_sources(runtime, clo.workspace);
    if (!sources) {
      if (clo.verbose) std::cerr << "Source file enumeration failed" << std::endl;
      // Try to run the build anyway; if sources are missing, it will fail later
      // The unreadable location might be irrelevant to the build
    }
  }

  std::unique_ptr<Top> top(new Top);
  {
    // Select a default package
    int longest_src_dir = -1;
    bool warned_conflict = false;

    // Read all wake build files
    Scope::debug = clo.debug;
    std::vector<ExternalFile> wakefiles;
    wakefiles.reserve(wakefilenames.size());

    bool alerted_slow_cache = false;

    for (size_t i = 0; i < wakefilenames.size(); i++) {
      auto &wakefile = wakefilenames[i];

      auto now = std::chrono::steady_clock::now();
      if (!clo.quiet &&
          std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count() > 1000) {
        std::cout << "Scanning " << i + 1 << "/" << wakefilenames.size()
                  << " wake files. Kernel file cache may be cold.\r" << std::flush;
        start = now;
        alerted_slow_cache = true;
      }

      if (clo.verbose && clo.debug) std::cerr << "Parsing " << wakefile << std::endl;

      wakefiles.emplace_back(terminalReporter, wakefile.c_str());
      FileContent &file = wakefiles.back();
      CST cst(file, terminalReporter);
      auto package = dst_top(cst.root(), *top);

      // Does this file inform our choice of a default package?
      size_t slash = wakefile.find_last_of('/');
      std::string dir(wakefile, 0, slash == std::string::npos ? 0 : (slash + 1));  // "" | .+/
      if (src_dir.compare(0, dir.size(), dir) == 0) {  // dir = prefix or parent of src_dir?
        int dirlen = dir.size();
        if (dirlen > longest_src_dir) {
          longest_src_dir = dirlen;
          top->def_package = package;
          warned_conflict = false;
        } else if (dirlen == longest_src_dir) {
          if (top->def_package != package && !warned_conflict) {
            std::cerr << "Directory " << (dir.empty() ? "." : dir.c_str())
                      << " has wakefiles with both package '" << top->def_package << "' and '"
                      << package << "'. This prevents default package selection;"
                      << " defaulting to no package." << std::endl;
            top->def_package = nullptr;
            warned_conflict = true;
          }
        }
      }
    }

    if (!clo.quiet && alerted_slow_cache) {
      std::cout << "Scanning " << wakefilenames.size() << "/" << wakefilenames.size()
                << " wake files. Kernel file cache may be cold." << std::endl;
    }
  }

  bool targets = (clo.remaining_argc == 1);
  bool noexecute =
      clo.html || clo.tcheck || clo.dumpssa || clo.global || clo.exports || clo.api || targets;
  bool ok = true;

  if (clo.in) {
    ok &= manually_set_package(clo, top);
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
      for (auto &e : it->second->exports.defs) defs.emplace_back(e.first, e.second.qualified);
    }
    if (defs.empty()) {
      ok = false;
      std::cerr << "No targets were found to recommend for use on the command-line." << std::endl
                << std::endl
                << "Potential solutions include:" << std::endl
                << "  cd project-directory; wake # lists targets for current directory" << std::endl
                << "  wake --in project          # lists targets for a specific project"
                << std::endl
                << std::endl
                << "If you are a developer, you should also consider adding:" << std::endl
                << "  export target build string_list = ... # to your wake build scripts"
                << std::endl
                << std::endl;
    }
  }

  if (clo.global) {
    for (auto &g : top->globals.defs) defs.emplace_back(g.first, g.second.qualified);
    for (auto &t : top->globals.topics)
      defs.emplace_back("topic " + t.first, "topic " + t.second.qualified);
    for (auto &t : top->globals.types) types.insert(t.first);
  }

  if (clo.exports || clo.api) {
    auto it = top->packages.find(top->def_package);
    if (it != top->packages.end()) {
      for (auto &e : it->second->exports.defs) defs.emplace_back(e.first, e.second.qualified);
      for (auto &t : it->second->exports.topics)
        defs.emplace_back("topic " + t.first, "topic " + t.second.qualified);
      for (auto &t : it->second->exports.types) types.insert(t.first);
    }
  }

  char *none = nullptr;
  char **cmdline = &none;
  std::string command;

  if (clo.exec) {
    command = clo.exec;
  } else if (clo.remaining_argc > 1) {
    command = clo.remaining_argv[1];
    cmdline = clo.remaining_argv + 2;
  }

  ExprParser cmdExpr(command);
  {
    if (clo.exec) {
      top->body = cmdExpr.expr(terminalReporter);
    } else if (clo.remaining_argc > 1) {
      top->body =
          std::unique_ptr<Expr>(new App(FRAGMENT_CPP_LINE, cmdExpr.expr(terminalReporter).release(),
                                        new Prim(FRAGMENT_CPP_LINE, "cmdline")));
    } else {
      top->body = std::unique_ptr<Expr>(new VarRef(FRAGMENT_CPP_LINE, "Nil@wake"));
    }
  }

  TypeVar type = top->body->typeVar;

  if (clo.parse) {
    top->format(std::cout, 0);
    return (ok && !terminalReporter.errors) ? 0 : 1;
  }

  {
    /* Setup logging streams */
    const char *fd1_streams = (noexecute && !clo.fd1) ? "error" : clo.fd1;
    fd1_streams = (clo.debug && !fd1_streams) ? "debug,info,echo,report,warning,error" : clo.fd1;
    fd1_streams = (clo.verbose && !fd1_streams) ? "info,echo,report,warning,error" : clo.fd1;
    fd1_streams = (clo.quiet && !fd1_streams) ? "error" : clo.fd1;
    fd1_streams = (!clo.tty && !fd1_streams) ? "echo,report,warning,error" : clo.fd1;
    if (!fd1_streams) fd1_streams = "report,warning,error";

    const char *fd2_streams = (!clo.fd2) ? "error" : clo.fd2;

    status_set_bulk_fd(1, fd1_streams);
    status_set_bulk_fd(2, fd2_streams);
    status_set_bulk_fd(3, clo.fd3);
    status_set_bulk_fd(4, clo.fd4);
    status_set_bulk_fd(5, clo.fd5);
  }

  /* Primitives */
  JobTable jobtable(&db, memory_budget, cpu_budget, clo.debug, clo.verbose, clo.quiet, clo.check,
                    !tty);
  StringInfo info(clo.verbose, clo.debug, clo.quiet, VERSION_STR, make_canonical(wake_cwd),
                  cmdline);
  PrimMap pmap = prim_register_all(&info, &jobtable);

  bool isTreeBuilt = true;
  std::unique_ptr<Expr> root = bind_refs(std::move(top), pmap, isTreeBuilt);
  if (!isTreeBuilt) ok = false;

  sums_ok();

  if (clo.tcheck) std::cout << root.get();

  if (!ok || terminalReporter.errors || (clo.fwarning && terminalReporter.warnings)) {
    std::cerr << ">>> Aborting without execution <<<" << std::endl;
    return 1;
  }

  if (clo.html) markup_html(libdir, std::cout, root.get());

  if (clo.api) {
    std::vector<std::string> mixed(types.begin(), types.end());
    std::cout << "package " << clo.api << std::endl;
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

  if (clo.api) {
    std::vector<std::string> def, topic;
    for (auto &d : defs) {
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
        DefBinding *d = static_cast<DefBinding *>(e);
        e = d->body.get();
        auto i = d->order.find(g.second);
        if (i != d->order.end()) {
          int idx = i->second.index;
          Expr *v =
              idx < (int)d->val.size() ? d->val[idx].get() : d->fun[idx - d->val.size()].get();
          if (targets) {
            TypeVar clone;
            v->typeVar.clone(clone);
            TypeVar fn1(FN, 2);
            TypeVar fn2(FN, 2);
            TypeVar list;
            Data::typeList.clone(list);
            fn1[0].unify(list);
            list[0].unify(Data::typeString);
            if (!clone.tryUnify(fn1)) continue;    // must accept List String
            if (clone[1].tryUnify(fn2)) continue;  // and not return a function
            std::cout << "  " << g.first << std::endl;
          } else {
            std::cout << g.first << ": ";
            v->typeVar.format(std::cout, v->typeVar);
            std::cout << " = <" << v->fragment.location() << ">" << std::endl;
          }
        }
      }
    }
  }

  // Convert AST to optimized SSA
  std::unique_ptr<Term> ssa = Term::fromExpr(std::move(root), runtime);
  if (clo.optim) ssa = Term::optimize(std::move(ssa), runtime);

  // Upon request, dump out the SSA
  if (clo.dumpssa) {
    TermFormat format;
    ssa->format(std::cout, format);
  }

  // Implement scope
  ssa = Term::scope(std::move(ssa), runtime);

  // Exit without execution for these arguments
  if (noexecute) return 0;

  db.prepare(clo.original_command_line);
  runtime.init(static_cast<RFun *>(ssa.get()));

  // Flush buffered IO before we enter the main loop (which uses unbuffered IO exclusively)
  std::cout << std::flush;
  std::cerr << std::flush;
  fflush(stdout);
  fflush(stderr);

  runtime.abort = false;

  status_init();
  do {
    runtime.run();
  } while (!runtime.abort && jobtable.wait(runtime));
  status_finish();

  runtime.heap.report();
  tree.report(clo.profile, command);

  auto db_clean = wcl::make_defer([&]() { db.clean(); });

  if (runtime.abort) {
    dont_report_future_targets();
    return 1;
  }

  if (JobTable::exit_now()) {
    dont_report_future_targets();
    std::cerr << "Early termination requested" << std::endl;
    return 1;
  }

  HeapObject *v = runtime.output.get();
  if (!v) {
    return 1;
  }

  bool pass = true;
  if (Record *r = dynamic_cast<Record *>(v)) {
    if (r->cons->ast.name == "Fail") pass = false;
  }
  std::ostream &os = pass ? (std::cout) : (std::cerr);
  if (clo.verbose) {
    os << command << ": ";
    type.format(os, type);
    os << " = ";
  }
  if (!clo.quiet || !pass) {
    HeapObject::format(os, v, clo.debug, clo.verbose ? 0 : -1);
    os << std::endl;
  }

  return pass ? 0 : 1;
}
