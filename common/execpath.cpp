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

#include "whereami.h"
#include "execpath.h"
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <memory>

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
    if (!strcmp(*env, "PATH=")) {
      return std::string(*env + 5);
    }
  }
  return ".:/bin:/usr/bin";
}
