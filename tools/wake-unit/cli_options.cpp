/*
 * Copyright 2023 SiFive, Inc.
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
#include "../tools/wake/cli_options.h"

#include <string.h>

#include "unit.h"

TEST(cli_options_basic) {
  {
    int argc = 2;
    char *argv[] = {(char *)"wake", (char *)"--help", nullptr};

    CommandLineOptions clo(argc, argv);

    EXPECT_TRUE(clo.help);
    EXPECT_FALSE(clo.init);
    EXPECT_EQUAL(1, clo.argc);
  }

  {
    int argc = 4;
    char *argv[] = {(char *)"wake", (char *)"-v", (char *)"-x", (char *)"Unit", nullptr};

    CommandLineOptions clo(argc, argv);

    EXPECT_TRUE(clo.verbose);
    EXPECT_EQUAL("Unit", clo.exec);
    EXPECT_EQUAL(1, clo.argc);
  }

  {
    int argc = 3;
    char *argv[] = {(char *)"wake", (char *)"--failed", (char *)"--script", nullptr};

    CommandLineOptions clo(argc, argv);

    EXPECT_TRUE(clo.failed);
    EXPECT_TRUE(clo.script);
    EXPECT_EQUAL(1, clo.argc);
  }
}

TEST(cli_options_target) {
  int argc = 4;
  char *argv[] = {(char *)"wake", (char *)"-v", (char *)"build", (char *)"default", nullptr};

  CommandLineOptions clo(argc, argv);

  EXPECT_TRUE(clo.verbose);
  EXPECT_EQUAL(3, clo.argc);
  EXPECT_EQUAL("build", clo.argv[1]);
  EXPECT_EQUAL("default", clo.argv[2]);
}

// Timeline is done poorly and relies on positional args even though it only
// has 3 possible values
TEST(cli_options_timline) {
  {
    int argc = 2;
    char *argv[] = {(char *)"wake", (char *)"--timeline", nullptr};

    CommandLineOptions clo(argc, argv);

    EXPECT_TRUE(clo.timeline);
    EXPECT_EQUAL(1, clo.argc);
  }

  {
    int argc = 3;
    char *argv[] = {(char *)"wake", (char *)"--timeline", (char *)"file-accesses", nullptr};

    CommandLineOptions clo(argc, argv);

    EXPECT_TRUE(clo.timeline);
    EXPECT_EQUAL(2, clo.argc);
    EXPECT_EQUAL("file-accesses", clo.argv[1]);
  }

  {
    int argc = 3;
    char *argv[] = {(char *)"wake", (char *)"--timeline", (char *)"job-reflections", nullptr};

    CommandLineOptions clo(argc, argv);

    EXPECT_TRUE(clo.timeline);
    EXPECT_EQUAL(2, clo.argc);
    EXPECT_EQUAL("job-reflections", clo.argv[1]);
  }

  // This should be a cli error, but isn't
  {
    int argc = 3;
    char *argv[] = {(char *)"wake", (char *)"--timeline", (char *)"invalid-value", nullptr};

    CommandLineOptions clo(argc, argv);

    EXPECT_TRUE(clo.timeline);
    EXPECT_EQUAL(2, clo.argc);
    EXPECT_EQUAL("invalid-value", clo.argv[1]);
  }
}

TEST(cli_options_shebang) {
  // This should be a cli error, but isn't
  {
    int argc = 3;
    char *argv[] = {(char *)"wake", (char *)"-:", (char *)"funcName", nullptr};

    CommandLineOptions clo(argc, argv);

    EXPECT_EQUAL("funcName", clo.shebang);
    EXPECT_EQUAL(1, clo.argc);
  }

  {
    int argc = 4;
    char *argv[] = {(char *)"wake", (char *)"-:", (char *)"funcName", (char *)"./in/directory",
                    nullptr};

    CommandLineOptions clo(argc, argv);

    EXPECT_EQUAL("funcName", clo.shebang);
    EXPECT_EQUAL(2, clo.argc);
    EXPECT_EQUAL("./in/directory", clo.argv[1]);
  }
}
