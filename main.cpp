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

#include <iostream>

#include "lexer.h"
#include "parser.h"
#include "syntax.h"
#include "file.h"
#include "reporter.h"
#include "cst.h"

class ConsoleReporter : public Reporter {
  void report(int severity, Location location, const std::string &message);
};

void ConsoleReporter::report(int severity, Location location, const std::string &message) {
  std::cerr << location << ": " << message << std::endl;
}

int main(int argc, const char **argv) {
    ConsoleReporter reporter;
    ExternalFile file(reporter, argv[1]);
    CSTBuilder builder(file);
    parseWake(ParseInfo(&file, &builder, &reporter));
    CST cst(std::move(builder));
    return 0;
}
