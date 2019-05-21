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

#define MAX_JSON 65536

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
#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif

#include <set>
#include <map>
#include <string>
#include <iostream>
#include <fstream>
#include "json5.h"
#include "execpath.h"

//#define TRACE(x) do { fprintf(stderr, "%s: %s\n", __FUNCTION__, x); fflush(stderr); } while (0)
#define TRACE(x) (void)x

struct Job {
	std::set<std::string> files_visible;
	std::set<std::string> files_read;
	std::set<std::string> files_wrote;
	std::string json_in;
	std::string json_out;
	int json_in_uses;
	int json_out_uses;
	int uses;

	Job() : json_in_uses(0), json_out_uses(0), uses(0) { }

	bool is_creatable(const std::string &path);
	bool is_writeable(const std::string &path);
	bool is_readable(const std::string &path);
protected:
	bool is_visible(const std::string &path);
};

struct Context {
	int rootfd;
	std::map<std::string, Job> jobs;
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
	Job *job;
	char kind;
	operator bool () const { return job; }
};

static Special is_special(const char *path) {
	Special out;
	bool special =
		path[0] == '/' && path[1] == '.' && path[3] == '.' && path[4]
		&& (path[2] == 'i' || path[2] == 'o' || path[2] == 'l');

	if (special) {
		auto it = context.jobs.find(path+4);
		if (it != context.jobs.end()) {
			out.kind = path[2];
			out.job = &it->second;
			return out;
		}
	}

	out.kind = 0;
	out.job = 0;
	return out;
}

static int wakefuse_getattr(const char *path, struct stat *stbuf)
{
	TRACE(path);

	if (auto s = is_special(path)) {
		int res = fstat(context.rootfd, stbuf);
		if (res == -1) res = -errno;
		stbuf->st_nlink = 1;
		switch (s.kind) {
			case 'i':
				stbuf->st_mode = S_IFREG | 0644;
				stbuf->st_size = s.job->json_in.size();
				return res;
			case 'o':
				stbuf->st_mode = S_IFREG | 0444;
				stbuf->st_size = s.job->json_out.size();
				return res;
			case 'l':
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
		if (res == -1) res = -errno;
		return res;
	}

	auto it = context.jobs.find(key.first);
	if (it == context.jobs.end()) {
		return -ENOENT;
	}

	if (key.second == ".") {
		int res = fstat(context.rootfd, stbuf);
		if (res == -1) res = -errno;
		return res;
	}

	if (!it->second.is_readable(key.second)) {
		return -ENOENT;
	}

	int res = fstatat(context.rootfd, key.second.c_str(), stbuf, AT_SYMLINK_NOFOLLOW);
	if (res == -1) res = -errno;
	return res;
}

static int wakefuse_access(const char *path, int mask)
{
	TRACE(path);

	if (auto s = is_special(path)) {
		switch (s.kind) {
			case 'i': return (mask & X_OK) ? -EACCES : 0;
			case 'o': return (mask & (X_OK|W_OK)) ? -EACCES : 0;
			case 'l': return (mask & (X_OK|W_OK)) ? -EACCES : 0;
			default:  return -ENOENT; // unreachable
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
	it->second.files_read.emplace(std::move(key.second));
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
		for (auto &job : context.jobs) {
			filler(buf, job.first.c_str(), 0, 0);
			filler(buf, (".i." + job.first).c_str(), 0, 0);
			filler(buf, (".o." + job.first).c_str(), 0, 0);
			filler(buf, (".l." + job.first).c_str(), 0, 0);
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

static int wakefuse_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	TRACE(path);

	if (!S_ISREG(mode))
		return -EPERM;

	if (is_special(path))
		return -EEXIST;

	auto key = split_key(path);
	if (key.first.empty())
		return -EEXIST;

	if (key.second == "." && key.first.size() > 3 &&
	    key.first[0] == '.' && key.first[1] == 'l' && key.first[2] == '.') {
		++context.jobs[key.first.substr(3)].uses;
		fi->fh = -1;
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

	int flag = O_CREAT | O_RDWR | O_NOFOLLOW;
	if (!it->second.is_readable(key.second))
		flag |= O_TRUNC;

	int fd = openat(context.rootfd, key.second.c_str(), flag, mode);
	if (fd == -1)
		return -errno;

	fi->fh = fd;
	it->second.files_wrote.emplace(std::move(key.second));
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

	it->second.files_wrote.emplace(std::move(key.second));
	return 0;
}

static int wakefuse_unlink(const char *path)
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

	it->second.files_wrote.emplace(std::move(key.second));
	return 0;
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

	if (keyt.first != keyf.second)
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
	it->second.files_wrote.emplace(std::move(keyt.second));

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

	if (keyt.first != keyf.second)
		return -EXDEV;

	if (!it->second.is_readable(keyf.second))
		return -ENOENT;

	if (!it->second.is_creatable(keyt.second))
		return -EACCES;

	int res = linkat(context.rootfd, keyf.second.c_str(), context.rootfd, keyt.second.c_str(), 0);
	if (res == -1)
		return -errno;

	it->second.files_wrote.emplace(std::move(keyt.second));
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

	int res = fchmodat(context.rootfd, key.second.c_str(), mode, AT_SYMLINK_NOFOLLOW);
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

	int res = ftruncate(fd, size);
	if (res == -1) {
		res = -errno;
		(void)close(fd);
		return res;
	} else {
		it->second.files_wrote.emplace(std::move(key.second));
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

	it->second.files_wrote.emplace(std::move(key.second));
	return 0;
}

static int wakefuse_open(const char *path, struct fuse_file_info *fi)
{
	TRACE(path);

	if (auto s = is_special(path)) {
		switch (s.kind) {
			case 'i': ++s.job->json_in_uses; break;
			case 'o': ++s.job->json_out_uses; break;
			case 'l': ++s.job->uses; break;
			default: return -ENOENT; // unreachable
		}
		fi->fh = -1;
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

	int fd = openat(context.rootfd, key.second.c_str(), O_RDWR | O_NOFOLLOW);
	if (fd == -1)
		return -errno;

	fi->fh = fd;
	return 0;
}

static int read_str(const std::string &str, char *buf, size_t size, off_t offset)
{
	if (offset >= str.size()) {
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

	if (fi->fh != -1) {
		auto key = split_key(path);
		auto it = context.jobs.find(key.first);
		if (it == context.jobs.end())
			return -ENOENT;

		int res = pread(fi->fh, buf, size, offset);
		if (res == -1)
			res = -errno;

		it->second.files_read.emplace(std::move(key.second));
		return res;
	}

	if (auto s = is_special(path)) {
		switch (s.kind) {
			case 'i': return read_str(s.job->json_in, buf, size, offset);
			case 'o': return read_str(s.job->json_out, buf, size, offset);
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
		size_t end = std::min((size_t)MAX_JSON, (size_t)offset+size);
		size_t got = end - offset;
		if (end > str.size()) str.resize(end);
		str.replace(offset, got, buf);
		return got;
	}
}

static int wakefuse_write(const char *path, const char *buf, size_t size,
		     off_t offset, struct fuse_file_info *fi)
{
	TRACE(path);

	if (fi->fh != -1) {
		auto key = split_key(path);
		auto it = context.jobs.find(key.first);
		if (it == context.jobs.end())
			return -ENOENT;

		int res = pwrite(fi->fh, buf, size, offset);
		if (res == -1)
			res = -errno;

		it->second.files_wrote.emplace(std::move(key.second));
		return res;
	}

	if (auto s = is_special(path)) {
		switch (s.kind) {
			case 'i': return write_str(s.job->json_in, buf, size, offset);
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

	if (fi->fh != -1) {
		int res = close(fi->fh);
		if (res == -1)
			return -errno;
	}

	if (auto s = is_special(path)) {
		switch (s.kind) {
			case 'i': --s.job->json_in_uses; break;
			case 'o': --s.job->json_out_uses; break;
			case 'l': --s.job->uses; break;
			default: return -EIO;
		}
	}

	return -EIO;
}

static int wakefuse_fsync(const char *path, int isdatasync,
		     struct fuse_file_info *fi)
{
	int res;

	TRACE(path);

	if (fi->fh == -1)
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

#ifdef HAVE_POSIX_FALLOCATE
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
		it->second.files_wrote.emplace(std::move(key.second));
		(void)close(fd);
		return 0;
	}
}
#endif

#ifdef HAVE_SETXATTR
/* xattr operations are optional and can safely be left unimplemented */
static int wakefuse_setxattr(const char *path, const char *name, const char *value,
			size_t size, int flags)
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

	fd = openat(context.rootfd, key.second.c_str(), O_WRONLY | O_NOFOLLOW);
	if (fd == -1)
		return -errno;

	res = fsetxattr(fd, name, value, size, flags);
	if (res == -1) {
		res = -errno;
		(void)close(fd);
		return res;
	} else {
		it->second.files_wrote.emplace(std::move(key.second));
		(void)close(fd);
		return 0;
	}
}

static int wakefuse_getxattr(const char *path, const char *name, char *value,
			size_t size)
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

	int fd;
	if (key.second == ".") {
		fd = dup(context.rootfd);
	} else {
		if (!it->second.is_readable(key.second))
			return -ENOENT;
		fd = openat(context.rootfd, key.second.c_str(), O_RDONLY | O_NOFOLLOW);
	}
	if (fd == -1)
		return -errno;

	res = fgetxattr(fd, name, value, size);
	if (res == -1) {
		res = -errno;
		(void)close(fd);
		return res;
	} else {
		it->second.files_read.emplace(std::move(key.second));
		(void)close(fd);
		return 0;
	}
}

static int wakefuse_listxattr(const char *path, char *list, size_t size)
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

	int fd;
	if (key.second == ".") {
		fd = dup(context.rootfd);
	} else {
		if (!it->second.is_readable(key.second))
			return -ENOENT;
		fd = openat(context.rootfd, key.second.c_str(), O_RDONLY | O_NOFOLLOW);
	}
	if (fd == -1)
		return -errno;

	int res = flistxattr(fd, list, size);
	if (res == -1) {
		res = -errno;
		(void)close(fd);
		return res;
	} else {
		it->second.files_read.emplace(std::move(key.second));
		(void)close(fd);
		return 0;
	}
}

static int wakefuse_removexattr(const char *path, const char *name)
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

	int fd = openat(context.rootfd, key.second.c_str(), O_WRONLY | O_NOFOLLOW);
	if (fd == -1)
		return -errno;

	int res = fremovexattr(fd, name);
	if (res == -1) {
		res = -errno;
		(void)close(fd);
		return res;
	} else {
		it->second.files_wrote.emplace(std::move(key.second));
		(void)close(fd);
		return 0;
	}
}
#endif /* HAVE_SETXATTR */

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

int main(int argc, char *argv[])
{
	wakefuse_ops.init		= wakefuse_init;
	wakefuse_ops.getattr		= wakefuse_getattr;
	wakefuse_ops.access		= wakefuse_access;
	wakefuse_ops.readlink		= wakefuse_readlink;
	wakefuse_ops.readdir		= wakefuse_readdir;
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
	sigset_t block;
	struct sigaction sa;
	struct fuse_args args;
	pid_t pid;
	int log, null;

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

	if (mkdir(path.c_str(), 0775) != 0 && errno != EEXIST) {
		fprintf(stderr, "mkdir %s: %s\n", path.c_str(), strerror(errno));
		goto term;
	}

	pid = fork();
	if (pid == -1) {
		perror("fork");
		goto term;
	} else if (pid != 0) {
		status = 0;
		goto term;
	}

	if (setsid() == -1) {
		perror("setsid");
		goto term;
	}

	pid = fork();
	if (pid == -1) {
		perror("fork2");
		goto term;
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
	if (rmdir(path.c_str()) != 0) {
		fprintf(stderr, "rmdir %s: %s\n", path.c_str(), strerror(errno));
	}
term:
	return status;
}
