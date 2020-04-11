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

#include "describe.h"
#include "database.h"
#include "shell.h"
#include "execpath.h"
#include <iostream>
#include <string>

#define SHORT_HASH 8

static void indent(const std::string& tab, const std::string& body) {
  size_t i, j;
  for (i = 0; (j = body.find('\n', i)) != std::string::npos; i = j+1) {
    std::cout << "\n" << tab;
    std::cout.write(body.data()+i, j-i);
  }
  std::cout.write(body.data()+i, body.size()-i);
  std::cout << std::endl;
}

static void describe_human(const std::vector<JobReflection> &jobs, bool debug, bool verbose) {
  for (auto &job : jobs) {
    std::cout
      << "Job " << job.job << ":" << std::endl
      << "  Command-line:";
    for (auto &arg : job.commandline) std::cout << " " << shell_escape(arg);
    std::cout
      << std::endl
      << "  Environment:" << std::endl;
    for (auto &env : job.environment)
      std::cout << "    " << shell_escape(env) << std::endl;
    std::cout
      << "  Directory: " << job.directory << std::endl
      << "  Built:     " << job.time << std::endl
      << "  Runtime:   " << job.usage.runtime << std::endl
      << "  CPUtime:   " << job.usage.cputime << std::endl
      << "  Mem bytes: " << job.usage.membytes << std::endl
      << "  In  bytes: " << job.usage.ibytes << std::endl
      << "  Out bytes: " << job.usage.obytes << std::endl
      << "  Status:    " << job.usage.status << std::endl
      << "  Stdin:     " << job.stdin_file << std::endl;
    if (verbose) {
      std::cout << "Visible:" << std::endl;
      for (auto &in : job.visible)
        std::cout << "  " << in.hash.substr(0, verbose?std::string::npos:SHORT_HASH)
                  << " " << in.path << std::endl;
    }
    std::cout << "Inputs:" << std::endl;
    for (auto &in : job.inputs)
      std::cout << "  " << in.hash.substr(0, verbose?std::string::npos:SHORT_HASH)
                << " " << in.path << std::endl;
    std::cout << "Outputs:" << std::endl;
    for (auto &out : job.outputs)
      std::cout << "  " << out.hash.substr(0, verbose?std::string::npos:SHORT_HASH)
                << " " << out.path << std::endl;
    if (debug) {
      std::cout << "Stack:";
      indent("  ", job.stack);
    }
    if (!job.stdout_payload.empty()) {
      std::cout << "Stdout:";
      indent("  ", job.stdout_payload);
    }
    if (!job.stderr_payload.empty()) {
      std::cout << "Stderr:";
      indent("  ", job.stderr_payload);
    }
  }
}

static void describe_shell(const std::vector<JobReflection> &jobs, bool debug, bool verbose) {
  std::cout << "#! /bin/sh -ex" << std::endl;

  for (auto &job : jobs) {
    std::cout << std::endl << "# Wake job " << job.job << ":" << std::endl;
    std::cout << "cd " << shell_escape(get_cwd()) << std::endl;
    if (job.directory != ".") {
      std::cout << "cd " << shell_escape(job.directory) << std::endl;
    }
    std::cout << "env -i \\" << std::endl;
    for (auto &env : job.environment) {
      std::cout << "\t" << shell_escape(env) << " \\" << std::endl;
    }
    for (auto &arg : job.commandline) {
      std::cout << shell_escape(arg) << " \\" << std::endl << '\t';
    }
    std::cout << "< " << shell_escape(job.stdin_file) << std::endl << std::endl;
    std::cout
      << "# When wake ran this command:" << std::endl
      << "#   Built:     " << job.time << std::endl
      << "#   Runtime:   " << job.usage.runtime << std::endl
      << "#   CPUtime:   " << job.usage.cputime << std::endl
      << "#   Mem bytes: " << job.usage.membytes << std::endl
      << "#   In  bytes: " << job.usage.ibytes << std::endl
      << "#   Out bytes: " << job.usage.obytes << std::endl
      << "#   Status:    " << job.usage.status << std::endl;
    if (verbose) {
      std::cout << "# Visible:" << std::endl;
      for (auto &in : job.visible)
        std::cout << "#  " << in.hash.substr(0, verbose?std::string::npos:SHORT_HASH)
                  << " " << in.path << std::endl;
    }
    std::cout
      << "# Inputs:" << std::endl;
    for (auto &in : job.inputs)
      std::cout << "#  " << in.hash.substr(0, verbose?std::string::npos:SHORT_HASH)
                << " " << in.path << std::endl;
    std::cout << "# Outputs:" << std::endl;
    for (auto &out : job.outputs)
      std::cout << "#  " << out.hash.substr(0, verbose?std::string::npos:SHORT_HASH)
                << " " << out.path << std::endl;
    if (debug) {
      std::cout << "# Stack:";
      indent("#   ", job.stack);
    }
    if (!job.stdout_payload.empty()) {
      std::cout << "# Stdout:";
      indent("#   ", job.stdout_payload);
    }
    if (!job.stderr_payload.empty()) {
      std::cout << "# Stderr:";
      indent("#   ", job.stderr_payload);
    }
  }
}

void describe(const std::vector<JobReflection> &jobs, bool script, bool debug, bool verbose) {
  if (script) {
    describe_shell(jobs, debug, verbose);
  } else {
    describe_human(jobs, debug, verbose);
  }
}
