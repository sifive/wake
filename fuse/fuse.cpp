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

#include "fuse.h"
#include "json5.h"
#include "execpath.h"
#include "membytes.h"
#include "namespace.h"
#include "shell.h"

struct daemon_locations {
	// Path to the fuse-waked daemon executable.
	std::string executable;
	// Location that the fuse filesystem is mounted.
	std::string mount_path;
	// Subdir in the fuse filesystem mount that will be used by this fuse-wake's job.
	std::string mount_subdir;
	// Path that the fuse daemon will write result metadata to.
	std::string output_path;
	// File that exists when the daemon is running/active.
	std::string is_running_path;
	// File held open by each child of fuse-wake. When all children close it,
	// the daemon releases the resources for that job.
	std::string subdir_live_file;
	// JSON input file to the fuse daemon, listing which files should be visible.
	std::string visibles_path;

	daemon_locations(const std::string &exec_path, const std::string &working_dir) {
		const std::string pid = std::to_string(getpid());
		executable       = exec_path;
		mount_path       = working_dir + "/.fuse";
		mount_subdir     = mount_path + "/" + pid;
		output_path      = mount_path + "/.o." + pid ;
		is_running_path  = mount_path + "/.f.fuse-waked";
		subdir_live_file = mount_path + "/.l." + pid ;
		visibles_path    = mount_path + "/.i." + pid ;
	}
};

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

// The arg 'visible' is destroyed/moved in the interest of performance with large visible lists.
bool contact_daemon(const struct daemon_locations &daemon, int &livefd, std::vector<std::string> &visible){
	int ffd = -1;
	useconds_t wait = 10000; /* 10ms */
	for (int retry = 0; (ffd = open(daemon.is_running_path.c_str(), O_RDONLY)) == -1 && retry < 12; ++retry) {
		pid_t pid = fork();
		if (pid == 0) {
			const char *env[2] = { "PATH=/usr/bin:/bin:/usr/sbin:/sbin", 0 };
			execle(daemon.executable.c_str(), "fuse-waked", daemon.mount_path.c_str(), nullptr, env);
			std::cerr << "execl " << daemon.executable << ": " << strerror(errno) << std::endl;
			exit(1);
		}
		usleep(wait);
		wait <<= 1;

		int status;
		do waitpid(pid, &status, 0);
		while (WIFSTOPPED(status));
	}

	if (ffd == -1) {
		std::cerr << "Could not contact FUSE daemon" << std::endl;
		return false;
	}

	// This stays open (keeping subdir_live_file live) until we terminate
	// Note: O_CLOEXEC is NOT set; thus, children keep subdir_live_file live as well
	livefd = open(daemon.subdir_live_file.c_str(), O_CREAT|O_RDWR|O_EXCL, 0644);
	if (livefd == -1) {
		std::cerr << "open " << daemon.subdir_live_file << ": " << strerror(errno) << std::endl;
		return false;
	}

	// We can safely release the global handle now that we hold a livefd
	(void)close(ffd);

	// The fuse-waked process takes an input file containing visible files, json formatted.
	JAST for_daemon(JSON_OBJECT);
	auto &vis = for_daemon.add("visible", JSON_ARRAY);
	for (auto &s : visible)
		vis.add(std::move(s));

	std::ofstream ijson(daemon.visibles_path);
	ijson << for_daemon;
	if (ijson.fail()) {
		std::cerr << "write " << daemon.visibles_path << ": " << strerror(errno) << std::endl;
		return false;
	}
	ijson.close();
	return true;
}

bool collect_result_metadata(
	const struct timeval &start,
	const struct timeval &stop,
	const std::string &daemon_output_path,
	const pid_t pid,
	const int livefd,
	const int status,
	const struct rusage &rusage,
	std::string &result_json
){
	// Cause the daemon_output_path to be generated (this write will fail)
	(void)!write(livefd, "x", 1); // the ! convinces older gcc that it's ok to ignore the write
	(void)fsync(livefd);

	JAST from_daemon;
	std::stringstream ss;
	if (!JAST::parse(daemon_output_path.c_str(), ss, from_daemon)) {
		// stderr is closed, so report the error on the only output we have
		result_json = ss.str();
		return false;
	}

	JAST result_jast(JSON_OBJECT);
	auto& usage = result_jast.add("usage", JSON_OBJECT);
	usage.add("status", status);
	usage.add("membytes", MEMBYTES(rusage));
	usage.add("inbytes", std::stoll(from_daemon.get("ibytes").value));
	usage.add("outbytes", std::stoll(from_daemon.get("obytes").value));
	usage.add("runtime", stop.tv_sec - start.tv_sec + (stop.tv_usec - start.tv_usec)/1000000.0);
	usage.add("cputime",
		rusage.ru_utime.tv_sec + rusage.ru_stime.tv_sec +
		(rusage.ru_utime.tv_usec + rusage.ru_stime.tv_usec)/1000000.0);

	result_jast.add("inputs", JSON_ARRAY).children = std::move(from_daemon.get("inputs").children);
	result_jast.add("outputs", JSON_ARRAY).children = std::move(from_daemon.get("outputs").children);

	std::stringstream result_ss;
	result_ss << result_jast;
	result_json = result_ss.str();

	return result_ss.fail();
}

int run_in_fuse(fuse_args &args, std::string &result_json) {
	if (0 != chdir(args.working_dir.c_str())) {
		std::cerr << "chdir " << args.working_dir << ": " << strerror(errno) << std::endl;
		return 1;
	}

	const daemon_locations daemon(args.daemon_path, args.working_dir);

	int livefd;
	if (!contact_daemon(daemon, livefd, args.visible))
		return 1;

	struct timeval start;
	gettimeofday(&start, 0);

	pid_t pid = fork();
	if (pid == 0) {

		std::vector<std::string> command = args.command;
#ifdef __linux__
		if (!setup_user_namespaces(
			args.userid,
			args.groupid,
			args.isolate_network,
			args.hostname,
			args.domainname))
			exit(1);

		std::vector<std::string> envs_from_mounts;
		if (!do_mounts(args.mount_ops, daemon.mount_subdir, envs_from_mounts))
			exit(1);

		std::string dir;
		if (!get_workspace_dir(args.mount_ops, args.working_dir, dir)) {
			std::cerr << "'workspace' mount entry is missing from input" << std::endl;
			exit(1);
		}
		dir = dir + "/" + args.directory;

		if (chdir(dir.c_str()) != 0) {
			std::cerr << "chdir " << dir << ": " << strerror(errno) << std::endl;
			exit(1);
		}

		if (envs_from_mounts.size() > 0) {
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

		} else {
			// Search the PATH for the executable location.
			command[0] = find_in_path(command[0], find_path(args.environment));
		}
#else
		std::string dir = daemon.mount_subdir + "/" + args.directory;
		if (chdir(dir.c_str()) != 0) {
			std::cerr << "chdir " << dir << ": " << strerror(errno) << std::endl;
			exit(1);
		}
		// Search the PATH for the executable location.
		command[0] = find_in_path(command[0], find_path(args.environment));
#endif
		if (args.use_stdin_file) {
			std::string stdin = args.stdin_file;
			if (stdin.empty()) stdin = "/dev/null";

			int fd = open(stdin.c_str(), O_RDONLY);
			if (fd == -1) {
				std::cerr << "open " << stdin << ":" << strerror(errno) << std::endl;
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

	int status;
	struct rusage rusage;
	do wait4(pid, &status, 0, &rusage);
	while (WIFSTOPPED(status));

	if (WIFEXITED(status)) {
		status = WEXITSTATUS(status);
	} else {
		status = -WTERMSIG(status);
	}

	struct timeval stop;
	gettimeofday(&stop, 0);

	return collect_result_metadata(start, stop, daemon.output_path, pid, livefd, status, rusage, result_json);
}
