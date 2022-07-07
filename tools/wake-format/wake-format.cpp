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

#include <fstream>
#include <iostream>
#include <memory>
#include <vector>

#include "gopt/gopt-arg.h"
#include "gopt/gopt.h"
#include "parser/cst.h"
#include "parser/parser.h"
#include "parser/syntax.h"
#include "util/diagnostic.h"
#include "util/file.h"

#define VERSION "0.0.1"

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

void print_version() { std::cout << "wake-format " << VERSION << std::endl; }

void print_cst(CSTElement node, int depth) {
  for (CSTElement child = node.firstChildElement(); !child.empty(); child.nextSiblingElement()) {
    std::cout << depth << ": ";
    for (int i = 0; i < depth; i++) {
      std::cout << "  ";
    }
    std::cout << symbolExample(child.id()) << " " << (child.isNode() ? "[Node]" : "[Token]");
    if (child.isNode()) {
      std::cout << std::endl;
      print_cst(child, depth + 1);
      continue;
    }
    switch (child.id()) {
      case TOKEN_ID:
      case TOKEN_INTEGER:
      case TOKEN_DOUBLE:
      case TOKEN_STR_SINGLE:
      case TOKEN_REG_SINGLE:
        std::cout << " -> " << child.fragment().segment().str() << std::endl;
        break;
      default:
        std::cout << std::endl;
        break;
    }
  }
}

void walk_cst(std::ostream *out, CSTElement node) {
  for (CSTElement child = node.firstChildElement(); !child.empty(); child.nextSiblingElement()) {
    if (child.isNode()) {
      walk_cst(out, child);
      continue;
    }

    if (child.id() == TOKEN_KW_MACRO_HERE) {
      *out << "@here";
    } else {
      *out << child.fragment().segment().str();
    }
  }
}

DiagnosticReporter *reporter;
int main(int argc, char **argv) {
  TerminalReporter terminalReporter;
  reporter = &terminalReporter;

  // clang-format off
  struct option options[] {
    {'d', "debug", GOPT_ARGUMENT_FORBIDDEN},
    {'n', "dry-run", GOPT_ARGUMENT_FORBIDDEN},
    {'h', "help", GOPT_ARGUMENT_FORBIDDEN},
    {'i', "in-place", GOPT_ARGUMENT_FORBIDDEN},
    {'v', "version", GOPT_ARGUMENT_FORBIDDEN},
    {0, 0, GOPT_LAST}
  };
  // clang-format on

  argc = gopt(argv, options);
  gopt_errors(argv[0], options);

  bool dry_run = arg(options, "dry-run")->count;
  bool help = arg(options, "help")->count;
  bool in_place = arg(options, "in-place")->count;
  bool version = arg(options, "version")->count;
  bool debug = arg(options, "debug")->count;

  if (help) {
    print_help(argv[0]);
    exit(EXIT_SUCCESS);
  }

  if (version) {
    print_version();
    exit(EXIT_SUCCESS);
  }

  if (dry_run) {
    std::cout << "wake-format: dry-run not yet supported" << std::endl;
    exit(EXIT_FAILURE);
  }

  if (argc < 2) {
    std::cerr << argv[0] << ": missing files to format" << std::endl;
    exit(EXIT_FAILURE);
  }

  for (int i = 1; i < argc; i++) {
    ExternalFile file = ExternalFile(*reporter, argv[i]);
    CST cst = CST(file, *reporter);
    if (terminalReporter.errors) {
      std::cerr << argv[0] << ": failed to parse file: " << argv[i] << std::endl;
      exit(EXIT_FAILURE);
    }

    std::unique_ptr<std::ofstream> output_file;
    std::ostream *out;

    std::string name(argv[i]);
    std::string tmp = name + ".tmp";
    if (in_place) {
      output_file = std::make_unique<std::ofstream>(tmp);
      out = output_file.get();
    } else {
      out = &std::cout;
    }

    if (debug) {
      print_cst(cst.root(), 0);
    }

    walk_cst(out, cst.root());

    // When editing in-place we need to copy the tmp file over the original
    if (in_place) {
      output_file.reset();
      rename(tmp.c_str(), name.c_str());
    }
  }

  exit(EXIT_SUCCESS);
}
