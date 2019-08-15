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

#define MAX_JSON (1024*1024)

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
#include <sys/resource.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <sys/xattr.h>

#include <set>
#include <map>
#include <string>
#include <sstream>
#include "json5.h"
#include "execpath.h"

#ifdef __APPLE__
#define st_mtim st_mtimespec
#define st_ctim st_ctimespec
#endif

#ifndef ENOATTR
#define ENOATTR ENODATA
#endif

//#define TRACE(x) do { fprintf(stderr, "%s: %s\n", __FUNCTION__, x); fflush(stderr); } while (0)
#define TRACE(x) (void)x

// We ensure STDIN is /dev/null, so this is a safe sentinel value for open files
#define BAD_FD STDIN_FILENO

struct Job {
	std::set<std::string> files_visible;
	std::set<std::string> files_read;
	std::set<std::string> files_wrote;
	std::string json_in;
	std::string json_out;
	long ibytes, obytes;
	int json_in_uses;
	int json_out_uses;
	int uses;

	Job() : ibytes(0), obytes(0), json_in_uses(0), json_out_uses(0), uses(0) { }

	void parse();
	void dump();

	bool is_creatable(const std::string &path);
	bool is_writeable(const std::string &path);
	bool is_readable(const std::string &path);
protected:
	bool is_visible(const std::string &path);
};

void Job::parse() {
	JAST jast;
	std::stringstream s;
	if (!JAST::parse(json_in, s, jast)) {
		fprintf(stderr, "Parse error: %s\n", s.str().c_str());
		return;
	}

	// We only need to make the relative paths visible; absolute paths are already
	files_visible.clear();
	for (auto &x : jast.get("visible").children)
		if (!x.second.value.empty() && x.second.value[0] != '/')
			files_visible.insert(std::move(x.second.value));
}

void Job::dump() {
	if (!json_out.empty()) return;

	bool first;
	std::stringstream s;

	s << "{\"ibytes\":"
	  << ibytes
	  << ",\"obytes\":"
	  << obytes
	  << ",\"inputs\":[";

	for (auto &x : files_wrote)
		files_read.erase(x);

	first = true;
	for (auto &x : files_read) {
		s << (first?"":",") << "\"" << json_escape(x) << "\"";
		first = false;
	}

	s << "],\"outputs\":[";

	first = true;
	for (auto &x : files_wrote) {
		s << (first?"":",") << "\"" << json_escape(x) << "\"";
		first = false;
	}

	s << "]}" << std::endl;

	json_out = s.str();
}

struct Context {
	std::map<std::string, Job> jobs;
	int rootfd, uses;
	Context() : jobs(), rootfd(-1), uses(0) { }
};

static Context context;

bool Job::is_visible(const std::string &path) {
	if (files_visible.find(path) != files_visible.end()) return true;

	auto i = files_visible.lower_bound(path + "/");
	return i != files_visible.end()
		&& i->size() > path.size()
		&& (*i)[path.size()] == '/'
		&& 0 == i->compare(0, path.size(), path);
}

bool Job::is_creatable(const std::string &path) {
	return true;
	// is_writeable(path) || faccessat(context.rootfd, path.c_str(), R_OK, 0) != 0;
}

bool Job::is_writeable(const std::string &path) {
	return files_wrote.find(path) != files_wrote.end();
}

bool Job::is_readable(const std::string &path) {
	return is_visible(path) || is_writeable(path);
}

static std::pair<std::string, std::string> split_key(const char *path) {
	const char *end = strchr(path+1, '/');
	if (end) {
		return std::make_pair(std::string(path+1, end), std::string(end+1));
	} else {
		return std::make_pair(std::string(path+1), std::string("."));
	}
}

struct Special {
	std::map<std::string, Job>::iterator job;
	char kind;
	Special() : job(), kind(0) { }
	operator bool () const { return kind; }
};

static Special is_special(const char *path) {
	Special out;

	if (path[0] != '/' || path[1] != '.' || !path[2] || path[3] != '.' || !path[4])
		return out;

	auto it = context.jobs.find(path+4);
	switch (path[2]) {
		case 'f':
			out.kind = strcmp(path+4, "fuse-waked") ? 0 : 'f';
			return out;
		case 'o':
			if (it != context.jobs.end() && !it->second.json_out.empty()) {
				out.kind = path[2];
				out.job = it;
			}
			return out;
		case 'i':
		case 'l':
			if (it != context.jobs.end()) {
				out.kind = path[2];
				out.job = it;
			}
			return out;
		default:
			return out;
	}
}

static int exit_attempts = 0;

static void cancel_exit()
{
	struct itimerval retry;
	memset(&retry, 0, sizeof(retry));
	setitimer(ITIMER_REAL, &retry, 0);
	exit_attempts = 0;
}

static void schedule_exit()
{
	struct itimerval retry;
	memset(&retry, 0, sizeof(retry));
	retry.it_value.tv_sec = 2 << exit_attempts;
	setitimer(ITIMER_REAL, &retry, 0);
}

static int wakefuse_getattr(const char *path, struct stat *stbuf)
{
	TRACE(path);

	if (auto s = is_special(path)) {
		int res = fstat(context.rootfd, stbuf);
		if (res == -1) res = -errno;
		stbuf->st_nlink = 1;
		stbuf->st_ino = 0;
		switch (s.kind) {
			case 'i':
				stbuf->st_mode = S_IFREG | 0644;
				stbuf->st_size = s.job->second.json_in.size();
				return res;
			case 'o':
				stbuf->st_mode = S_IFREG | 0444;
				stbuf->st_size = s.job->second.json_out.size();
				return res;
			case 'l':
				stbuf->st_mode = S_IFREG | 0644;
				stbuf->st_size = 0;
				return res;
			case 'f':
				stbuf->st_mode = S_IFREG | 0444;
				stbuf->st_size = 0;
				return res;
			default:
				return -ENOENT; // unreachable
		}
	}

	auto key = split_key(path);
	if (key.first.empty()) {
		int res = fstat(context.rootfd, stbuf);
		stbuf->st_nlink = 1;
		stbuf->st_ino = 0;
		if (res == -1) res = -errno;
		return res;
	}

	auto it = context.jobs.find(key.first);
	if (it == context.jobs.end())
		return -ENOENT;

	if (key.second == ".") {
		int res = fstat(context.rootfd, stbuf);
		stbuf->st_nlink = 1;
		stbuf->st_ino = 0;
		if (res == -1) res = -errno;
		return res;
	}

	if (!it->second.is_readable(key.second))
		return -ENOENT;

	int res = fstatat(context.rootfd, key.second.c_str(), stbuf, AT_SYMLINK_NOFOLLOW);
	if (res == -1) res = -errno;
	return res;
}

static int wakefuse_access(const char *path, int mask)
{
	TRACE(path);

	if (auto s = is_special(path)) {
		switch (s.kind) {
			case 'o':
			case 'i': return (mask & X_OK) ? -EACCES : 0;
			default:  return (mask & (X_OK|W_OK)) ? -EACCES : 0;
		}
	}

	auto key = split_key(path);
	if (key.first.empty()) return 0;

	auto it = context.jobs.find(key.first);
	if (it == context.jobs.end())
		return -ENOENT;

	if (key.second == ".") return 0;

	if (!it->second.is_readable(key.second))
		return -ENOENT;

	int res = faccessat(context.rootfd, key.second.c_str(), mask, 0);
	if (res == -1)
		return -errno;

	return 0;
}

static int wakefuse_readlink(const char *path, char *buf, size_t size)
{
	TRACE(path);

	if (is_special(path))
		return -EINVAL;

	auto key = split_key(path);
	if (key.first.empty())
		return -EINVAL;

	auto it = context.jobs.find(key.first);
	if (it == context.jobs.end())
		return -ENOENT;

	if (key.second == ".")
		return -EINVAL;

	if (!it->second.is_readable(key.second))
		return -ENOENT;

	int res = readlinkat(context.rootfd, key.second.c_str(), buf, size - 1);
	if (res == -1)
		return -errno;

	buf[res] = '\0';
	it->second.files_read.insert(std::move(key.second));
	return 0;
}

static int wakefuse_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
		       off_t offset, struct fuse_file_info *fi)
{
	(void) offset;
	(void) fi;

	TRACE(path);

	if (is_special(path))
		return -ENOTDIR;

	auto key = split_key(path);
	if (key.first.empty()) {
		filler(buf, ".f.fuse-waked", 0, 0);
		for (auto &job : context.jobs) {
			filler(buf, job.first.c_str(), 0, 0);
			filler(buf, (".l." + job.first).c_str(), 0, 0);
			filler(buf, (".i." + job.first).c_str(), 0, 0);
			if (!job.second.json_out.empty())
			  filler(buf, (".o." + job.first).c_str(), 0, 0);
		}
		return 0;
	}

	auto it = context.jobs.find(key.first);
	if (it == context.jobs.end())
		return -ENOENT;

	int dfd;
	if (key.second == ".") {
		dfd = dup(context.rootfd);
	} else if (!it->second.is_readable(key.second)) {
		return -ENOENT;
	} else {
		dfd = openat(context.rootfd, key.second.c_str(), O_RDONLY | O_NOFOLLOW | O_DIRECTORY);
	}
	if (dfd == -1)
		return -errno;

	DIR *dp = fdopendir(dfd);
	if (dp == NULL) {
		int res = -errno;
		(void)close(dfd);
		return res;
	}

	rewinddir(dp);
	struct dirent *de;
	while ((de = readdir(dp)) != NULL) {
		struct stat st;
		memset(&st, 0, sizeof(st));
		st.st_ino = de->d_ino;
		st.st_mode = de->d_type << 12;

		std::string file;
		if (key.second != ".") {
			file += key.second;
			file += "/";
		}
		file += de->d_name;

		if (!it->second.is_readable(file))
			continue;

		if (filler(buf, de->d_name, &st, 0))
			break;
	}

	(void)closedir(dp);
	return 0;
}

static int wakefuse_mknod(const char *path, mode_t mode, dev_t rdev)
{
	TRACE(path);

	if (is_special(path))
		return -EEXIST;

	auto key = split_key(path);
	if (key.first.empty())
		return -EEXIST;

	auto it = context.jobs.find(key.first);
	if (it == context.jobs.end()) {
		if (key.second == ".")
			return -EACCES;
		else
			return -ENOENT;
	}

	if (key.second == ".")
		return -EEXIST;

	if (!it->second.is_creatable(key.second))
		return -EACCES;

	int res;
	if (S_ISREG(mode)) {
		res = openat(context.rootfd, key.second.c_str(), O_CREAT | O_EXCL | O_WRONLY, mode);
		if (res >= 0)
			res = close(res);
	} else if (S_ISDIR(mode)) {
		res = mkdirat(context.rootfd, key.second.c_str(), mode);
	} else if (S_ISFIFO(mode)) {
#ifdef __APPLE__
		res = mkfifo(key.second.c_str(), mode);
#else
		res = mkfifoat(context.rootfd, key.second.c_str(), mode);
#endif
#ifdef __FreeBSD__
	} else if (S_ISSOCK(mode)) {
		struct sockaddr_un su;
		if (key.second.size() >= sizeof(su.sun_path))
			return -ENAMETOOLONG;

		int fd = socket(AF_UNIX, SOCK_STREAM, 0);
		if (fd >= 0) {
			su.sun_family = AF_UNIX;
			strncpy(su.sun_path, key.second.c_str(), sizeof(su.sun_path));
			res = bindat(context.rootfd, fd, (struct sockaddr*)&su, sizeof(su));
			if (res == 0)
				res = close(fd);
		} else {
			res = -1;
		}
#endif
	} else {
#ifdef __APPLE__
		res = mknod(key.second.c_str(), mode, rdev);
#else
		res = mknodat(context.rootfd, key.second.c_str(), mode, rdev);
#endif
	}

	if (res == -1)
		return -errno;

	return 0;
}

static int wakefuse_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	TRACE(path);

	if (is_special(path))
		return -EEXIST;

	auto key = split_key(path);
	if (key.first.empty())
		return -EEXIST;

	if (key.second == "." && key.first.size() > 3 &&
	    key.first[0] == '.' && key.first[1] == 'l' && key.first[2] == '.' && key.first[3] != '.') {
		++context.jobs[key.first.substr(3)].uses;
		cancel_exit();
		fi->fh = BAD_FD;
		return 0;
	}

	auto it = context.jobs.find(key.first);
	if (it == context.jobs.end()) {
		if (key.second == ".")
			return -EACCES;
		else
			return -ENOENT;
	}

	if (key.second == ".")
		return -EEXIST;

	if (!it->second.is_creatable(key.second))
		return -EACCES;

	(void)unlinkat(context.rootfd, key.second.c_str(), 0);

	int fd = openat(context.rootfd, key.second.c_str(), fi->flags, mode);
	if (fd == -1)
		return -errno;

	fi->fh = fd;
	it->second.files_wrote.insert(std::move(key.second));
	return 0;
}

static int wakefuse_mkdir(const char *path, mode_t mode)
{
	TRACE(path);

	if (is_special(path))
		return -EEXIST;

	auto key = split_key(path);
	if (key.first.empty())
		return -EEXIST;

	auto it = context.jobs.find(key.first);
	if (it == context.jobs.end()) {
		if (key.second == ".")
			return -EACCES;
		else
			return -ENOENT;
	}

	if (key.second == ".")
		return -EEXIST;

	if (!it->second.is_creatable(key.second))
		return -EACCES;

	int res = mkdirat(context.rootfd, key.second.c_str(), mode);
	if (res == -1 && (errno != EEXIST || it->second.is_readable(key.second)))
		return -errno;

	it->second.files_wrote.insert(std::move(key.second));
	return 0;
}

static int wakefuse_unlink(const char *path)
{
	TRACE(path);

	if (is_special(path))
		return -EACCES;

	auto key = split_key(path);
	if (key.first.empty())
		return -EPERM;

	auto it = context.jobs.find(key.first);
	if (it == context.jobs.end())
		return -ENOENT;

	if (key.second == ".")
		return -EPERM;

	if (!it->second.is_readable(key.second))
		return -ENOENT;

	if (!it->second.is_writeable(key.second))
		return -EACCES;

	int res = unlinkat(context.rootfd, key.second.c_str(), 0);
	if (res == -1)
		return -errno;

	it->second.files_wrote.erase(key.second);
	it->second.files_read.erase(key.second);
	return 0;
}

static int wakefuse_rmdir(const char *path)
{
	TRACE(path);

	if (is_special(path))
		return -ENOTDIR;

	auto key = split_key(path);
	if (key.first.empty())
		return -EACCES;

	auto it = context.jobs.find(key.first);
	if (it == context.jobs.end())
		return -ENOENT;

	if (key.second == ".")
		return -EACCES;

	if (!it->second.is_readable(key.second))
		return -ENOENT;

	if (!it->second.is_writeable(key.second))
		return -EACCES;

	int res = unlinkat(context.rootfd, key.second.c_str(), AT_REMOVEDIR);
	if (res == -1)
		return -errno;

	it->second.files_wrote.erase(key.second);
	it->second.files_read.erase(key.second);
	return 0;
}

static int wakefuse_symlink(const char *from, const char *to)
{
	TRACE(to);

	if (is_special(to))
		return -EEXIST;

	auto key = split_key(to);
	if (key.first.empty())
		return -EEXIST;

	auto it = context.jobs.find(key.first);
	if (it == context.jobs.end()) {
		if (key.second == ".")
			return -EACCES;
		else
			return -ENOENT;
	}

	if (key.second == ".")
		return -EEXIST;

	if (!it->second.is_creatable(key.second))
		return -EACCES;

	int res = symlinkat(from, context.rootfd, key.second.c_str());
	if (res == -1)
		return -errno;

	it->second.files_wrote.insert(std::move(key.second));
	return 0;
}

static void move_members(std::set<std::string> &from, std::set<std::string> &to, const std::string &dir, const std::string &dest)
{
	auto i = from.upper_bound(dir + "/");
	auto e = from.lower_bound(dir + "0");
	while (i != e) {
		auto kill = i++;
		to.insert(dest + kill->substr(dir.size()));
		from.erase(kill);
	}
}

static int wakefuse_rename(const char *from, const char *to)
{
	TRACE(from);

	if (is_special(to))
		return -EEXIST;

	if (is_special(from))
		return -EACCES;

	auto keyt = split_key(to);
	if (keyt.first.empty())
		return -EEXIST;

	auto keyf = split_key(from);
	if (keyf.first.empty())
		return -EACCES;

	auto it = context.jobs.find(keyf.first);
	if (it == context.jobs.end())
		return -ENOENT;

	if (keyf.second == ".")
		return -EACCES;

	if (keyt.second == ".") {
		if (context.jobs.find(keyt.first) == context.jobs.end())
			return -EACCES;
		else
			return -EEXIST;
	}

	if (keyt.first != keyf.first)
		return -EXDEV;

	if (!it->second.is_readable(keyf.second))
		return -ENOENT;

	if (!it->second.is_writeable(keyf.second))
		return -EACCES;

	if (!it->second.is_creatable(keyt.second))
		return -EACCES;

	int res = renameat(context.rootfd, keyf.second.c_str(), context.rootfd, keyt.second.c_str());
	if (res == -1)
		return -errno;

	it->second.files_wrote.erase(keyf.second);
	it->second.files_read.erase(keyf.second);
	it->second.files_wrote.insert(keyt.second);

	// Move any children as well
	move_members(it->second.files_wrote, it->second.files_wrote, keyf.second, keyt.second);
	move_members(it->second.files_read,  it->second.files_wrote, keyf.second, keyt.second);

	return 0;
}

static int wakefuse_link(const char *from, const char *to)
{
	TRACE(from);

	if (is_special(to))
		return -EEXIST;

	if (is_special(from))
		return -EACCES;

	auto keyt = split_key(to);
	if (keyt.first.empty())
		return -EEXIST;

	auto keyf = split_key(from);
	if (keyf.first.empty())
		return -EACCES;

	auto it = context.jobs.find(keyf.first);
	if (it == context.jobs.end())
		return -ENOENT;

	if (keyf.second == ".")
		return -EACCES;

	if (keyt.second == ".") {
		if (context.jobs.find(keyt.first) == context.jobs.end())
			return -EACCES;
		else
			return -EEXIST;
	}

	if (keyt.first != keyf.first)
		return -EXDEV;

	if (!it->second.is_readable(keyf.second))
		return -ENOENT;

	if (!it->second.is_creatable(keyt.second))
		return -EACCES;

	int res = linkat(context.rootfd, keyf.second.c_str(), context.rootfd, keyt.second.c_str(), 0);
	if (res == -1)
		return -errno;

	it->second.files_wrote.insert(std::move(keyt.second));
	return 0;
}

static int wakefuse_chmod(const char *path, mode_t mode)
{
	TRACE(path);

	if (is_special(path))
		return -EACCES;

	auto key = split_key(path);
	if (key.first.empty())
		return -EACCES;

	auto it = context.jobs.find(key.first);
	if (it == context.jobs.end())
		return -ENOENT;

	if (key.second == ".")
		return -EACCES;

	if (!it->second.is_readable(key.second))
		return -ENOENT;

	if (!it->second.is_writeable(key.second))
		return -EACCES;

#ifdef __linux__
	// Linux is broken and violates POSIX by returning EOPNOTSUPP even for non-symlinks
	int res = fchmodat(context.rootfd, key.second.c_str(), mode, 0);
#else
	int res = fchmodat(context.rootfd, key.second.c_str(), mode, AT_SYMLINK_NOFOLLOW);
#endif
	if (res == -1)
		return -errno;

	return 0;
}

static int wakefuse_chown(const char *path, uid_t uid, gid_t gid)
{
	TRACE(path);

	if (is_special(path))
		return -EACCES;

	auto key = split_key(path);
	if (key.first.empty())
		return -EACCES;

	auto it = context.jobs.find(key.first);
	if (it == context.jobs.end())
		return -ENOENT;

	if (key.second == ".")
		return -EACCES;

	if (!it->second.is_readable(key.second))
		return -ENOENT;

	if (!it->second.is_writeable(key.second))
		return -EACCES;

	int res = fchownat(context.rootfd, key.second.c_str(), uid, gid, AT_SYMLINK_NOFOLLOW);
	if (res == -1)
		return -errno;

	return 0;
}

static int wakefuse_truncate(const char *path, off_t size)
{
	TRACE(path);

	if (auto s = is_special(path)) {
		switch (s.kind) {
			case 'i':
				if (size <= MAX_JSON) {
					s.job->second.json_in.resize(size);
					return 0;
				} else {
					return -ENOSPC;
				}
			default:
				return -EACCES;
		}
	}

	auto key = split_key(path);
	if (key.first.empty())
		return -EISDIR;

	auto it = context.jobs.find(key.first);
	if (it == context.jobs.end())
		return -ENOENT;

	if (key.second == ".")
		return -EISDIR;

	if (!it->second.is_readable(key.second))
		return -ENOENT;

	if (!it->second.is_writeable(key.second))
		return -EACCES;

	int fd = openat(context.rootfd, key.second.c_str(), O_WRONLY | O_NOFOLLOW);
	if (fd == -1)
		return -errno;

	int res = ftruncate(fd, size);
	if (res == -1) {
		res = -errno;
		(void)close(fd);
		return res;
	} else {
		it->second.files_wrote.insert(std::move(key.second));
		(void)close(fd);
		return 0;
	}
}

static int wakefuse_utimens(const char *path, const struct timespec ts[2])
{
	TRACE(path);

	if (is_special(path))
		return -EACCES;

	auto key = split_key(path);
	if (key.first.empty())
		return -EACCES;

	auto it = context.jobs.find(key.first);
	if (it == context.jobs.end())
		return -ENOENT;

	if (key.second == ".")
		return -EACCES;

	if (!it->second.is_readable(key.second))
		return -ENOENT;

	if (!it->second.is_writeable(key.second))
		return -EACCES;

	int res = utimensat(context.rootfd, key.second.c_str(), ts, AT_SYMLINK_NOFOLLOW);
	if (res == -1)
		return -errno;

	it->second.files_wrote.insert(std::move(key.second));
	return 0;
}

static int wakefuse_open(const char *path, struct fuse_file_info *fi)
{
	TRACE(path);

	if (auto s = is_special(path)) {
		switch (s.kind) {
			case 'f': ++context.uses; cancel_exit(); break;
			case 'i': ++s.job->second.json_in_uses; break;
			case 'o': ++s.job->second.json_out_uses; break;
			case 'l': ++s.job->second.uses; break;
			default: return -ENOENT; // unreachable
		}
		fi->fh = BAD_FD;
		return 0;
	}

	auto key = split_key(path);
	if (key.first.empty())
		return -EINVAL; // open is for files only

	auto it = context.jobs.find(key.first);
	if (it == context.jobs.end())
		return -ENOENT;

	if (key.second == ".")
		return -EINVAL;

	if (!it->second.is_readable(key.second))
		return -ENOENT;

	int fd = openat(context.rootfd, key.second.c_str(), fi->flags);
	if (fd == -1)
		return -errno;

	fi->fh = fd;
	return 0;
}

static int read_str(const std::string &str, char *buf, size_t size, off_t offset)
{
	if (offset >= (ssize_t)str.size()) {
		return 0;
	} else {
		size_t got = std::min(str.size() - (size_t)offset, size);
		memcpy(buf, str.data() + offset, got);
		return got;
	}
}

static int wakefuse_read(const char *path, char *buf, size_t size, off_t offset,
		    struct fuse_file_info *fi)
{
	TRACE(path);

	if (fi->fh != BAD_FD) {
		auto key = split_key(path);
		auto it = context.jobs.find(key.first);
		if (it == context.jobs.end())
			return -ENOENT;

		int res = pread(fi->fh, buf, size, offset);
		if (res == -1)
			res = -errno;

		it->second.ibytes += res;
		it->second.files_read.insert(std::move(key.second));
		return res;
	}

	if (auto s = is_special(path)) {
		switch (s.kind) {
			case 'i': return read_str(s.job->second.json_in, buf, size, offset);
			case 'o': return read_str(s.job->second.json_out, buf, size, offset);
			default:  return 0;
		}
	}

	return -EIO;
}

static int write_str(std::string &str, const char *buf, size_t size, off_t offset)
{
	if (offset >= MAX_JSON) {
		return 0;
	} else {
		size_t end = std::min((off_t)MAX_JSON, offset+(off_t)size);
		size_t got = end - offset;
		if (end > str.size()) str.resize(end);
		str.replace(offset, got, buf, got);
		return got;
	}
}

static int wakefuse_write(const char *path, const char *buf, size_t size,
		     off_t offset, struct fuse_file_info *fi)
{
	TRACE(path);

	if (fi->fh != BAD_FD) {
		auto key = split_key(path);
		auto it = context.jobs.find(key.first);
		if (it == context.jobs.end())
			return -ENOENT;

		int res = pwrite(fi->fh, buf, size, offset);
		if (res == -1)
			res = -errno;

		it->second.obytes += res;
		it->second.files_wrote.insert(std::move(key.second));
		return res;
	}

	if (auto s = is_special(path)) {
		switch (s.kind) {
			case 'i': return write_str(s.job->second.json_in, buf, size, offset);
			case 'l': s.job->second.dump(); return -ENOSPC;
			default:  return -EACCES;
		}
	}

	return -EIO;
}

static int wakefuse_statfs(const char *path, struct statvfs *stbuf)
{
	TRACE(path);

	int fd;
	auto key = split_key(path);
	if (key.first.empty() || is_special(path)) {
		fd = dup(context.rootfd);
	} else {
		auto it = context.jobs.find(key.first);
		if (it == context.jobs.end()) {
			return -ENOENT;
		} else if (key.second == ".") {
			fd = dup(context.rootfd);
		} else if (!it->second.is_readable(key.second)) {
			return -ENOENT;
		} else {
			fd = openat(context.rootfd, key.second.c_str(), O_RDONLY | O_NOFOLLOW);
		}
	}
	if (fd == -1)
		return -errno;

	int res = fstatvfs(fd, stbuf);
	if (res == -1) {
		res = -errno;
		(void)close(fd);
		return res;
	} else {
		(void)close(fd);
		return 0;
	}
}

static int wakefuse_release(const char *path, struct fuse_file_info *fi)
{
	TRACE(path);

	if (fi->fh != BAD_FD) {
		int res = close(fi->fh);
		if (res == -1)
			return -errno;
	}

	if (auto s = is_special(path)) {
		switch (s.kind) {
			case 'f': --context.uses; break;
			case 'i': if (--s.job->second.json_in_uses == 0) s.job->second.parse(); break;
			case 'o': --s.job->second.json_out_uses; break;
			case 'l': --s.job->second.uses; break;
			default: return -EIO;
		}
		if ('f' != s.kind &&
		    0 == s.job->second.uses &&
		    0 == s.job->second.json_in_uses &&
		    0 == s.job->second.json_out_uses) {
			context.jobs.erase(s.job);
		}
		if (context.jobs.empty() && 0 == context.uses)
			schedule_exit();
	}

	return 0;
}

static int wakefuse_fsync(const char *path, int isdatasync,
		     struct fuse_file_info *fi)
{
	int res;

	TRACE(path);

	if (fi->fh == BAD_FD)
		return 0;

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

#ifdef HAVE_FALLOCATE
static int wakefuse_fallocate(const char *path, int mode,
			off_t offset, off_t length, struct fuse_file_info *fi)
{
	(void) fi;

	TRACE(path);

	if (mode)
		return -EOPNOTSUPP;

	if (is_special(path))
		return -EACCES;

	auto key = split_key(path);
	if (key.first.empty())
		return -EISDIR;

	auto it = context.jobs.find(key.first);
	if (it == context.jobs.end())
		return -ENOENT;

	if (key.second == ".")
		return -EISDIR;

	if (!it->second.is_readable(key.second))
		return -ENOENT;

	if (!it->second.is_writeable(key.second))
		return -EACCES;

	int fd = openat(context.rootfd, key.second.c_str(), O_WRONLY | O_NOFOLLOW);
	if (fd == -1)
		return -errno;

	int res = posix_fallocate(fd, offset, length);
	if (res != 0) {
		(void)close(fd);
		return -res;
	} else {
		it->second.files_wrote.insert(std::move(key.second));
		(void)close(fd);
		return 0;
	}
}
#endif

static std::string path;
static struct fuse* fh;
static struct fuse_chan* fc;
static sigset_t saved;

static struct fuse_operations wakefuse_ops;

static void *wakefuse_init(struct fuse_conn_info *conn)
{
	// unblock signals
	sigprocmask(SIG_SETMASK, &saved, 0);

	return 0;
}

static void handle_exit(int sig)
{
	static pid_t pid = -1;
	// Unfortunately, fuse_unmount can fail if the filesystem is still in use.
	// Yes, this can even happen on linux with MNT_DETACH / lazy umount.
	// Worse, fuse_unmount closes descriptors and frees memory, so can only be called once.
	// Thus, calling fuse_exit here would terminate fuse_loop and then maybe fail to unmount.

	// Instead of terminating the loop directly via fuse_exit, try to unmount.
	// If this succeeds, fuse_loop will terminate anyway.
	// In case it fails, we setup an itimer to keep trying to unmount.

	// Reap prior attempts
	if (pid != -1) {
		int status;
		do waitpid(pid, &status, 0);
		while (WIFSTOPPED(status));
	}

	if (exit_attempts == 3) {
		fprintf(stderr, "Unable to cleanly exit after 4 unmount attempts\n");
		exit(1);
	} else if ((pid = fork()) == 0) {
		// We need to fork before fuse_unmount, in order to be able to try more than once.
		fuse_unmount(path.c_str(), fc);
		exit(0);
	} else {
		++exit_attempts;
		schedule_exit();
	}
}

int main(int argc, char *argv[])
{
	wakefuse_ops.init		= wakefuse_init;
	wakefuse_ops.getattr		= wakefuse_getattr;
	wakefuse_ops.access		= wakefuse_access;
	wakefuse_ops.readlink		= wakefuse_readlink;
	wakefuse_ops.readdir		= wakefuse_readdir;
	wakefuse_ops.mknod		= wakefuse_mknod;
	wakefuse_ops.create		= wakefuse_create;
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
	// xattr were removed because they are not hashed!
#ifdef HAVE_FALLOCATE
	wakefuse_ops.fallocate		= wakefuse_fallocate;
#endif

	int status = 1;
	sigset_t block;
	struct sigaction sa;
	struct fuse_args args;
	pid_t pid;
	int log, null;
	bool madedir;

	if (argc != 2) {
		fprintf(stderr, "Syntax: fuse-waked <mount-point>\n");
		goto term;
	}
	path = argv[1];

	null = open("/dev/null", O_RDONLY);
	if (null == -1) {
		perror("open /dev/null");
		goto term;
	}

	log = open((path + ".log").c_str(), O_CREAT|O_RDWR|O_APPEND, 0644); // S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	if (log == -1) {
		fprintf(stderr, "open %s.log: %s\n", path.c_str(), strerror(errno));
		goto term;
	}

	if (flock(log, LOCK_EX|LOCK_NB) != 0) {
		if (errno == EWOULDBLOCK) {
			status = 0; // another daemon is already running
		} else {
			fprintf(stderr, "flock %s.log: %s\n", path.c_str(), strerror(errno));
		}
		goto term;
	}

	umask(0);

	context.rootfd = open(".", O_RDONLY);
	if (context.rootfd == -1) {
		perror("open .");
		goto term;
	}

	madedir = mkdir(path.c_str(), 0775) == 0;
	if (!madedir && errno != EEXIST) {
		fprintf(stderr, "mkdir %s: %s\n", path.c_str(), strerror(errno));
		goto rmroot;
	}

	pid = fork();
	if (pid == -1) {
		perror("fork");
		goto rmroot;
	} else if (pid != 0) {
		status = 0;
		goto term;
	}

	if (setsid() == -1) {
		perror("setsid");
		goto rmroot;
	}

	pid = fork();
	if (pid == -1) {
		perror("fork2");
		goto rmroot;
	} else if (pid != 0) {
		status = 0;
		goto term;
	}

	// block those signals where we wish to terminate cleanly
	sigemptyset(&block);
	sigaddset(&block, SIGINT);
	sigaddset(&block, SIGQUIT);
	sigaddset(&block, SIGTERM);
	sigaddset(&block, SIGALRM);
	sigprocmask(SIG_BLOCK, &block, &saved);

	memset(&sa, 0, sizeof(sa));

	// ignore these signals
	sa.sa_handler = SIG_IGN;
	sa.sa_flags = SA_RESTART;
	sigaction(SIGPIPE, &sa, 0);
	sigaction(SIGUSR1, &sa, 0);
	sigaction(SIGUSR2, &sa, 0);
	sigaction(SIGHUP,  &sa, 0);

	// hook these signals
	sa.sa_handler = handle_exit;
	sa.sa_flags = SA_RESTART;
	sigaction(SIGINT,  &sa, 0);
	sigaction(SIGQUIT, &sa, 0);
	sigaction(SIGTERM, &sa, 0);
	sigaction(SIGALRM, &sa, 0);

	args = FUSE_ARGS_INIT(0, NULL);
	if (fuse_opt_add_arg(&args, "wake") != 0) {
		fprintf(stderr, "fuse_opt_add_arg failed\n");
		goto rmroot;
	}

	fc = fuse_mount(path.c_str(), &args);
	if (!fc) {
		fprintf(stderr, "fuse_mount failed\n");
		goto freeargs;
	}

	fh = fuse_new(fc, &args, &wakefuse_ops, sizeof(wakefuse_ops), 0);
	if (!fh) {
		fprintf(stderr, "fuse_new failed\n");
		goto unmount;
	}

	fflush(stdout);
	fflush(stderr);

	if (log != STDOUT_FILENO) dup2(log, STDOUT_FILENO);
	if (log != STDERR_FILENO) dup2(log, STDERR_FILENO);
	if (log != STDOUT_FILENO && log != STDERR_FILENO) close(log);

	if (null != STDIN_FILENO) {
		dup2(null, STDIN_FILENO);
		close(null);
	}

	if (fuse_loop(fh) != 0) {
		fprintf(stderr, "fuse_loop failed");
		goto unmount;
	}

	status = 0;

	// Block signals again
	sigprocmask(SIG_BLOCK, &block, 0);

unmount:
	// out-of-order completion: unmount THEN destroy
	fuse_unmount(path.c_str(), fc);
	if (fh) fuse_destroy(fh);
freeargs:
	fuse_opt_free_args(&args);
rmroot:
	if (madedir && rmdir(path.c_str()) != 0) {
		fprintf(stderr, "rmdir %s: %s\n", path.c_str(), strerror(errno));
	}
term:
	return status;
}
