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

// Open Group Base Specifications Issue 7
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include "execpath.h"
#include "whereami.h"
#include <unistd.h>
#include <errno.h>
#include <memory>
#include <vector>
#include <iostream>
#include <string.h>

std::string find_execpath() {
  static std::string exepath;
  if (exepath.empty()) {
    int dirlen = wai_getExecutablePath(0, 0, 0) + 1;
    std::unique_ptr<char[]> execbuf(new char[dirlen]);
    wai_getExecutablePath(execbuf.get(), dirlen, &dirlen);
    exepath.assign(execbuf.get(), dirlen);
  }
  return exepath;
}

static bool check_exec(const char *tok, size_t len, const std::string &exec, std::string &out) {
  out.assign(tok, len);
  out += "/";
  out += exec;
  return access(out.c_str(), X_OK) == 0;
}

std::string find_in_path(const std::string &file, const std::string &path) {
  if (file.find('/') != std::string::npos)
    return file;

  std::string out;
  const char *tok = path.c_str();
  const char *end = tok + path.size();
  for (const char *scan = tok; scan != end; ++scan) {
    if (*scan == ':') {
      if (scan != tok && check_exec(tok, scan-tok, file, out)) return out;
      tok = scan+1;
    }
  }

  if (end != tok && check_exec(tok, end-tok, file, out)) return out;

  // If not found, return input unmodified => runJob fails somewhat gracefully
  return file;
}

std::string find_path(const char *const * env) {
  for (; *env; ++env) {
    if (!strncmp(*env, "PATH=", 5)) {
      return std::string(*env + 5);
    }
  }
  return ".:/bin:/usr/bin";
}

std::string find_path(const std::vector<std::string> &env) {
  for (auto &s : env) {
    if (!s.compare(0, 5, "PATH=")) {
      return s.substr(5);
    }
  }
  return ".:/bin:/usr/bin";
}

std::string get_cwd() {
  std::vector<char> buf;
  buf.resize(1024, '\0');
  errno = 0;
  while (getcwd(buf.data(), buf.size()) == 0 && errno == ERANGE)
    buf.resize(buf.size() * 2);
  if (buf[0] == 0)
    std::cerr << "Unable to read current working directory: " << strerror(errno) << std::endl;
  return buf.data();
}
