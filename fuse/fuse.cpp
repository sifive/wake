/* Wake FUSE driver to capture inputs/outputs
 *
 * Copyright 2019 SiFive, Inc.
 * Copyright 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
 * Copyright 2011       Sebastian Pipping <sebastian@pipping.org>
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

#define FUSE_USE_VERSION 26

#ifdef linux
#define _XOPEN_SOURCE 700
#endif

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <sys/time.h>
#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif

#include <set>
#include <string>
#include <iostream>
#include <fstream>
#include "json5.h"

//#define TRACE(x) do { fprintf(stderr, "%s: %s\n", __FUNCTION__, x); fflush(stderr); } while (0)
#define TRACE(x) (void)x

extern char **environ;

static int rootfd;
std::set<std::string> files_visible;
std::set<std::string> files_read;
std::set<std::string> files_wrote;

static const char *map(const char *path) {
	return path+1;
}

static inline int is_root(const char *path) {
	return path[0] == '/' && !path[1];
}

static bool is_visible(const std::string &path) {
	if (files_visible.find(path) != files_visible.end()) return true;
	auto i = files_visible.lower_bound(path + "/");
	return i != files_visible.end()
		&& i->size() > path.size()
		&& (*i)[path.size()] == '/'
		&& 0 == i->compare(0, path.size(), path);
}

static bool is_writeable(const std::string &path) {
	return files_wrote.find(path) != files_wrote.end();
}

static bool is_readable(const std::string &path) {
	return is_visible(path) || is_writeable(path);
}

static bool is_creatable(const std::string &path) {
	return true; // is_writeable(path) || faccessat(rootfd, path.c_str(), R_OK, 0) != 0;
}

static int map_open(const char *path, int oflag, int mode)
{
	if (is_root(path)) {
		return dup(rootfd);
	} else {
		if ((oflag & O_CREAT)) {
			if (!is_creatable(map(path)))
				return -EACCES;
			if ((oflag & O_EXCL))
				oflag = O_TRUNC | (oflag & ~O_EXCL);
		}

		if (!(oflag & O_CREAT) && !is_readable(map(path)))
			return -ENOENT;

		int ret = openat(rootfd, map(path), oflag, mode);

		if (ret >= 0 && (oflag & O_CREAT))
			files_wrote.insert(map(path));

		return ret;
	}
}

static int wakefuse_getattr(const char *path, struct stat *stbuf)
{
	int res;

	TRACE(path);

	if (is_root(path)) {
		res = fstat(rootfd, stbuf);
	} else {
		if (!is_readable(map(path)))
			return -ENOENT;
		res = fstatat(rootfd, map(path), stbuf, AT_SYMLINK_NOFOLLOW);
	}
	if (res == -1)
		return -errno;

	// files_read.insert(map(path));
	return 0;
}

static int wakefuse_access(const char *path, int mask)
{
	int res;

	TRACE(path);

	if (is_root(path))
		return 0;

	if (!is_readable(map(path)))
		return -ENOENT;

	res = faccessat(rootfd, map(path), mask, 0);
	if (res == -1)
		return -errno;

	// files_read.insert(map(path));
	return 0;
}

static int wakefuse_readlink(const char *path, char *buf, size_t size)
{
	int res;

	TRACE(path);

	if (is_root(path))
		return -EINVAL;

	if (!is_readable(map(path)))
		return -ENOENT;

	res = readlinkat(rootfd, map(path), buf, size - 1);
	if (res == -1)
		return -errno;

	buf[res] = '\0';
	files_read.insert(map(path));
	return 0;
}


static int wakefuse_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
		       off_t offset, struct fuse_file_info *fi)
{
	int dfd;
	DIR *dp;
	struct dirent *de;

	(void) offset;
	(void) fi;

	TRACE(path);

	dfd = map_open(path, O_RDONLY, 0);
	if (dfd == -1)
		return -errno;

	dp = fdopendir(dfd);
	if (dp == NULL) {
		close(dfd);
		return -errno;
	}

	if (is_root(path))
		rewinddir(dp);

	while ((de = readdir(dp)) != NULL) {
		struct stat st;
		memset(&st, 0, sizeof(st));
		st.st_ino = de->d_ino;
		st.st_mode = de->d_type << 12;

		std::string file = std::string(map(path));
		if (!file.empty()) file += "/";
		file += de->d_name;

		if (!is_readable(file))
			continue;

		if (filler(buf, de->d_name, &st, 0))
			break;
	}

	closedir(dp);
	return 0;
}

static int wakefuse_mknod(const char *path, mode_t mode, dev_t rdev)
{
	int res;
	(void)rdev;

	TRACE(path);

	if (S_ISREG(mode)) {
		res = map_open(path, O_CREAT | O_EXCL | O_WRONLY, mode);
		if (res >= 0)
			res = close(res);
	} else {
		res = -1;
		errno = EPERM;
	}

	if (res == -1)
		return -errno;

	return 0;
}

static int wakefuse_mkdir(const char *path, mode_t mode)
{
	int res;

	TRACE(path);

	if (is_root(path))
		return -EEXIST;

	if (!is_creatable(map(path)))
		return -EACCES;

	res = mkdirat(rootfd, map(path), mode);
	if (res == -1 && (errno != EEXIST || is_readable(map(path))))
		return -errno;

	files_wrote.insert(map(path));
	return 0;
}

static int wakefuse_unlink(const char *path)
{
	int res;

	TRACE(path);

	if (is_root(path))
		return -EACCES;

	if (!is_writeable(map(path))) {
		if (!is_readable(map(path)))
			return -ENOENT;
		else
			return -EACCES;
	}

	res = unlinkat(rootfd, map(path), 0);
	if (res == -1)
		return -errno;

	files_wrote.erase(map(path));
	files_read.erase(map(path));
	return 0;
}

static int wakefuse_rmdir(const char *path)
{
	int res;

	TRACE(path);

	if (is_root(path))
		return -EBUSY;

	if (!is_writeable(map(path))) {
		if (!is_readable(map(path)))
			return -ENOENT;
		else
			return -EACCES;
	}


	res = unlinkat(rootfd, map(path), AT_REMOVEDIR);
	if (res == -1)
		return -errno;

	files_wrote.erase(map(path));
	files_read.erase(map(path));
	return 0;
}

static int wakefuse_symlink(const char *from, const char *to)
{
	int res;

	TRACE(to);

	if (is_root(to))
		return -EEXIST;

	if (!is_creatable(map(to)))
		return -EACCES;

	res = symlinkat(from, rootfd, map(to));
	if (res == -1)
		return -errno;

	files_wrote.insert(map(to));
	return 0;
}

static int wakefuse_rename(const char *from, const char *to)
{
	int res;

	TRACE(from);

	if (is_root(from))
		return -EINVAL;

	if (is_root(to))
		return -EEXIST;

	if (!is_creatable(map(to)))
		return -EACCES;

	if (!is_writeable(map(from)))
		return -EACCES;

	res = renameat(rootfd, map(from), rootfd, map(to));
	if (res == -1)
		return -errno;

	files_wrote.erase(map(from));
	files_read.erase(map(from));
	files_wrote.insert(map(to));

	return 0;
}

static int wakefuse_link(const char *from, const char *to)
{
	int res;

	TRACE(from);

	if (is_root(from))
		return -EINVAL;

	if (is_root(to))
		return -EEXIST;

	if (!is_creatable(map(to)))
		return -EACCES;

	if (!is_readable(map(from)))
		return -ENOENT;

	res = linkat(rootfd, map(from), rootfd, map(to), 0);
	if (res == -1)
		return -errno;

	files_wrote.insert(map(to));
	return 0;
}

static int wakefuse_chmod(const char *path, mode_t mode)
{
	int res;

	TRACE(path);

	if (is_root(path)) {
		return -EACCES;
	} else {
		if (!is_writeable(map(path)))
			return -EACCES;
		res = fchmodat(rootfd, map(path), mode, 0);
	}
	if (res == -1)
		return -errno;

	return 0;
}

static int wakefuse_chown(const char *path, uid_t uid, gid_t gid)
{
	int res;

	TRACE(path);

	if (is_root(path)) {
		return -EACCES;
	} else {
		if (!is_writeable(map(path)))
			return -EACCES;
		res = fchownat(rootfd, map(path), uid, gid, 0);
	}
	if (res == -1)
		return -errno;

	return 0;
}

static int wakefuse_truncate(const char *path, off_t size)
{
	int fd;
	int res;

	TRACE(path);

	if (!is_writeable(map(path)))
		return -EACCES;

	fd = map_open(path, O_RDWR, 0);
	if (fd == -1)
		return -errno;

	res = ftruncate(fd, size);
	if (res == -1)
		return -errno;

	res = close(fd);
	if (res == -1)
		return -errno;

	return 0;
}

static int wakefuse_utimens(const char *path, const struct timespec ts[2])
{
	int res;

	TRACE(path);

	if (is_root(path)) {
		return -EACCES;
		// res = futimens(rootfd, ts);
	} else {
		if (!is_writeable(map(path)))
			return -EACCES;
		res = utimensat(rootfd, map(path), ts, AT_SYMLINK_NOFOLLOW);
	}
	if (res == -1)
		return -errno;

	return 0;
}

static int wakefuse_open(const char *path, struct fuse_file_info *fi)
{
	int res;

	TRACE(path);

	res = map_open(path, fi->flags, 0);
	if (res == -1)
		return -errno;

	fi->fh = res;
	return 0;
}

static int wakefuse_read(const char *path, char *buf, size_t size, off_t offset,
		    struct fuse_file_info *fi)
{
	int res;

	TRACE(path);

	files_read.insert(map(path));

	res = pread(fi->fh, buf, size, offset);
	if (res == -1)
		res = -errno;

	return res;
}

static int wakefuse_write(const char *path, const char *buf, size_t size,
		     off_t offset, struct fuse_file_info *fi)
{
	int res;

	TRACE(path);

	files_wrote.insert(map(path));

	res = pwrite(fi->fh, buf, size, offset);
	if (res == -1)
		res = -errno;

	return res;
}

static int wakefuse_statfs(const char *path, struct statvfs *stbuf)
{
	int fd;
	int res;

	TRACE(path);

	fd = map_open(path, O_RDONLY, 0);
	if (fd == -1)
		return -errno;

	res = fstatvfs(fd, stbuf);
	if (res == -1)
		return -errno;

	res = close(fd);
	if (res == -1)
		return -errno;

	return 0;
}

static int wakefuse_release(const char *path, struct fuse_file_info *fi)
{
	int res;

	TRACE(path);

	res = close(fi->fh);
	if (res == -1)
		return -errno;

	return 0;
}

static int wakefuse_fsync(const char *path, int isdatasync,
		     struct fuse_file_info *fi)
{
	int res;

	TRACE(path);

#ifdef HAVE_FDATASYNC
	if (isdatasync)
		res = fdatasync(fi->fh);
	else
#else
	(void) isdatasync;
#endif
		res = fsync(fi->fh);

	if (res == -1)
		return -errno;

	return 0;
}

#ifdef HAVE_POSIX_FALLOCATE
static int wakefuse_fallocate(const char *path, int mode,
			off_t offset, off_t length, struct fuse_file_info *fi)
{
	int fd;
	int res;

	(void) fi;

	TRACE(path);

	if (!is_writeable(map(path)))
		return -EACCES;

	if (mode)
		return -EOPNOTSUPP;

	fd = map_open(path, O_WRONLY, 0);
	if (fd == -1)
		return -errno;

	res = -posix_fallocate(fd, offset, length);

	close(fd);
	return res;
}
#endif

#ifdef HAVE_SETXATTR
/* xattr operations are optional and can safely be left unimplemented */
static int wakefuse_setxattr(const char *path, const char *name, const char *value,
			size_t size, int flags)
{
	int fd;
	int res;

	TRACE(path);

	if (!is_writeable(map(path)))
		return -EACCES;

	fd = map_open(path, O_RDWR | O_NOFOLLOW, 0);
	if (fd == -1)
		return -errno;

	res = fsetxattr(fd, name, value, size, flags);
	if (res == -1)
		return -errno;

	res = close(fd);
	if (res == -1)
		return -errno;

	files_wrote.insert(map(path)));
	return 0;
}

static int wakefuse_getxattr(const char *path, const char *name, char *value,
			size_t size)
{
	int fd;
	int res;

	TRACE(path);

	if (!is_readable(map(path)))
		return -EACCES;

	fd = map_open(path, O_RDONLY | O_NOFOLLOW, 0);
	if (fd == -1)
		return -errno;

	res = fgetxattr(fd, name, value, size);
	if (res == -1) {
		close(fd);
		return -errno;
	}

	if (close(fd) == -1)
		return -errno;

	file_read.insert(map(path));
	return res;
}

static int wakefuse_listxattr(const char *path, char *list, size_t size)
{
	int fd;
	int res;

	TRACE(path);

	if (!is_readable(map(path)))
		return -EACCES;

	fd = map_open(path, O_RDONLY | O_NOFOLLOW, 0);
	if (fd == -1)
		return -errno;

	res = flistxattr(fd, list, size);
	if (res == -1) {
		close(fd);
		return -errno;
	}

	if (close(fd) == -1)
		return -errno;

	file_read.insert(map(path));
	return res;
}

static int wakefuse_removexattr(const char *path, const char *name)
{
	int fd;
	int res;

	TRACE(path);

	if (!is_writeable(map(path)))
		return -EACCES;

	fd = map_open(path, O_RDWR, 0);
	if (fd == -1)
		return -errno;

	res = fremovexattr(fd, name);
	if (res == -1) {
		close(fd);
		return -errno;
	}

	res = close(fd);
	if (res == -1)
		return -errno;

	files_wrote.insert(map(path));
	return 0;
}
#endif /* HAVE_SETXATTR */

static std::string path;
static struct fuse* fh;
static struct fuse_chan* fc;
static sigset_t saved;
static bool pass;
static int pipefds[2];

static struct fuse_operations wakefuse_ops;

static void handle_exit(int sig)
{
	if (sig != SIGCHLD) pass = false;

	// Unfortunately, fuse_unmount can fail if the filesystem is still in use.
	// Yes, this can even happen on linux with MNT_DETACH / lazy umount.
	// Worse, fuse_unmount closes descriptors and frees memory, so can only be called once.
	// Thus, calling fuse_exit here would terminate fuse_loop and then maybe fail to unmount.

	// Instead of terminating the loop directly via fuse_exit, try to unmount.
	// If this succeeds, fuse_loop will terminate anyway.
	// In case it fails, we setup an itimer to keep trying to unmount.

	// We need to fork before fuse_unmount, in order to be able to try more than once.
	if (fork() == 0) {
		fuse_unmount(path.c_str(), fc);
		exit(0);
	} else {
		struct itimerval retry;
		memset(&retry, 0, sizeof(retry));
		retry.it_value.tv_usec = 200000; // 200ms
		setitimer(ITIMER_REAL, &retry, 0);
	}
}

static void *wakefuse_init(struct fuse_conn_info *conn)
{
	if (pipefds[1] != -1) close(pipefds[1]);
	pipefds[1] = -1;

	// unblock signals
	struct sigaction sa;
	sa.sa_handler = handle_exit;
	sa.sa_flags = SA_RESTART|SA_NOCLDSTOP;
	sigaction(SIGCHLD, &sa, 0);
	sigprocmask(SIG_SETMASK, &saved, 0);
	return 0;
}

static char hex(unsigned char x) {
  if (x < 10) return '0' + x;
  return 'a' + x - 10;
}

static std::string escape(const std::string &x) {
  std::string out;
  char escape[] = "\\u0000";
  for (char z : x) {
    unsigned char c = z;
    if (c == '"') out.append("\\\"");
    else if (c == '\\') out.append("\\\\");
    else if (z >= 0x20) {
      out.push_back(c);
    } else {
      escape[4] = hex(z >> 4);
      escape[5] = hex(z & 0xf);
      out.append(escape);
    }
  }
  return out;
}

int main(int argc, char *argv[])
{
	wakefuse_ops.init		= wakefuse_init;
	wakefuse_ops.getattr		= wakefuse_getattr;
	wakefuse_ops.access		= wakefuse_access;
	wakefuse_ops.readlink		= wakefuse_readlink;
	wakefuse_ops.readdir		= wakefuse_readdir;
	wakefuse_ops.mknod		= wakefuse_mknod;
	wakefuse_ops.mkdir		= wakefuse_mkdir;
	wakefuse_ops.symlink		= wakefuse_symlink;
	wakefuse_ops.unlink		= wakefuse_unlink;
	wakefuse_ops.rmdir		= wakefuse_rmdir;
	wakefuse_ops.rename		= wakefuse_rename;
	wakefuse_ops.link		= wakefuse_link;
	wakefuse_ops.chmod		= wakefuse_chmod;
	wakefuse_ops.chown		= wakefuse_chown;
	wakefuse_ops.truncate		= wakefuse_truncate;
	wakefuse_ops.utimens		= wakefuse_utimens;
	wakefuse_ops.open		= wakefuse_open;
	wakefuse_ops.read		= wakefuse_read;
	wakefuse_ops.write		= wakefuse_write;
	wakefuse_ops.statfs		= wakefuse_statfs;
	wakefuse_ops.release		= wakefuse_release;
	wakefuse_ops.fsync		= wakefuse_fsync;
#ifdef HAVE_POSIX_FALLOCATE
	wakefuse_ops.fallocate		= wakefuse_fallocate;
#endif
#ifdef HAVE_SETXATTR
	wakefuse_ops.setxattr		= wakefuse_setxattr;
	wakefuse_ops.getxattr		= wakefuse_getxattr;
	wakefuse_ops.listxattr		= wakefuse_listxattr;
	wakefuse_ops.removexattr	= wakefuse_removexattr;
#endif

	int status = 1;
	JAST jast;
	pid_t pid;
	sigset_t block;
	struct sigaction sa;
	struct fuse_args args;
	struct timeval start, stop;
	std::vector<char *> arg, env;

	// no fail operations
	memset(&sa, 0, sizeof(sa));
	umask(0);

	if (argc != 3) {
		std::cerr << "Syntax: fuse-wake <input-json> <output-json>" << std::endl;
		goto term;
	}

	if (!JAST::parse(argv[1], std::cerr, jast))
		goto term;

	// block those signals where we wish to terminate cleanly
	sigemptyset(&block);
	sigaddset(&block, SIGHUP);
	sigaddset(&block, SIGINT);
	sigaddset(&block, SIGQUIT);
	sigaddset(&block, SIGTERM);
	sigaddset(&block, SIGCHLD);
	sigprocmask(SIG_BLOCK, &block, &saved);

	// ignore these signals
	sa.sa_handler = SIG_IGN;
	sa.sa_flags = SA_RESTART;
	sigaction(SIGPIPE, &sa, 0);
	sigaction(SIGUSR1, &sa, 0);
	sigaction(SIGUSR2, &sa, 0);

	// hook these signals
	sa.sa_handler = handle_exit;
	sa.sa_flags = SA_RESTART|SA_NOCLDSTOP;
	sigaction(SIGHUP,  &sa, 0);
	sigaction(SIGINT,  &sa, 0);
	sigaction(SIGQUIT, &sa, 0);
	sigaction(SIGTERM, &sa, 0);
	sigaction(SIGCHLD, &sa, 0);

	// Prepare the subcommand inputs
	for (auto &x : jast.get("command").children)
		arg.push_back(const_cast<char*>(x.second.value.c_str()));
	arg.push_back(0);
	for (auto &x : jast.get("environment").children)
		env.push_back(const_cast<char*>(x.second.value.c_str()));
	env.push_back(0);

	path = ".build/" + std::to_string(getpid());

	if (pipe(pipefds) != 0) {
		std::cerr << "pipe: " << strerror(errno) << std::endl;
		goto term;
	}

	gettimeofday(&start, 0);

	pid = fork();
	if (pid == 0) {
		std::string dir = path + "/" + jast.get("directory").value;
		std::string stdin = jast.get("stdin").value;
		if (stdin.empty()) stdin = "/dev/null";

		// Wait for fuse to be mounted
		char c;
		close(pipefds[1]);
		read(pipefds[0], &c, 1);

		if (chdir(dir.c_str()) != 0) {
			std::cerr << "chdir " << dir << ": " << strerror(errno) << std::endl;
			exit(1);
		}

		int fd = open(stdin.c_str(), O_RDONLY);
		if (fd == -1) {
			std::cerr << "open " << stdin << ":" << strerror(errno) << std::endl;
			exit(1);
		}

		if (fd != 0) {
			dup2(fd, 0);
			close(fd);
		}

		environ = env.data();
		execvp(arg[0], arg.data());
		std::cerr << "execvp " << arg[0] << ": " << strerror(errno) << std::endl;
		exit(1);
	}
	close(pipefds[0]);

	for (auto &x : jast.get("visible").children)
		files_visible.insert(x.second.value);

	rootfd = open(".", O_RDONLY);
	if (rootfd == -1) {
		perror("failed to open .");
		goto term;
	}

	mkdir(".build", 0775);
	if (mkdir(path.c_str(), 0775) != 0 && errno != EEXIST) {
		perror("mkdir");
		goto term;
	}

	args = FUSE_ARGS_INIT(0, NULL);
	if (fuse_opt_add_arg(&args, "wake") != 0) {
		fprintf(stderr, "fuse_opt_add_arg failed");
		goto rmroot;
	}

	fc = fuse_mount(path.c_str(), &args);
	if (!fc) {
		fprintf(stderr, "fuse_mount failed");
		goto freeargs;
	}

	fh = fuse_new(fc, &args, &wakefuse_ops, sizeof(wakefuse_ops), 0);
	if (!fh) {
		fprintf(stderr, "fuse_new failed");
		goto unmount;
	}

	if (fuse_loop(fh) != 0) {
		fprintf(stderr, "fuse_loop failed");
		goto unmount;
	}

	// Block signals again
	sigprocmask(SIG_BLOCK, &block, 0);
	pass = true;

unmount:
	// out-of-order completion: unmount THEN destroy
	fuse_unmount(path.c_str(), fc);
	if (fh) fuse_destroy(fh);
freeargs:
	fuse_opt_free_args(&args);
rmroot:
	if (rmdir(path.c_str()) != 0) {
		fprintf(stderr, "rmdir %s: %s\n", path.c_str(), strerror(errno));
	}
term:
	if (pass) {
		// re-enable signals to make it possible to interrupt output
		sa.sa_handler = SIG_DFL;
		sa.sa_flags = 0;
		sigaction(SIGPIPE, &sa, 0);
		sigaction(SIGUSR1, &sa, 0);
		sigaction(SIGUSR2, &sa, 0);
		sigaction(SIGHUP,  &sa, 0);
		sigaction(SIGINT,  &sa, 0);
		sigaction(SIGQUIT, &sa, 0);
		sigaction(SIGTERM, &sa, 0);
		sigaction(SIGCHLD, &sa, 0);
		sigprocmask(SIG_SETMASK, &saved, 0);

		int child;
		struct rusage rusage;
		do wait4(pid, &child, 0, &rusage);
		while (WIFSTOPPED(child));

		if (WIFEXITED(child)) {
			child = WEXITSTATUS(child);
		} else {
			child = -WTERMSIG(child);
		}

		gettimeofday(&stop, 0);

		status = 0;
		for (auto &i : files_wrote) files_read.erase(i);

		std::ofstream out(argv[2], std::ios_base::trunc);
		if (out.fail()) {
			std::cerr << "ofstream " << argv[2] << ": " << strerror(errno) << std::endl;
			status = 1;
		}

		struct rusage self;
		getrusage(RUSAGE_SELF, &self);
		rusage.ru_utime.tv_sec  += self.ru_utime.tv_sec;
		rusage.ru_utime.tv_usec += self.ru_utime.tv_usec;
		rusage.ru_stime.tv_sec  += self.ru_stime.tv_sec;
		rusage.ru_stime.tv_usec += self.ru_stime.tv_usec;
		rusage.ru_maxrss        += self.ru_maxrss;
		rusage.ru_inblock       += self.ru_inblock;
		rusage.ru_oublock       += self.ru_oublock;

		bool first;
		out << "{\"usage\":{\"status\":" << child
		  << ",\"runtime\":" << (stop.tv_sec - start.tv_sec + (stop.tv_usec - start.tv_usec)/1000000.0)
		  << ",\"cputime\":" << (rusage.ru_utime.tv_sec + rusage.ru_stime.tv_sec + (rusage.ru_utime.tv_usec + rusage.ru_stime.tv_usec)/1000000.0)
		  << ",\"membytes\":" << rusage.ru_maxrss
		  << ",\"inbytes\":" << rusage.ru_inblock * UINT64_C(512)
		  << ",\"outbytes\":" << rusage.ru_oublock * UINT64_C(512)
		  << "},\"inputs\":[";

		first = true;
		for (auto &x : files_read) {
			out << (first?"":",") << "\"" << escape(x) << "\"";
			first = false;
		}

		out << "],\"outputs\":[";

		first = true;
		for (auto &x : files_wrote) {
			out << (first?"":",") << "\"" << escape(x) << "\"";
			first = false;
		}

		out << "],\"indexes\":[]}" << std::endl;

		if (out.bad()) {
			std::cerr << "bad " << argv[2] << ": " << strerror(errno) << std::endl;
			status = 1;
		}
	}

	return status;
}
