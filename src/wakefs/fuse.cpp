/* Wake FUSE launcher to capture inputs/outputs
 *
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

// Open Group Base Specifications Issue 7
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>

#include <iostream>
#include <fstream>
#include <sstream>

#include "compat/rusage.h"
#include "util/shell.h"
#include "util/execpath.h"
#include "json/json5.h"
#include "fuse.h"
#include "namespace.h"

bool json_as_struct(const std::string &json, json_args &result) {
	JAST jast;
	if (!JAST::parse(json, std::cerr, jast))
		return false;

	for (auto &x : jast.get("command").children)
		result.command.push_back(x.second.value);

	for (auto &x : jast.get("environment").children)
		result.environment.push_back(x.second.value);

	for (auto &x : jast.get("visible").children)
		result.visible.push_back(x.second.value);

	result.directory = jast.get("directory").value;
	result.stdin_file = jast.get("stdin").value;

	result.isolate_network = jast.get("isolate-network").kind == JSON_TRUE;

	result.hostname = jast.get("hostname").value;
	result.domainname = jast.get("domainname").value;

	std::string userid = jast.get("user-id").value;
	result.userid = !userid.empty() ? std::stoi(userid) : geteuid();

	std::string groupid = jast.get("group-id").value;
	result.groupid = !groupid.empty() ? std::stoi(groupid) : getegid();

	for (auto &x : jast.get("mount-ops").children) {
		result.mount_ops.push_back({
			x.second.get("type").value,
			x.second.get("source").value,
			x.second.get("destination").value,
			x.second.get("read_only").kind == JSON_TRUE
		});
	}
	return true;
}

static int execve_wrapper(const std::vector<std::string> &command, const std::vector<std::string> &environment) {
	std::vector<const char *> cmd_args;
	for (auto &s : command)
		cmd_args.push_back(s.c_str());
	cmd_args.push_back(0);

	std::vector<const char *> env;
	for (auto &e : environment)
		env.push_back(e.c_str());
	env.push_back(0);

	execve(command[0].c_str(),
		const_cast<char * const *>(cmd_args.data()),
		const_cast<char * const *>(env.data()));
	return errno;
}


static bool collect_result_metadata(
	const std::string daemon_output,
	const struct timeval &start,
	const struct timeval &stop,
	const pid_t pid,
	const int status,
	const RUsage &rusage,
	std::string &result_json)
{
	JAST from_daemon;
	std::stringstream ss;
	if (!JAST::parse(daemon_output, ss, from_daemon)) {
		// stderr is closed, so report the error on the only output we have
		result_json = ss.str();
		return false;
	}

	JAST result_jast(JSON_OBJECT);
	auto& usage = result_jast.add("usage", JSON_OBJECT);
	usage.add("status", status);
	usage.add("membytes", static_cast<long long>(rusage.membytes));
	usage.add("inbytes", std::stoll(from_daemon.get("ibytes").value));
	usage.add("outbytes", std::stoll(from_daemon.get("obytes").value));
	usage.add("runtime", stop.tv_sec - start.tv_sec + (stop.tv_usec - start.tv_usec)/1000000.0);
	usage.add("cputime", rusage.utime + rusage.stime);

	result_jast.add("inputs", JSON_ARRAY).children = std::move(from_daemon.get("inputs").children);
	result_jast.add("outputs", JSON_ARRAY).children = std::move(from_daemon.get("outputs").children);

	std::stringstream result_ss;
	result_ss << result_jast;
	result_json = result_ss.str();

	return !result_ss.fail();
}

bool run_in_fuse(fuse_args &args, int &status, std::string &result_json) {
	if (0 != chdir(args.working_dir.c_str())) {
		std::cerr << "chdir " << args.working_dir << ": " << strerror(errno) << std::endl;
		return false;
	}

	if (!args.daemon.connect(args.visible))
		return false;

	struct timeval start;
	gettimeofday(&start, 0);

	pid_t pid = fork();
	if (pid == 0) {

		std::vector<std::string> command = args.command;
		std::vector<std::string> envs_from_mounts;
#ifdef __linux__
		if (!setup_user_namespaces(
			args.userid,
			args.groupid,
			args.isolate_network,
			args.hostname,
			args.domainname))
			exit(1);

		if (!do_mounts(args.mount_ops, args.daemon.mount_subdir, envs_from_mounts))
			exit(1);
#endif

		if (chdir(args.command_running_dir.c_str()) != 0) {
			std::cerr << "chdir " << args.command_running_dir << ": " << strerror(errno) << std::endl;
			exit(1);
		}

		if (envs_from_mounts.empty()) {
			// Search the PATH for the executable location.
			command[0] = find_in_path(command[0], find_path(args.environment));
		} else {
			// 'source' the environments provided by any mounts before running command.
			// The shell will search the PATH for the executable location.
			command = {"/bin/sh", "-c"};
			std::stringstream cmd_ss;
			for (auto& e : envs_from_mounts)
				cmd_ss << ". " << shell_escape(e) << " && ";

			cmd_ss << "exec";
			for (auto& s : args.command)
				cmd_ss << " " << shell_escape(s);
			command.push_back(cmd_ss.str());
		}

		if (args.use_stdin_file) {
			std::string stdin_file = args.stdin_file;
			if (stdin_file.empty()) stdin_file = "/dev/null";

			int fd = open(stdin_file.c_str(), O_RDONLY);
			if (fd == -1) {
				std::cerr << "open " << stdin_file << ":" << strerror(errno) << std::endl;
				exit(1);
			}
			if (fd != STDIN_FILENO) {
				dup2(fd, STDIN_FILENO);
				close(fd);
			}
		}

		int err = execve_wrapper(command, args.environment);
		std::cerr << "execve " << command[0] << ": " << strerror(err) << std::endl;
		exit(1);
	}

	// Don't hold IO open while waiting
	(void)close(STDIN_FILENO);
	(void)close(STDOUT_FILENO);
	(void)close(STDERR_FILENO);

	do waitpid(pid, &status, 0);
	while (WIFSTOPPED(status));

	if (WIFEXITED(status)) {
		status = WEXITSTATUS(status);
	} else {
		status = -WTERMSIG(status);
	}

	// We only ever wait for one child, so this is that child's usage
	RUsage usage = getRUsageChildren();

	struct timeval stop;
	gettimeofday(&stop, 0);

	std::string output;
	args.daemon.disconnect(output);

	return collect_result_metadata(output, start, stop, pid, status, usage, result_json);
}
