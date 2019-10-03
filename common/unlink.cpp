/*
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

#include "unlink.h"
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>

int deep_unlink(int parentfd, const char *path) {
	// Set the directory read-write-execute for removing contents.
	// However, if this fails, ignore it (later removes will fail).
	int ignore = fchmodat(parentfd, path, S_IRWXU|S_IRWXG|S_IRWXO, 0);
	(void)ignore;

	// Capture a persistent handle to the directory
	int dirfd = openat(parentfd, path, O_RDONLY|O_DIRECTORY);
	if (dirfd == -1) {
		if (errno == ENOTDIR) {
			// The directory became a file between readdir and openat.
			// This should not count as failure unless we can't remove it.
			if (unlinkat(parentfd, path, 0)) {
				return -errno;
			} else {
				return 0;
			}
		} else {
			return -errno;
		}
	}

	// This could fail due to lack of memory
	DIR *dir = fdopendir(dirfd);
	if (!dir) {
		(void)close(dirfd);
		return -errno;
	}

	int out = 0;
	bool isdir;

	// SUSv3 explicitly notes that it is unspecified whether readdir()
	// will return a filename that has been added to or removed from
	// since the last since the last call to opendir() or rewinddir(). 
	// All filenames that have been neither added nor removed since the
	// last such call are guaranteed to be returned.
	for (errno = 0; struct dirent *f = readdir(dir); errno = 0) {
		if (f->d_name[0] == '.' && (f->d_name[1] == 0 || (f->d_name[1] == '.' && f->d_name[2] == 0)))
			continue;
#ifdef DT_DIR
		if (f->d_type == DT_UNKNOWN) {
#endif
			struct stat sbuf;
			if (fstatat(dirfd, f->d_name, &sbuf, AT_SYMLINK_NOFOLLOW)) {
				isdir = false;
			} else {
				isdir = S_ISDIR(sbuf.st_mode);
			}
#ifdef DT_DIR
		} else {
			isdir = f->d_type == DT_DIR;
		}
#endif
		if (isdir) {
			if (int r = deep_unlink(dirfd, f->d_name)) out = r;
		} else {
			if (unlinkat(dirfd, f->d_name, 0)) out = -errno;
		}
	}

	if (errno) out = -errno;
	if (closedir(dir)) out = -errno;

	// Remove the (hopefully) now empty directory
	if (unlinkat(parentfd, path, AT_REMOVEDIR)) out = -errno;

	return out;
}
