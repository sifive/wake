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

#include "json5.h"
#include "execpath.h"

#ifdef __linux__
// For unshare and bind mount
#include <sched.h>
#include <sys/mount.h>

static void write_file(const char *file, const char *content, int len)
{
	int fd;

	fd = open(file, O_WRONLY);
	if (fd < 0) {
		std::cerr << "open " << file << ": " << strerror(errno) << std::endl;
		exit(1);
	}

	if (write(fd, content, len) != len) {
		std::cerr << "write " << file << ": " << strerror(errno) << std::endl;
		exit(1);
	}

	close(fd);
}

static void map_id(const char *file, uint32_t from, uint32_t to)
{
	char buf[80];
	snprintf(buf, sizeof(buf), "%u %u 1", from, to);
	write_file(file, buf, strlen(buf));
}

#endif

int main(int argc, char *argv[])
{
	if (argc != 3) {
		std::cerr << "Syntax: fuse-wake <input-json> <output-json>" << std::endl;
		return 1;
	}

	std::ifstream ifs(argv[1]);
	std::string json(
		(std::istreambuf_iterator<char>(ifs)),
		(std::istreambuf_iterator<char>()));
	if (ifs.fail()) {
		std::cerr << "read " << argv[1] << ": " << strerror(errno) << std::endl;
		return 1;
	}
	ifs.close();

	std::ofstream ofs(argv[2], std::ios_base::trunc);
	if (ofs.fail()) {
		std::cerr << "write " << argv[2] << ": " << strerror(errno) << std::endl;
		return 1;
	}

	JAST jast;
	if (!JAST::parse(json, std::cerr, jast))
		return 1;

	std::string name = std::to_string(getpid());
	std::string daemon = find_execpath() + "/fuse-waked";
	std::string cwd   = get_cwd();
	std::string mpath = cwd + "/.fuse";
	std::string fpath = mpath + "/.f.fuse-waked";
	std::string rpath = mpath + "/" + name;
	std::string lpath = mpath + "/.l." + name;
	std::string ipath = mpath + "/.i." + name;
	std::string opath = mpath + "/.o." + name;

	int ffd = -1;
	useconds_t wait = 10000; /* 10ms */
	for (int retry = 0; (ffd = open(fpath.c_str(), O_RDONLY)) == -1 && retry < 12; ++retry) {
		pid_t pid = fork();
		if (pid == 0) {
			ofs.close();
			const char *env[2] = { "PATH=/usr/bin:/bin:/usr/sbin:/sbin", 0 };
			execle(daemon.c_str(), "fuse-waked", mpath.c_str(), nullptr, env);
			std::cerr << "execl " << daemon << ": " << strerror(errno) << std::endl;
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
		return 1;
	}

	// This stays open (keeping rpath live) until we terminate
	// Note: O_CLOEXEC is NOT set; thus, children keep rpath live as well
	int livefd = open(lpath.c_str(), O_CREAT|O_RDWR|O_EXCL, 0644);
	if (livefd == -1) {
		std::cerr << "open " << rpath << ": " << strerror(errno) << std::endl;
		return 1;
	}

	// We can safely release the global handle now that we hold a livefd
	(void)close(ffd);

	std::ofstream ijson(ipath);
	ijson.write(json.data(), json.size());
	if (ijson.fail()) {
		std::cerr << "write " << ipath << ": " << strerror(errno) << std::endl;
		return 1;
	}
	ijson.close();
	json.clear();

	struct timeval start;
	gettimeofday(&start, 0);

	pid_t pid = fork();
	if (pid == 0) {
		ofs.close();
		// Become a target for group death
		// setpgid(0, 0);

		// Prepare the subcommand inputs
		std::vector<char *> arg, env;
		for (auto &x : jast.get("command").children)
			arg.push_back(const_cast<char*>(x.second.value.c_str()));
		arg.push_back(0);
		for (auto &x : jast.get("environment").children)
			env.push_back(const_cast<char*>(x.second.value.c_str()));
		env.push_back(0);

		std::string subdir = jast.get("directory").value;
		std::string stdin = jast.get("stdin").value;
		if (stdin.empty()) stdin = "/dev/null";

#ifdef __linux__
		// Allow overriding this to; e.g /build
		std::string workspace = cwd;

		uid_t real_euid = geteuid();
		gid_t real_egid = getegid();

		// Enter a new mount namespace we can control
		if (0 != unshare(CLONE_NEWNS|CLONE_NEWUSER)) {
			std::cerr << "unshare: " << strerror(errno) << std::endl;
			exit(1);
		}

		// Return to our original UID (otherwise we are nobody)
		write_file("/proc/self/setgroups", "deny", 4);
		map_id("/proc/self/uid_map", real_euid, real_euid);
		map_id("/proc/self/gid_map", real_egid, real_egid);

		// Mount the fuse-visibility-protected view onto the workspace
		if (0 != mount(rpath.c_str(), workspace.c_str(), NULL, MS_BIND, NULL)) {
			std::cerr << "mount " << rpath << ": " << strerror(errno) << std::endl;
			exit(1);
		}

		std::string dir = workspace + "/" + subdir;
#else
		std::string dir = rpath + "/" + subdir;
#endif

		if (chdir(dir.c_str()) != 0) {
			std::cerr << "chdir " << dir << ": " << strerror(errno) << std::endl;
			exit(1);
		}

		int fd = open(stdin.c_str(), O_RDONLY);
		if (fd == -1) {
			std::cerr << "open " << stdin << ":" << strerror(errno) << std::endl;
			exit(1);
		}

		if (fd != STDIN_FILENO) {
			dup2(fd, STDIN_FILENO);
			close(fd);
		}

		std::string command = find_in_path(arg[0], find_path(env.data()));
		execve(command.c_str(), arg.data(), env.data());
		std::cerr << "execve " << command << ": " << strerror(errno) << std::endl;
		exit(1);
	}
	jast.children.clear();

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

	// Cause the opath to be generated (this write will fail)
	(void)!write(livefd, &stop, 1); // the ! convinces older gcc that it's ok to ignore the write
	(void)fsync(livefd);

	if (!JAST::parse(opath.c_str(), ofs, jast))
		return 1;

	bool first;
	ofs << "{\"usage\":{\"status\":" << status
	  << ",\"runtime\":" << (stop.tv_sec - start.tv_sec + (stop.tv_usec - start.tv_usec)/1000000.0)
	  << ",\"cputime\":" << (rusage.ru_utime.tv_sec + rusage.ru_stime.tv_sec + (rusage.ru_utime.tv_usec + rusage.ru_stime.tv_usec)/1000000.0)
	  << ",\"membytes\":" << rusage.ru_maxrss
	  << ",\"inbytes\":" << jast.get("ibytes").value
	  << ",\"outbytes\":" << jast.get("obytes").value
	  << "},\"inputs\":[";

	first = true;
	for (auto &x : jast.get("inputs").children) {
		ofs << (first?"":",") << "\"" << json_escape(x.second.value) << "\"";
		first = false;
	}

	ofs << "],\"outputs\":[";

	first = true;
	for (auto &x : jast.get("outputs").children) {
		ofs << (first?"":",") << "\"" << json_escape(x.second.value) << "\"";
		first = false;
	}

	ofs << "]}" << std::endl;

	return ofs.fail() ? 1 : 0;
}
