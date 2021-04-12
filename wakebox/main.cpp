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
		"Usage: wakebox [OPTIONS] [COMMAND...]                                                         \n"
		"                                                                                              \n"
		"Interactive options                                                                           \n"
		"    -r --rootfs FILE         Use a squashfs file as the command's view of the root filesystem.\n"
		"    -t --toolchain FILE      Make a toolchain visible on the command's view of the filesystem.\n"
		"                             May be specified multiple times.                                 \n"
		"    -b --bind DIR1:DIR2      Place the directory (or file) at DIR1 within the command's view  \n"
		"                             of the filesystem at location DIR2.                              \n"
		"                             May be specified multiple times.                                 \n"
		"    -B --no-bind-home        Don't place the current users home directory within the command's\n"
		"                             view of the filesystem.                                          \n"
		"    COMMAND                  The command to run.                                              \n"
		"                                                                                              \n"
		"Batch options                                                                                 \n"
		"    -p --params FILE         Json file specifying input parameters. Above interactive options \n"
		"                             will be ignored.                                                 \n"
		"    -o --output-stats FILE   Json file written to with output results                         \n"
		"    -s --force-shell         Run shell instead of command from params file.                   \n"
		"                             Implies --allow-interactive.                                     \n"
		"                             Use 'eval $WAKEBOX_CMD' to run the command from params file.     \n"
		"    -i --allow-interactive   Use default stdin, ignoring the params json file's stdin value.  \n"
		"                                                                                              \n"
		"Other options                                                                                 \n"
		"    -h --help                Print usage                                                      \n";
}

void arg_vstr(struct option opts[], const char *name, std::vector<std::string>& result) {
	for (int i = 0; !(opts[i].flags & GOPT_LAST); ++i) {
		if (!strcmp(opts[i].long_name, name)) {
			char *val = opts[i].argument;
			if (val)
				result.push_back(val);
		}
	}
}

std::string arg_str(struct option opts[], const char *name) {
	struct option *o = arg(opts, name);
	if (!o->argument)
		return "";
	return std::string(o->argument);
}

int run_interactive(
	const std::string &rootfs,
	const std::vector<std::string> toolchains,
	const std::vector<std::string> binds,
	const std::vector<std::string> command,
	const bool bind_home
){

	struct fuse_args fa;
	fa.command = command;
	fa.userid = 0;
	fa.groupid = 0;
	fa.daemon_path = find_execpath() + "/../lib/wake/fuse-waked";

	if (!rootfs.empty())
		fa.mount_ops.push_back({"squashfs", rootfs, "/", false});

	for (auto &tool : toolchains) {
		if (!tool.empty())
			fa.mount_ops.push_back({"squashfs", tool, "", false});
	}

	const char *home_val = std::getenv("HOME");
	std::string home_var = std::string("HOME=") + home_val;
	std::string user_var = std::string("USER=") + std::getenv("USER");

	if (rootfs.empty()) {
		fa.environment.push_back(home_var);
		fa.environment.push_back(user_var);
		fa.working_dir = get_cwd();

	} else if (!rootfs.empty() && bind_home) {
		fa.mount_ops.push_back({"create-dir", "", home_val, ""});
		fa.mount_ops.push_back({"bind", home_val, home_val, false});
		fa.environment.push_back(home_var);
		fa.environment.push_back(user_var);
		fa.working_dir = get_cwd();

	} else if (!rootfs.empty() && !bind_home) {
		fa.working_dir = "/";
	}

	const char *term = std::getenv("TERM");
	fa.environment.push_back(std::string("TERM=") + term);

	std::string result;
	return run_in_fuse(fa, result);
}


int run_batch(
	const char* params_path,
	bool has_output,
	bool use_stdin_file,
	bool use_shell,
	const char* result_path
){
	// Read the params file
	std::ifstream ifs(params_path);
	const std::string json(
		(std::istreambuf_iterator<char>(ifs)),
		(std::istreambuf_iterator<char>()));
	if (ifs.fail()) {
		std::cerr << "read " << params_path << ": " << strerror(errno) << std::endl;
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

	if (args.command.empty() || args.command[0].empty()) {
		std::cerr << "No command was provided." << std::endl;
		return 1;
	}

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

int main(int argc, char *argv[])
{
	struct option options[] {
		{'r', "rootfs",       GOPT_ARGUMENT_REQUIRED},
		{'t', "toolchain",    GOPT_ARGUMENT_REQUIRED | GOPT_REPEATABLE}, // TODO gopt repeatable isn't enough..
	//	{'b', "bind",         GOPT_ARGUMENT_REQUIRED | GOPT_REPEATABLE}, // TODO not implemented
		{'N', "no-bind-home", GOPT_ARGUMENT_FORBIDDEN},

		{'p', "params",       GOPT_ARGUMENT_REQUIRED},
		{'o', "output-stats", GOPT_ARGUMENT_REQUIRED},
		{'i', "interactive",  GOPT_ARGUMENT_FORBIDDEN},
		{'s', "force-shell",  GOPT_ARGUMENT_FORBIDDEN},

		{'h', "help",         GOPT_ARGUMENT_FORBIDDEN},
		{0,   0,              GOPT_LAST }
	};

	argc = gopt(argv, options);
	gopt_errors("wakebox", options);

	bool has_help = arg(options, "help")->count > 0;
	bool has_params_file = arg(options, "params")->count > 0;
	bool has_positional_cmd = argc > 1;

	if (has_help) {
		print_help();
		return 1;
	}

	if (has_positional_cmd && has_params_file) {
		std::cerr
			<< "The batch mode --params argument can't be used with the interactive mode command argument."
			<< std::endl;
		return 1;
	}

	if (has_positional_cmd) {
		const std::string rootfs = arg_str(options, "rootfs");
		const bool bind_home = arg(options, "no-bind-home")->count == 0;

		std::vector<std::string> toolchains;
		arg_vstr(options, "toolchain", toolchains);

		std::vector<std::string> binds;
		arg_vstr(options, "bind", binds);

		std::vector<std::string> positional_params;
		for (int i = 1; i < argc; i++)
			positional_params.push_back(argv[i]);
		if (positional_params.empty()) {
			std::cerr << "Must provide a command." << std::endl;
			return 1;
		}
		return run_interactive(rootfs, toolchains, binds, positional_params, bind_home);

	} else if (has_params_file) {
		const char* params  = arg(options, "params" )->argument;
		bool has_output = arg(options, "output-stats")->count > 0;
		bool use_stdin_file = arg(options, "interactive")->count == 0;
		bool use_shell = arg(options, "force-shell")->count > 0;
		const char* result_path = arg(options, "output-stats")->argument;
		return run_batch(params, has_output, use_stdin_file, use_shell, result_path);
	}

	print_help();
	return 1;
}
