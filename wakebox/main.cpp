/* Wake FUSE launcher to capture inputs/outputs
 *
 * Copyright 2021 SiFive, Inc.
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

#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <fcntl.h>
#include <unistd.h>

#include "execpath.h"
#include "fuse.h"
#include "gopt.h"
#include "gopt-arg.h"
#include "shell.h"

void print_help() {
	std::cout <<
		"Usage: wakebox [OPTIONS]\n"
		"Options\n"
		"    -p --params FILE         Json file specifying input parameters\n"
		"    -o --output-stats FILE   Json file written to with output results\n"
		"    -i --allow-interactive   Ignore params json file's stdin value\n"
		"    -s --force-shell         Run shell instead of command from params.\n"
		"                             Implies --allow-interactive.\n"
		"                             Use 'eval $WAKEBOX_CMD' to run the command from params.\n"
		"    -h --help                Print usage\n";
}

int main(int argc, char *argv[])
{
	struct option options[] {
		{'p', "params",       GOPT_ARGUMENT_REQUIRED},
		{'o', "output-stats", GOPT_ARGUMENT_REQUIRED},
		{'i', "interactive",  GOPT_ARGUMENT_FORBIDDEN},
		{'s', "force-shell",  GOPT_ARGUMENT_FORBIDDEN},
		{'h', "help",         GOPT_ARGUMENT_FORBIDDEN},
		{0,   0,              GOPT_LAST }
	};

	argc = gopt(argv, options);
	gopt_errors("wakebox", options);

	bool has_output = arg(options, "output-stats")->count > 0;
	bool use_stdin_file = arg(options, "interactive")->count == 0;
	bool use_shell = arg(options, "force-shell")->count > 0;

	const char* params  = arg(options, "params" )->argument;
	const char* result_path = arg(options, "output-stats")->argument;

	if (arg(options, "help")->count > 0 || !params) {
		print_help();
		return 1;
	}

	// Read the params file
	std::ifstream ifs(params);
	const std::string json(
		(std::istreambuf_iterator<char>(ifs)),
		(std::istreambuf_iterator<char>()));
	if (ifs.fail()) {
		std::cerr << "read " << argv[1] << ": " << strerror(errno) << std::endl;
		return 1;
	}
	ifs.close();

	fuse_args args;
	if (!json_as_struct(json, args)) {
		return 1;
	}
	args.daemon_path = find_execpath() + "/../lib/wake/fuse-waked";
	args.working_dir = get_cwd();
	args.use_stdin_file = use_stdin_file;

	if (use_shell) {
		std::stringstream ss;
		ss << "WAKEBOX_CMD=";
		for (auto &s : args.command)
			ss << shell_escape(s) << " ";
		std::string escaped = ss.str();
		args.environment.push_back(escaped.substr(0, escaped.length()-1));

		args.use_stdin_file = false;
		args.command = {"/bin/sh"};
		std::cerr << "To execute the original command:\n\teval $WAKEBOX_CMD" << std::endl;
	}

	std::string result;
	if (!has_output)
		return run_in_fuse(args, result);

	// Open the output file
	int out_fd = open(result_path, O_WRONLY | O_CREAT | O_CLOEXEC, 0664);
	if (out_fd < 0) {
		std::cerr << "open " << result_path << ": " << strerror(errno) << std::endl;
		return 1;
	}

	int res = run_in_fuse(args, result);
	// write output stats as json
	ssize_t wrote = write(out_fd, result.c_str(), result.length());
	if (wrote == -1)
		return errno;

	if (0 != close(out_fd))
		return errno;

	return res;
}
