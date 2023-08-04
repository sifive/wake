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

#include <cstring>
#include <set>

#include "json/json5.h"
#include "util/diagnostic.h"
#include "util/term.h"

DiagnosticReporter* reporter;

struct Test {
  std::string test_name;
  TestFunc test;
  std::set<std::string> tags;
  Test(const char* test_name_, TestFunc test_, std::set<std::string> tags)
      : test_name(test_name_), test(test_), tags(tags) {}
};

// A global list of all the tests to run. Its kept static so that
// nothing can interfear with it except things in this file, which
// is only TestRegister and main itself. We have to make it static
// local so that it can be initied before anything else no matter
// what order things are linked in.
static std::vector<Test>* get_tests() {
  static std::vector<Test> tests;
  return &tests;
}

TestRegister::TestRegister(const char* test_name, TestFunc test,
                           std::initializer_list<const char*> tags) {
  std::set<std::string> test_tags;
  for (const auto* tag : tags) {
    test_tags.emplace(tag);
  }
  get_tests()->emplace_back(test_name, test, std::move(test_tags));
}

//
bool matches_prefix(const std::vector<std::string>& prefixes, const Test& test) {
  // If there are no prefixes, everything matches
  if (prefixes.empty()) return true;

  // Otherwise at least one of the prefixes should match.
  for (const auto& prefix : prefixes) {
    if (test.test_name.find(prefix) == 0) {
      return true;
    }
  }
  return false;
}

bool matches_tags(const std::set<std::string>& tags, const Test& test) {
  // In order for a test to match the tags, its tags must be a subset of
  // of the tags the user specified. This prevents tests tagged with "large"
  // from running unless the user specifies the "large" tag
  for (const auto& tag : test.tags) {
    if (tags.count(tag) == 0) {
      return false;
    }
  }
  return true;
}

int main(int argc, char** argv) {
  bool no_color = false;
  std::vector<std::string> prefixes;
  std::set<std::string> tags;
  for (int i = 1; i < argc; ++i) {
    const char* arg = argv[i];
    if (strcmp(arg, "--no-color") == 0) {
      no_color = true;
    }
    if (strcmp(arg, "--prefix") == 0) {
      prefixes.emplace_back(argv[++i]);
    }
    if (strcmp(arg, "--tag") == 0) {
      tags.emplace(argv[++i]);
    }
  }
  term_init(true, true);

  auto res = JsonSubscriber::fd_t::open("wake.log");
  if (!res) {
    std::cerr << "Unable to init logging: wake.log failed to open: " << strerror(res.error())
              << std::endl;
  }
  wcl::log::subscribe(std::make_unique<JsonSubscriber>(std::move(*res)));

  TestLogger logger;
  std::set<std::string> failed_tests;
  std::set<std::string> passing_tests;
  for (auto& test : *get_tests()) {
    // Set this logjump in case this test fails
    size_t num_errors = logger.errors.size();
    logger.test_name = test.test_name.c_str();
    if (setjmp(logger.return_jmp_buffer)) {
      // Now that we've made it in here we know that a test has assert failed.
      // We still want to keep running all the other tests however so we keep going.
      // The failure will be logged so we can catch it later.
      continue;
    }
    if (!matches_prefix(prefixes, test)) continue;
    if (!matches_tags(tags, test)) continue;
    // If this returns then we won't long jump
    test.test(logger);
    if (num_errors == logger.errors.size()) passing_tests.emplace(test.test_name);
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
