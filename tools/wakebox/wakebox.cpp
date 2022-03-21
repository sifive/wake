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

// Open Group Base Specifications Issue 7
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "gopt/gopt.h"
#include "gopt/gopt-arg.h"
#include "util/shell.h"
#include "util/execpath.h"
#include "wakefs/fuse.h"


void print_help() {
	const std::string interactive =
	"Interactive options                                                                           \n"
	"    -r --rootfs FILE         Use a squashfs file as the command's view of the root filesystem.\n"
	"    -t --toolchain FILE      Make a toolchain visible on the command's view of the filesystem.\n"
	"                             May be specified multiple times.                                 \n"
	"    -b --bind DIR1:DIR2      Place the directory (or file) at DIR1 within the command's view  \n"
	"                             of the filesystem at location DIR2.                              \n"
	"                             May be specified multiple times.                                 \n"
	"    -x                       Shorthand for '--bind $PWD:$PWD'                                 \n"
	"    COMMAND                  The command to run.                                              \n";

	const std::string batch_and_help =
	"Batch options                                                                                 \n"
	"    -p --params FILE         Json file specifying input parameters.                           \n"
	"    -o --output-stats FILE   Json file written containing output results and return code.     \n"
	"    -s --force-shell         Run shell instead of command from params file.                   \n"
	"                             Implies --allow-interactive.                                     \n"
	"                             Use 'eval $WAKEBOX_CMD' to run the command from params file.     \n"
	"    -i --allow-interactive   Use default stdin, ignoring the params json file's stdin value.  \n"
	"    -I --isolate-retcode     Don't allow COMMAND's return code to impact wakebox's return code.\n"
	"                                                                                              \n"
	"Other options                                                                                 \n"
	"    -h --help                Print usage                                                      \n";

#ifdef __linux__
	std::cout
		<< "Usage: wakebox [OPTIONS] [COMMAND...]\n\n"
		<< interactive
		<< "\n"
		<< batch_and_help;
#else
	std::cout
		<< "Usage: wakebox [OPTIONS]\n\n"
		<< "NOTE: Reduced command line options due to operating system support.\n"
		<< "      Mount options, uid/gid control and network isolation in the input parameters file\n"
		<< "      will be ignored.\n\n"
		<< batch_and_help;
#endif
}

// Decide the default working directory for the new process.
// Wakebox does not provide direct control of the command running dir at this time.
const std::string pick_running_dir(const struct fuse_args &fa)
{
#ifdef __linux__
	// If we have a workspace mount, we want to default to that location.
	for (auto &x : fa.mount_ops) {
		if (x.type == "workspace") {
			if (x.destination[0] == '/')
				return x.destination + "/" + fa.directory;
			else
				// convert a workspace relative path into absolute path
				return fa.working_dir + "/" + x.destination + "/" + fa.directory;
		}
	}
	// If we're binding in the parent namespace's current working directory, use that.
	for (auto &x : fa.mount_ops)
		if (x.type == "bind" && fa.working_dir == x.source)
			return x.destination + "/" + fa.directory;

	// If we have a replacement rootfs, we know we atleast have "/".
	for (auto &x : fa.mount_ops)
		if (x.destination == "/")
			return "/" + fa.directory;

	// Try the current directory, which should exist if we have no replacement rootfs.
	return fa.working_dir + "/" + fa.directory;
#else
	// On non-linux platforms like MacOS, run_in_fuse is unable to re-map the fuse
	// mountpoint over the top of the original workspace.
	// It may expose the temporary fuse mountpoint as a component of absolute paths.
	return fa.daemon.mount_subdir + "/" + fa.directory;
#endif

}

// Interactive mode does not provide userid control at this time.
// Allows networking by default, no user control yet.
int run_interactive(
	const std::string &rootfs,
	const std::vector<std::string> &toolchains,
	const std::vector<mount_op> &binds,
	const std::vector<std::string> &command
){
	struct fuse_args fa(get_cwd(), false);
	fa.command = command;

	if (!rootfs.empty())
		fa.mount_ops.push_back({"squashfs", rootfs, "/", false});

	for (auto &tool : toolchains)
		fa.mount_ops.push_back({"squashfs", tool, "", false});

	fa.mount_ops.insert(fa.mount_ops.end(), binds.begin(), binds.end());

	if (rootfs.empty()) {
		fa.environment.push_back(std::string("HOME=") + std::getenv("HOME"));
		fa.environment.push_back(std::string("USER=") + std::getenv("USER"));
	}

	const char *term = std::getenv("TERM");
	fa.environment.push_back(std::string("TERM=") + term);

	fa.command_running_dir = pick_running_dir(fa);

	int retcode;
	std::string result;
	if (!run_in_fuse(fa, retcode, result))
		return 1;
	return retcode;
}


int run_batch(
	const char* params_path,
	bool has_output,
	bool use_stdin_file,
	bool use_shell,
	bool isolate_retcode,
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

	fuse_args args(get_cwd(), use_stdin_file);
	if (!json_as_struct(json, args))
		return 1;
	args.command_running_dir = pick_running_dir(args);

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

	int retcode;
	std::string result;
	if (!has_output) {
		if (!run_in_fuse(args, retcode, result))
			return 1;

		if (isolate_retcode)
			return 0;
		else
			return retcode;
	}

	// Open the output file
	int out_fd = open(result_path, O_WRONLY | O_CREAT | O_CLOEXEC, 0664);
	if (out_fd < 0) {
		std::cerr << "open " << result_path << ": " << strerror(errno) << std::endl;
		return 1;
	}

	if (!run_in_fuse(args, retcode, result))
		return 1;

	// write output stats as json
	ssize_t wrote = write(out_fd, result.c_str(), result.length());
	if (wrote == -1 )
		return errno;
	if (0 != close(out_fd))
		return errno;

	if (isolate_retcode)
		return 0;
	else
		return retcode;
}

int main(int argc, char *argv[])
{
	unsigned int max_pairs = argc / 2;
	std::vector<char*> tools(max_pairs, nullptr);
	std::vector<char*> binds(max_pairs, nullptr);

	struct option options[] {
#ifdef __linux__
		{'r', "rootfs",    GOPT_ARGUMENT_REQUIRED},
		{'t', "toolchain", GOPT_ARGUMENT_REQUIRED | GOPT_REPEATABLE_VALUE, tools.data(), max_pairs},
		{'b', "bind",      GOPT_ARGUMENT_REQUIRED | GOPT_REPEATABLE_VALUE, binds.data(), max_pairs},
		{'x', "bind-cwd",  GOPT_ARGUMENT_FORBIDDEN},
#endif
		{'p', "params",          GOPT_ARGUMENT_REQUIRED},
		{'o', "output-stats",    GOPT_ARGUMENT_REQUIRED},
		{'s', "force-shell",     GOPT_ARGUMENT_FORBIDDEN},
		{'i', "interactive",     GOPT_ARGUMENT_FORBIDDEN},
		{'I', "isolate-retcode", GOPT_ARGUMENT_FORBIDDEN},

		{'h', "help", GOPT_ARGUMENT_FORBIDDEN},
		{0,   0,      GOPT_LAST }
	};

	argc = gopt(argv, options);
	gopt_errors("wakebox", options);

	bool has_help = arg(options, "help")->count > 0;
	bool has_params_file = arg(options, "params")->count > 0;
	bool has_positional_cmd = argc > 1;
	bool isolate_retcode = arg(options, "isolate-retcode")->count > 0;

	if (has_help) {
		print_help();
		return 1;
	}

	if (has_positional_cmd && has_params_file) {
		std::cerr
			<< "The batch mode --params argument can't be used with the interactive"
                           " mode command argument."
			<< std::endl;
		return 1;
	}

	if (has_positional_cmd) {
		std::string rootfs;
		if (arg(options, "rootfs")->count > 0)
			rootfs = arg(options, "rootfs")->argument;

		std::vector<std::string> toolchains;
		for (unsigned int i = 0; i < arg(options, "toolchain")->count; i++)
			toolchains.push_back(tools[i]);

		std::vector<mount_op> bind_ops;
		for (unsigned int i = 0; i < arg(options, "bind")->count; i++) {
			const std::string s = binds[i];
			std::string source = s.substr(0, s.find(":"));
			std::string destination = s.substr(s.find(":")+1);
			if (source.empty() || destination.empty() || s.find(":") == std::string::npos) {
				std::cerr << "Invalid bind: " << s << std::endl;
				return 1;
			}
			bind_ops.push_back({"bind", source, destination});
		}
		if (arg(options, "bind-cwd")->count > 0) {
			std::string cwd = get_cwd();
			bind_ops.push_back({"create-dir", "", cwd});
			bind_ops.push_back({"bind", cwd, cwd});
		}

		std::vector<std::string> positional_params;
		for (int i = 1; i < argc; i++)
			positional_params.push_back(argv[i]);
		if (positional_params.empty()) {
			std::cerr << "Must provide a command." << std::endl;
			return 1;
		}
		return run_interactive(rootfs, toolchains, bind_ops, positional_params);

	} else if (has_params_file) {
		const char* params  = arg(options, "params" )->argument;
		bool has_output = arg(options, "output-stats")->count > 0;
		bool use_stdin_file = arg(options, "interactive")->count == 0;
		bool use_shell = arg(options, "force-shell")->count > 0;
		const char* result_path = arg(options, "output-stats")->argument;
		return run_batch(params, has_output, use_stdin_file, use_shell, isolate_retcode, result_path);
	}
	print_help();
	return 1;
}
