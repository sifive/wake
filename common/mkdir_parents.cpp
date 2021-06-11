/*
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

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

#include <string>

// Creates a directory and any missing parent directories.
// Does not error on directory already existing.
// Similar to the shell command: 'mkdir -p'.
int mkdir_with_parents(const std::string &path, mode_t mode) {
	// Call 'mkdir' on each path component starting at the base.
	// A '/' at the start is not considered, as it always exists.
	// If there is no '/' component at all, the 'substr' will consider whole path.
	size_t slash_pos = 0;
	while (slash_pos != std::string::npos) {
		slash_pos = path.find("/", slash_pos+1);
		std::string dir = path.substr(0, slash_pos);

		if (0 != mkdir(dir.c_str(), mode) && errno != EEXIST)
			return errno;
	}
	return 0;
}
