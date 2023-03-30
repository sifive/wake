/*
 * Copyright 2022 SiFive, Inc.
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

#pragma once

#include <vector>

#include "gopt/gopt-arg.h"
#include "gopt/gopt.h"

struct CommandLineOptions {
  bool check;
  bool verbose;
  bool debug;
  bool quiet;
  bool wait;
  bool workspace;
  bool tty;
  bool fwarning;
  int profileh;
  unsigned int input;
  unsigned int output;
  bool last_use;
  bool last_exe;
  bool lsp;
  bool failed;
  bool script;
  bool metadata;
  bool version;
  bool html;
  bool global;
  bool help;
  bool config;
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
  const char *label;
  char *shebang;
  const char *tagdag;
  const char *tag;
  const char *api;
  const char *fd1;
  const char *fd2;
  const char *fd3;
  const char *fd4;
  const char *fd5;

  std::vector<char *> input_files;
  std::vector<char *> output_files;

  int argc;
  char **argv;

  CommandLineOptions(int argc_in, char **argv_in) {
    argv = argv_in;
    unsigned int max_pairs = argc_in / 2;
    input_files.reserve(max_pairs);
    output_files.reserve(max_pairs);

    // clang-format off
    struct option options[] {
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
      {'i', "input", GOPT_ARGUMENT_REQUIRED | GOPT_REPEATABLE_VALUE, input_files.data(), max_pairs},
      {'o', "output", GOPT_ARGUMENT_REQUIRED | GOPT_REPEATABLE_VALUE, output_files.data(), max_pairs},
      {0, "label", GOPT_ARGUMENT_REQUIRED},
      {'l', "last", GOPT_ARGUMENT_FORBIDDEN},
      {0, "last-used", GOPT_ARGUMENT_FORBIDDEN},
      {0, "last-executed", GOPT_ARGUMENT_FORBIDDEN},
      {0, "lsp", GOPT_ARGUMENT_FORBIDDEN},
      {'f', "failed", GOPT_ARGUMENT_FORBIDDEN},
      {'s', "script", GOPT_ARGUMENT_FORBIDDEN},
      {0, "metadata", GOPT_ARGUMENT_FORBIDDEN},
      {0, "init", GOPT_ARGUMENT_REQUIRED},
      {0, "version", GOPT_ARGUMENT_FORBIDDEN},
      {'g', "globals", GOPT_ARGUMENT_FORBIDDEN},
      {'e', "exports", GOPT_ARGUMENT_FORBIDDEN},
      {0, "html", GOPT_ARGUMENT_FORBIDDEN},
      {0, "timeline", GOPT_ARGUMENT_OPTIONAL},
      {'h', "help", GOPT_ARGUMENT_FORBIDDEN},
      {0, "config", GOPT_ARGUMENT_FORBIDDEN},
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
      {0, "clean", GOPT_ARGUMENT_FORBIDDEN },
      {0, "list-outputs", GOPT_ARGUMENT_FORBIDDEN },
      {0, "fd:3", GOPT_ARGUMENT_REQUIRED},
      {0, "fd:4", GOPT_ARGUMENT_REQUIRED},
      {0, "fd:5", GOPT_ARGUMENT_REQUIRED},
      {':', "shebang", GOPT_ARGUMENT_REQUIRED},
      {0, 0, GOPT_LAST}
    };
    // clang-format on

    argc = gopt(argv, options);
    gopt_errors(argv[0], options);

    check = arg(options, "check")->count;
    verbose = arg(options, "verbose")->count;
    debug = arg(options, "debug")->count;
    quiet = arg(options, "quiet")->count;
    wait = !arg(options, "no-wait")->count;
    workspace = !arg(options, "no-workspace")->count;
    tty = !arg(options, "no-tty")->count;
    fwarning = arg(options, "fatal-warnings")->count;
    profileh = arg(options, "profile-heap")->count;
    input = arg(options, "input")->count;
    output = arg(options, "output")->count;
    bool last = arg(options, "last")->count;
    last_use = last || arg(options, "last-used")->count;
    last_exe = arg(options, "last-executed")->count;
    lsp = arg(options, "lsp")->count;
    failed = arg(options, "failed")->count;
    script = arg(options, "script")->count;
    metadata = arg(options, "metadata")->count;
    version = arg(options, "version")->count;
    html = arg(options, "html")->count;
    global = arg(options, "globals")->count;
    help = arg(options, "help")->count;
    config = arg(options, "config")->count;
    debugdb = arg(options, "debug-db")->count;
    parse = arg(options, "stop-after-parse")->count;
    tcheck = arg(options, "stop-after-type-check")->count;
    dumpssa = arg(options, "stop-after-ssa")->count;
    optim = !arg(options, "no-optimize")->count;
    exports = arg(options, "exports")->count;
    timeline = arg(options, "timeline")->count;
    clean = arg(options, "clean")->count;
    list_outputs = arg(options, "list-outputs")->count;

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
    label = arg(options, "label")->argument;
    shebang = arg(options, "shebang")->argument;
    tagdag = arg(options, "tag-dag")->argument;
    tag = arg(options, "tag")->argument;
    api = arg(options, "export-api")->argument;
    fd1 = arg(options, "stdout")->argument;
    fd2 = arg(options, "stderr")->argument;
    fd3 = arg(options, "fd:3")->argument;
    fd4 = arg(options, "fd:4")->argument;
    fd5 = arg(options, "fd:5")->argument;
  }
};
