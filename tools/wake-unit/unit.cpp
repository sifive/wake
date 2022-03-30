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

#include "unit.h"
#include "util/colour.h"
#include "util/diagnostic.h"

#include <set>
#include <cstring>

DiagnosticReporter *reporter;

struct Test {
  const char* test_name;
  TestFunc test;
  Test(const char* test_name_, TestFunc test_) : test_name(test_name_), test(test_) {}
};

// TODO: sequester this off to its own file and make it static
// so that only TestRegister and the test runner can access it.
std::vector<Test> tests__;

TestRegister::TestRegister(const char* test_name, TestFunc test) {
  tests__.emplace_back(test_name, test);
}

int main(int argc, char** argv) {
  bool no_color = false;
  if (argc > 2) {
    std::cerr << "Too many arguments to be valid, --no-color is the only valid argument";
    return 1;
  }
  if (argc == 2) {
    if (strcmp(argv[1],"--no-color") == 0) no_color = true;
    else {
      std::cerr << "`" << argv[1] << "` is not a valid argument, --no-color is the only valid option";
      return 1;
    }
  }
  term_init(true);
  TestLogger logger;
  std::set<std::string> failed_tests;
  std::set<std::string> passing_tests;
  for (auto& test : tests__) {
    // Set this logjump in case this test fails
    size_t num_errors = logger.errors.size();
    logger.test_name = test.test_name;
    if(setjmp(logger.return_jmp_buffer)) {
      // Now that we've made it in here we know that a test has assert failed.
      // We still want to keep running all the other tests however so we keep going.
      // The failure will be logged so we can catch it later.
      continue;
    }
    // If this returns then we won't long jump
    test.test(logger);
    if (num_errors == logger.errors.size())
      passing_tests.emplace(test.test_name);
  }

  for (auto& err : logger.errors) {
    if (!no_color) std::cerr << term_intensity(2);
    std::cerr << err->file << ":" << err->line << ": ";
    if (!no_color) std::cerr << term_colour(TERM_RED);
    std::cerr << "error: ";
    if (!no_color) std::cerr << term_normal();
    std::cerr << std::endl;
    std::string msg = err->user_error.str();
    if (msg.size() > 0) {
      std::cerr << msg << std::endl;
    }
    std::cerr << err->predicate_error.str() << std::endl;
    failed_tests.emplace(err->test_name);
  }

  if (failed_tests.size()) {
    if (!no_color) std::cerr << term_colour(TERM_RED);
    std::cerr << "FAILED:" << std::endl;
    for (auto& test_name : failed_tests) {
      std::cerr << "  " << test_name << std::endl;
    }
  }

  if (passing_tests.size()) {
    if (!no_color) std::cout << term_colour(TERM_GREEN);
    std::cout << "PASSED:" << std::endl;
    for (auto& test_name : passing_tests) {
      std::cout << "  " << test_name << std::endl;
    }
  }

  if (failed_tests.size()) {
    if (!no_color) std::cerr << term_normal();
    std::cerr << "\n\nFAILURE" << std::endl;
    return 1;
  } else {
    if (!no_color) std::cout << term_normal();
    std::cout << "\n\nSUCCESS" << std::endl;
    return 0;
  }
}
