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

// Open Group Base Specifications Issue 7
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <wcl/doc.h>

#include <fstream>
#include <iostream>
#include <memory>
#include <vector>

#include "emitter.h"
#include "gopt/gopt-arg.h"
#include "gopt/gopt.h"
#include "parser/cst.h"
#include "parser/parser.h"
#include "parser/syntax.h"
#include "util/diagnostic.h"
#include "util/file.h"
#include "wcl/diff.h"
#include "wcl/xoshiro_256.h"

#ifndef VERSION
#include "version.h"
#endif
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define VERSION_STR TOSTRING(VERSION)

class TerminalReporter : public DiagnosticReporter {
 public:
  TerminalReporter() : errors(false), warnings(false) {}
  bool errors;
  bool warnings;

  void report_to_stderr() { std::cerr << "Start<" << ostream.str() << ">End"; }

 private:
  std::stringstream ostream;
  std::string last;

  void report(Diagnostic diagnostic) {
    if (diagnostic.getSeverity() == S_ERROR) errors = true;
    if (diagnostic.getSeverity() == S_WARNING) warnings = true;

    if (last != diagnostic.getMessage()) {
      last = diagnostic.getMessage();
      ostream << diagnostic.getLocation() << ": ";
      if (diagnostic.getSeverity() == S_WARNING) ostream << "(warning) ";
      ostream << diagnostic.getMessage() << std::endl;
    }
  }
};

void print_help(const char *argv0) {
  // clang-format off
  std::cout << std::endl
    << "Usage: " << argv0 << " [OPTIONS] [<file> ...]" << std::endl
    << "  --debug    -d     Print debug info while formatting"                   << std::endl
    << "  --dry-run  -n     Check if formatting needed, but don't apply it"      << std::endl
    << "  --help     -h     Print this help message and exit"                    << std::endl
    << "  --in-place -i     Edit files in place. Default emits file to stdout"   << std::endl
    << "  --version  -v     Print the version and exit"                          << std::endl
    << std::endl;
  // clang-format on
}

void print_version() { std::cout << "wake-format " << VERSION_STR << std::endl; }

void print_cst(CSTElement node, int depth) {
  static uint32_t indent_level = 0;
  for (CSTElement child = node.firstChildElement(); !child.empty(); child.nextSiblingElement()) {
    std::cerr << depth << ": ";
    for (int i = 0; i < depth; i++) {
      std::cerr << "  ";
    }
    std::cerr << symbolName(child.id());
    if (child.isNode()) {
      std::cerr << std::endl;
      print_cst(child, depth + 1);
      continue;
    }
    std::cerr << " (r: " << child.location().start.row << ", c: " << child.location().start.column
              << ", i: " << indent_level << ")";
    switch (child.id()) {
      case TOKEN_ID:
      case TOKEN_INTEGER:
      case TOKEN_DOUBLE:
      case TOKEN_STR_SINGLE:
      case TOKEN_REG_SINGLE:
      case TOKEN_COMMENT:
      case TOKEN_MSTR_BEGIN:
      case TOKEN_MSTR_CONTINUE:
      case TOKEN_MSTR_PAUSE:
      case TOKEN_MSTR_RESUME:
      case TOKEN_MSTR_END:
      case TOKEN_LSTR_BEGIN:
      case TOKEN_LSTR_CONTINUE:
      case TOKEN_LSTR_END:
      case TOKEN_LSTR_MID:
      case TOKEN_LSTR_PAUSE:
      case TOKEN_LSTR_RESUME:
        std::cerr << " -> " << child.fragment().segment().str() << std::endl;
        break;
      case TOKEN_WS: {
        size_t size = child.fragment().segment().size();
        std::cerr << " (" << size << ")" << std::endl;

        if (child.location().start.column == 1) {
          indent_level = size;
        }

        break;
      }
      default:
        std::cerr << std::endl;
        break;
    }
  }
}

DiagnosticReporter *reporter;
int main(int argc, char **argv) {
  TerminalReporter terminalReporter;
  reporter = &terminalReporter;

  // clang-format off
  struct option options[] {
    { 'd',    "debug", GOPT_ARGUMENT_FORBIDDEN},
    { 'n',  "dry-run", GOPT_ARGUMENT_FORBIDDEN},
    { 'h',     "help", GOPT_ARGUMENT_FORBIDDEN},
    { 'i', "in-place", GOPT_ARGUMENT_FORBIDDEN},
    { 'v',  "version", GOPT_ARGUMENT_FORBIDDEN},
    // Undocumented flag for turning off true RNG
    // Used to make messages deterministic for testing
    {   0,   "no-rng", GOPT_ARGUMENT_FORBIDDEN},
    {   0,          0, GOPT_LAST}
  };
  // clang-format on

  argc = gopt(argv, options);
  gopt_errors(argv[0], options);

  bool dry_run = arg(options, "dry-run")->count;
  bool help = arg(options, "help")->count;
  bool in_place = arg(options, "in-place")->count;
  bool version = arg(options, "version")->count;
  bool debug = arg(options, "debug")->count;
  bool no_rng = arg(options, "no-rng")->count;

  if (help) {
    print_help(argv[0]);
    exit(EXIT_SUCCESS);
  }

  if (version) {
    print_version();
    exit(EXIT_SUCCESS);
  }

  if (argc < 2) {
    std::cerr << argv[0] << ": missing files to format" << std::endl;
    exit(EXIT_FAILURE);
  }

  auto seed = (no_rng) ? std::tuple<uint64_t, uint64_t, uint64_t, uint64_t>({0, 0, 0, 0})
                       : wcl::xoshiro_256::get_rng_seed();
  wcl::xoshiro_256 rng(seed);

  bool dry_run_failed = false;

  for (int i = 1; i < argc; i++) {
    std::string name(argv[i]);
    std::string tmp = name + ".tmp." + rng.unique_name();

    ExternalFile external_file = ExternalFile(*reporter, name.c_str());
    if (terminalReporter.errors) {
      std::cerr << argv[0] << ": failed to open file: '" << name << "'" << std::endl << std::endl;
      terminalReporter.report_to_stderr();
      exit(EXIT_FAILURE);
    }

    CST cst = CST(external_file, *reporter);
    if (terminalReporter.errors) {
      std::cerr << argv[0] << ": failed to parse file: '" << name << "'" << std::endl << std::endl;
      terminalReporter.report_to_stderr();
      exit(EXIT_FAILURE);
    }

    if (debug) {
      print_cst(cst.root(), 0);
    }

    auto output_file = std::make_unique<std::ofstream>(tmp);
    Emitter emitter;
    wcl::doc d = emitter.layout(cst);

    d.write(*output_file);
    output_file.reset();

    if (!debug) {
      // Check for bad formatting
      ExternalFile external_file = ExternalFile(*reporter, tmp.c_str());
      CST cst = CST(external_file, *reporter);
      if (terminalReporter.errors) {
        std::cerr << "wake-format failed to format '" << name << "'." << std::endl
                  << "    This is probably due to bad indentation caused by '# wake-format off'"
                  << std::endl
                  << "    Please complete the following steps:" << std::endl
                  << "        1) Submit a copy of '" << name
                  << "' to the wake-format authors so they can improve the tool." << std::endl
                  << "        2) Review '" << tmp << "' for the syntax errors mentioned below."
                  << std::endl
                  << "        3) Edit '" << name
                  << "' to have the correct indentation expected by wake-format" << std::endl
                  << std::endl;

        terminalReporter.report_to_stderr();

        std::cerr << std::endl;

        exit(EXIT_FAILURE);
      }
    }

    if (dry_run) {
      std::vector<std::string> src;
      std::vector<std::string> fmt;

      {
        std::ifstream src_file(name);

        std::string src_line;
        while (std::getline(src_file, src_line)) {
          src.push_back(src_line);
        }
      }

      {
        std::ifstream fmt_file(tmp);

        std::string fmt_line;
        while (std::getline(fmt_file, fmt_line)) {
          fmt.push_back(fmt_line);
        }
      }

      // cleanup the formatting tmp file
      remove(tmp.c_str());

      if (src == fmt) {
        continue;
      }

      auto diff = wcl::diff<std::string>(src.begin(), src.end(), fmt.begin(), fmt.end());
      display_diff(std::cerr, diff);
      dry_run_failed = true;
    }

    if (in_place) {
      // When editing in-place we need to rename the tmp file over the original
      rename(tmp.c_str(), name.c_str());
    } else {
      // print out the resulting file and remove
      std::ifstream src(tmp);
      std::cout << src.rdbuf();
      remove(tmp.c_str());
    }
  }

  if (dry_run && dry_run_failed) {
    exit(EXIT_FAILURE);
  }

  exit(EXIT_SUCCESS);
}
