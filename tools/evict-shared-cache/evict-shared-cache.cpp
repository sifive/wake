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

// Open Group Base Specifications Issue 7
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <string.h>

#include <iostream>
#include <memory>

#include "eviction-policy.h"
#include "gopt/gopt-arg.h"
#include "gopt/gopt.h"

void print_help(const char* argv0) {
  // clang-format off
  std::cout << std::endl
     << "Usage: " << argv0 << " [OPTIONS]" << std::endl
     << "  --cache  DIR     Evict from shared cache DIR"  << std::endl
     << "  --policy POLICY  Evict using POLICY"           << std::endl
     << "  --help   -h      Print this message and exit"  << std::endl
     << "Commands (read from stdin):" << std::endl
     << "  write JOB_ID     JOB_ID was written into the shared cache" << std::endl
     << "  read JOB_ID      JOB_ID was read from the shared cache" << std::endl
     << "Available Policies:" << std::endl
     << "  nil              No op policy. Process commands but do nothing." << std::endl
     << std::endl;
  // clang-format on
}

std::unique_ptr<EvictionPolicy> make_policy(const char* argv0, const char* policy) {
  if (strcmp(policy, "nil") == 0) {
    return std::make_unique<NilEvictionPolicy>();
  }

  std::cout << "Unknown policy: " << policy << std::endl;
  print_help(argv0);
  exit(EXIT_FAILURE);
}

int main(int argc, char** argv) {
  // clang-format off
  struct option options[] {
    {0, "cache", GOPT_ARGUMENT_REQUIRED},
    {0, "policy", GOPT_ARGUMENT_REQUIRED},
    {'h', "help", GOPT_ARGUMENT_FORBIDDEN},
    {0, 0, GOPT_LAST}
  };
  // clang-format on

  argc = gopt(argv, options);
  gopt_errors(argv[0], options);

  bool help = arg(options, "help")->count;
  const char* cache = arg(options, "cache")->argument;
  const char* policy_name = arg(options, "policy")->argument;

  if (help) {
    print_help(argv[0]);
    exit(EXIT_SUCCESS);
  }

  if (!cache) {
    std::cout << "Cache directory not specified" << std::endl;
    print_help(argv[0]);
    exit(EXIT_FAILURE);
  }

  if (!policy_name) {
    std::cout << "Eviction policy not specified" << std::endl;
    print_help(argv[0]);
    exit(EXIT_FAILURE);
  }

  std::unique_ptr<EvictionPolicy> policy = make_policy(argv[0], policy_name);
  policy->init();

  // TODO: Implement the following structure

  // while stdin and stdout open
  //   read chunk from stdin
  //   if byte not null
  //     append to command buffer
  //     continue
  //   else
  //     parse command buffer into json
  //     convert json command into relevant Policy funtion call
  //     maybe send an ack back

  exit(EXIT_SUCCESS);
}
