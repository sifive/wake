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

#include <iostream>
#include <iomanip>
#include <sstream>
#include <vector>
#include <csetjmp>

#include "json/json5.h"
#include "util/colour.h"

// Represents an error message to display to the user
struct ErrorMessage {
  // Test and failure location information
  const char* test_name;
  const char* file;
  int line;

  // The generated error message with precise details
  std::stringstream predicate_error;

  // The error message supplied by the user
  std::stringstream user_error;
};

// A class that handles the return value from EXPECT_* and ASSERT_*
// On assert failure it longjumps back to the test harness. Allows
// the user to add specilized messages to errors like gtest. If the
// the test did not fail, the user supplied messages are ignored.
struct TestStream {
  std::stringstream* ss;
  std::jmp_buf* assert_throw;
  TestStream(std::stringstream* ss_, std::jmp_buf* assert_) : ss(ss_), assert_throw(assert_) {}
  ~TestStream() {
    if (assert_throw) std::longjmp(*assert_throw, 1);
  }
  template <class T>
  TestStream& operator<<(T&& x) {
    if (ss) *ss << x;
    return *this;
  }
};

// Public:
struct TestLogger {
  std::vector<ErrorMessage> errors;
  std::jmp_buf return_jmp_buffer;
  const char* test_name = nullptr;

private:
  TestStream fail(ErrorMessage& err, bool assert) {
    return TestStream(&err.user_error, assert ? &return_jmp_buffer : nullptr);
  }

public:
  TestStream expect(bool assert, bool expected, bool cond, const char* cond_str, int line, const char* file) {
    if (cond == expected) return TestStream(nullptr, nullptr);
    auto expected_str = expected ? "true" : "false";
    auto actual_str = cond ? "true" : "false";
    errors.emplace_back();
    auto& err = errors.back();
    err.test_name = test_name;
    err.file = file;
    err.line = line;
    err.predicate_error << "Expected " << term_colour(TERM_MAGENTA) << "`" << cond_str << "`";
    err.predicate_error << term_normal() << " to be ";
    err.predicate_error << term_colour(TERM_MAGENTA) << expected_str;
    err.predicate_error << term_normal() << ", but was found to be ";
    err.predicate_error << term_colour(TERM_MAGENTA) << actual_str;
    err.predicate_error << term_normal() << std::endl;
    return fail(err, assert);
  }

  TestStream expect_equal(bool assert, std::string expected, std::string actual, const char* expected_str, const char* actual_str, int line, const char* file) {
    if (expected == actual) return TestStream(nullptr, nullptr);
    errors.emplace_back();
    auto& err = errors.back();
    err.test_name = test_name;
    err.file = file;
    err.line = line;
    err.predicate_error << "Expected:\n\t" << term_colour(TERM_MAGENTA) << json_escape(expected);
    err.predicate_error << term_normal() << "\nBut got:\n\t";
    err.predicate_error << term_colour(TERM_MAGENTA) << json_escape(actual);
    err.predicate_error << term_normal() << std::endl;
    return fail(err, assert);
  }

  template <class T>
  TestStream expect_equal(bool assert, T&& expected, T&& actual, const char* expected_str, const char* actual_str, int line, const char* file) {
    if (expected == actual) return TestStream(nullptr, nullptr);
    errors.emplace_back();
    auto& err = errors.back();
    err.test_name = test_name;
    err.file = file;
    err.line = line;
    err.predicate_error << "Expected " << term_colour(TERM_MAGENTA) << "`" << expected_str << "`";
    err.predicate_error << term_normal() << " to be equal to `";
    err.predicate_error << term_colour(TERM_MAGENTA) << actual_str;
    err.predicate_error << "`" << term_normal() << ", but was found to differ";
    return fail(err, assert);
  }
};

// Public:
#define EXPECT_TRUE(cond) (logger__.expect(false, true, (cond), #cond, __LINE__, __FILE__))
#define ASSERT_TRUE(cond) (logger__.expect(true, true, (cond), #cond, __LINE__, __FILE__))
#define EXPECT_FALSE(cond) (logger__.expect(false, false, (cond), #cond, __LINE__, __FILE__))
#define ASSERT_FALSE(cond) (logger__.expect(true, false, (cond), #cond, __LINE__, __FILE__))
#define EXPECT_EQUAL(x, y) (logger__.expect_equal(false, (x), (y), #x, #y, __LINE__, __FILE__))
#define ASSERT_EQUAL(x, y) (logger__.expect_equal(true, (x), (y), #x, #y, __LINE__, __FILE__))

using TestFunc = void(*)(TestLogger&);

struct TestRegister {
  TestRegister(const char* test_name, TestFunc test);
};

template <class T>
struct TestRegisterUnique {
  static TestRegister unique_register;
};

#define TEST(name)\
namespace {struct Test__Unique__ ## name {};}\
static void Test__ ## name (TestLogger&);\
template<> TestRegister TestRegisterUnique<Test__Unique__ ## name>::unique_register(#name, Test__ ## name);\
static void Test__ ## name (TestLogger& logger__)
