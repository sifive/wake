/* Linux namespace and mount ops for fuse-wake
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
#ifdef __linux__

#include <algorithm>
#include <fstream>
#include <iostream>
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <string.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <sys/wait.h>

#include "json5.h"
#include "namespace.h"

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

// Inside a user namespace, you are not allowed to separate mounts that you inherit from another
// mount namespace from their child mounts.
// Therefore, when a mount has subdirectories containing other mounts we must be recursive when we bind mount.
static bool bind_mount(const std::string& source, const std::string& destination, bool readonly)
{
	const char* src = source.c_str();
	const char* dest = destination.c_str();

	if (0 != mount(src, dest, NULL, MS_BIND | MS_REC, NULL)) {
		std::cerr << "bind mount (" << source << " -> " << destination << "): "
			<< strerror(errno) << std::endl;
		return false;
	}
	// Re-mount to set destination as read-only.
	// Source filesystem must not have 'MS_NODEV' (a.k.a. 'nodev') set.
	if (readonly) {
		if (0 != mount(src, dest, NULL, MS_BIND | MS_REC | MS_RDONLY | MS_REMOUNT, NULL)) {
			std::cerr << "bind mount (" << source << " -> " << destination << "): "
				<< strerror(errno) << std::endl;
			return false;
		}
	}
	return true;
}

static bool validate_mount(
	const std::string& op,
	const std::string& source)
{
	static const std::vector<std::string> mount_ops {
		// must be sorted
		"bind",
		"create-dir",
		"create-file",
		"squashfs",
		"tmpfs",
		"workspace"
	};

	if (!std::binary_search(mount_ops.begin(), mount_ops.end(), op)) {
		std::cerr << "unknown mount type: '" << op << "'" << std::endl;
		return false;
	}

	if ((op != "bind" && op != "squashfs") && source.length() != 0) {
		std::cerr << "mount: " << op << " can not have 'source' option" << std::endl;
		return false;
	}

	return true;
}

// The pivot_root syscall has no glibc wrapper.
static int pivot_root(const char* new_root, const char* put_old) {
#ifndef __NR_pivot_root
#error 'pivot_root' syscall number missing from <sys/syscall.h>
#endif
	return syscall(__NR_pivot_root, new_root, put_old);
}

/* Many systems have an ancient manpage entry for pivot_root,
 * See 2019-era docs at: https://lwn.net/Articles/800381/
 *
 * new_root and put_old may be the same directory.
 * In particular, the following sequence allows a pivot-root operation without needing
 * to create and remove a temporary directory:
 *
 *   chdir(new_root);
 *   pivot_root(".", ".");
 *   umount2(".", MNT_DETACH);
 *
 * This sequence succeeds because the pivot_root() call stacks the old root mount point
 * on top of the new root mount point at /. At that point, the calling process's root
 * directory and current working directory refer to the new root mount point (new_root).
 * During the subsequent umount() call, resolution of "." starts with new_root and then
 * moves up the list of mounts stacked at /, with the result that old root mount point
 * is unmounted.
 */
static bool do_pivot(const std::string& newroot) {
	// The pivot_root syscall requires that the new root location is a mountpoint.
	// Bind mount the new root onto itself to ensure this.
	if (!bind_mount(newroot, newroot, false)) {
		return false;
	}
	if (0 != chdir(newroot.c_str())) {
		std::cerr << "chdir: " << strerror(errno) << std::endl;
		return false;
	}
	if (0 != pivot_root(".", ".")) {
		std::cerr << "pivot_root(\".\", \".\"): " << strerror(errno) << std::endl;
		return false;
	}
	if (0 != umount2(".", MNT_DETACH)) {
		std::cerr << "umount2: " << strerror(errno) << std::endl;
		return false;
	}
	return true;
}

static bool mount_tmpfs(const std::string& destination) {
	if (0 != mount("tmpfs", destination.c_str(), "tmpfs", 0UL, NULL)) {
		std::cerr << "tmpfs mount (" << destination << "): "
			<< strerror(errno) << std::endl;
		return false;
	}
	return true;
}

static bool equal_dev_ids(dev_t a, dev_t b) {
	return major(a) == major(b) && minor(a) == minor(b);
}

static bool mount_squashfs(const std::string& source, const std::string& mountpoint) {
	pid_t pid = fork();
	if (pid == 0) {
		// kernel to send SIGKILL to squashfuse when fuse-wake terminates
		if (prctl(PR_SET_PDEATHSIG, SIGKILL) == -1) {
			std::cerr << "squashfuse prctl: " << strerror(errno) << std::endl;
			exit(1);
		}
		execlp("squashfuse", "squashfuse", "-f", source.c_str(), mountpoint.c_str(), NULL);
		std::cerr << "execlp squashfuse: " << strerror(errno) << std::endl;
		exit(1);
	}

	// Wait for the mount to exist before we continue by checking if the
	// stat() device id or the inode changes.
	struct stat before;
	if (0 != stat(mountpoint.c_str(), &before)) {
		std::cerr << "stat (" << mountpoint << "): " << strerror(errno) << std::endl;
		return false;
	}

	for (int i = 0; i < 10; i++) {
		struct stat after;
		if (0 != stat(mountpoint.c_str(), &after)) {
			std::cerr << "stat (" << mountpoint << "): " << strerror(errno) << std::endl;
			return false;
		}

		if (!equal_dev_ids(before.st_dev, after.st_dev) || (before.st_ino != after.st_ino))
			return true;
		else
			usleep(10000 << i); // 10ms * 2^i
	}

	std::cerr << "squashfs mount missing: " << mountpoint << std::endl;
	return false;
}

bool create_dir(const std::string& dest) {
	if (0 == mkdir(dest.c_str(), 0777))
		return true;

	std::cerr << "mkdir (" << dest << "): " << strerror(errno) << std::endl;
	return false;
}

bool create_file(const std::string& dest) {
	int fd = creat(dest.c_str(), 0777);
	if (fd < 0) {
		std::cerr << "creat (" << dest << "): " << strerror(errno) << std::endl;
		return false;
	}
	if (0 != close(fd)) {
		std::cerr << "close (" << dest << "): " << strerror(errno) << std::endl;
		return false;
	}
	return true;
}

// Do the mounts specified in the parsed input json.
// The input/caller responsibility to ensure that the mountpoint exists,
// that the platform supports the mount type/options, and to correctly order
// the layered mounts.
bool do_mounts(const std::vector<mount_op> mount_ops, const std::string& fuse_mount_path)
{
	std::string mount_prefix;
	for (auto &x : mount_ops) {
		if (x.destination == "/") {
			// All mount ops from here onward will have a prefixed destination.
			// The prefix will be pivoted to after the final mount op.
			mount_prefix = "/tmp/.wakebox-mount";
			// Re-use if it already exists.
			if (0 != mkdir(mount_prefix.c_str(), 0777) && errno != EEXIST) {
				std::cerr << "mkdir (" << mount_prefix << "): " << strerror(errno) << std::endl;
				return false;
			}
		}
		const std::string& target = mount_prefix + x.destination;

		if (!validate_mount(x.type, x.source))
			return false;

		if (x.type == "bind" && !bind_mount(x.source, target, x.read_only))
			return false;

		if (x.type == "workspace" && !bind_mount(fuse_mount_path, target, false))
			return false;

		if (x.type == "tmpfs" && !mount_tmpfs(target))
			return false;

		if (x.type == "squashfs" && !mount_squashfs(x.source, target))
			return false;

		if (x.type == "create-dir" && !create_dir(target))
			return false;

		if (x.type == "create-file" && !create_file(target))
			return false;
	}

	if (!mount_prefix.empty() && !do_pivot(mount_prefix))
		return false;

	return true;
}

bool get_workspace_dir(
	const std::vector<mount_op> mount_ops,
	const std::string& host_workspace_dir,
	std::string& out)
{
	for (auto &x : mount_ops) {
		if (x.type == "workspace") {
			out = x.destination;
			// convert a workspace relative path into absolute path
			if (out.at(0) != '/')
				out = host_workspace_dir + "/" + out;
			return true;
		}
	}
	return false;
}

bool setup_user_namespaces(
	int id_user,
	int id_group,
	bool isolate_network,
	std::string hostname,
	std::string domainname)
{
	uid_t real_euid = geteuid();
	gid_t real_egid = getegid();

	uid_t euid = id_user;
	gid_t egid = id_group;

	int flags = CLONE_NEWNS|CLONE_NEWUSER|CLONE_NEWUTS;

	if (isolate_network)
		flags |= CLONE_NEWNET;

	// Enter a new mount namespace we can control
	if (0 != unshare(flags)) {
		std::cerr << "unshare: " << strerror(errno) << std::endl;
		return false;
	}

	if (sethostname(hostname.c_str(), hostname.length()) != 0)
		std::cerr << "sethostname(" << hostname << "): " << strerror(errno) << std::endl;

	if (setdomainname(domainname.c_str(), domainname.length()) != 0)
		std::cerr << "setdomainname(" << domainname << "): " << strerror(errno) << std::endl;

	// Map our UID to either our original UID or root
	write_file("/proc/self/setgroups", "deny", 4);
	map_id("/proc/self/uid_map", euid, real_euid);
	map_id("/proc/self/gid_map", egid, real_egid);

	return true;
}
#endif
