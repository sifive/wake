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

#include <re2/re2.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>

#include <memory>
#include <sstream>
#include <fstream>
#include <algorithm>

#include "compat/readable.h"
#include "compat/windows.h"
#include "util/execpath.h"
#include "util/diagnostic.h"
#include "util/file.h"
#include "wakefiles.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>

EM_JS(char *, nodejs_getfiles, (const char *dir, int *ok), {
  const Path = require("path").posix;
  const FS   = require("fs");
  let files  = [];

  function walkTree(dir) {
      FS.readdirSync(dir, {withFileTypes: true}).forEach(dirent => {
          const absolute = Path.join(dir, dirent.name);
          if (dirent.isDirectory()) {
            if (dirent.name != ".build" && dirent.name != ".fuse" && dirent.name != ".git")
              walkTree(absolute);
          } else {
            files.push(absolute);
          }
      });
  }

  try {
    walkTree(UTF8ToString(dir));
    const sep = String.fromCharCode(0);
    const out = files.join(sep) + sep;

    const lengthBytes = lengthBytesUTF8(out)+1;
    const stringOnWasmHeap = _malloc(lengthBytes);
    stringToUTF8(out, stringOnWasmHeap, lengthBytes);
    setValue(ok, 1, "i32");
    return stringOnWasmHeap;
  } catch (err) {
    const lengthBytes = lengthBytesUTF8(err.message)+1;
    const stringOnWasmHeap = _malloc(lengthBytes);
    stringToUTF8(err.message, stringOnWasmHeap, lengthBytes);
    setValue(ok, 0, "i32");
    return stringOnWasmHeap;
  }
});

bool push_files(std::vector<std::string> &out, const std::string &path, const re2::RE2& re, size_t skip) {
  int ok;
  char *files = nodejs_getfiles(path.c_str(), &ok);
  if (ok) {
    for (char *file = files; *file; file += strlen(file)+1) {
      re2::StringPiece p(file + skip, strlen(file) - skip);
      if (RE2::FullMatch(p, re)) {
        out.emplace_back(file);
      }
    }
    free(files);
    return false;
  } else {
    fputs(files, stderr);
    fputs("\n", stderr);
    free(files);
    return true;
  }
}

#else

static bool push_files(std::vector<std::string> &out, const std::string &path, int dirfd, const RE2 &re, size_t skip) {
  auto dir = fdopendir(dirfd);
  if (!dir) {
    close(dirfd);
    fprintf(stderr, "Failed to fdopendirat %s: %s\n", path.c_str(), strerror(errno));
    return true;
  }

  struct dirent *f;
  bool failed = false;
  for (errno = 0; 0 != (f = readdir(dir)); errno = 0) {
    if (f->d_name[0] == '.' && (f->d_name[1] == 0 || (f->d_name[1] == '.' && f->d_name[2] == 0))) continue;
    bool recurse;
    struct stat sbuf;
#if defined(DT_DIR)
    if (f->d_type != DT_UNKNOWN) {
      recurse = f->d_type == DT_DIR;
    } else {
#endif
      if (fstatat(dirfd, f->d_name, &sbuf, AT_SYMLINK_NOFOLLOW) != 0) {
        fprintf(stderr, "Failed to fstatat %s/%s: %s\n", path.c_str(), f->d_name, strerror(errno));
        failed = true;
        recurse = false;
      } else {
        recurse = S_ISDIR(sbuf.st_mode);
      }
#if defined(DT_DIR)
    }
#endif
    std::string name(path == "." ? f->d_name : (path + "/" + f->d_name));
    if (recurse) {
      if (name == ".build" || name == ".fuse" || name == ".git") continue;
      int fd = openat(dirfd, f->d_name, O_RDONLY);
      if (fd == -1) {
        fprintf(stderr, "Failed to openat %s/%s: %s\n", path.c_str(), f->d_name, strerror(errno));
        failed = true;
      } else {
        if (push_files(out, name, fd, re, skip))
          failed = true;
      }
    } else {
      re2::StringPiece p(name.c_str() + skip, name.size() - skip);
      if (RE2::FullMatch(p, re))
        out.emplace_back(std::move(name));
    }
  }

  if (errno != 0 && !failed) {
    fprintf(stderr, "Failed to readdir %s: %s\n", path.c_str(), strerror(errno));
    failed = true;
  }

  if (closedir(dir) != 0) {
    fprintf(stderr, "Failed to closedir %s: %s\n", path.c_str(), strerror(errno));
    failed = true;
  }

  return failed;
}

bool push_files(std::vector<std::string> &out, const std::string &path, const RE2& re, size_t skip) {
  int flags, dirfd = open(path.c_str(), O_RDONLY);
  if ((flags = fcntl(dirfd, F_GETFD, 0)) != -1)
    fcntl(dirfd, F_SETFD, flags | FD_CLOEXEC);
  return dirfd == -1 || push_files(out, path, dirfd, re, skip);
}
#endif

// . => ., hax/ => hax, foo/.././bar.z => bar.z, foo/../../bar.z => ../bar.z
std::string make_canonical(const std::string &x) {
  bool abs = x[0] == '/';

  std::stringstream str;
  if (abs) str << "/";

  std::vector<std::string> tokens;

  size_t tok = 0;
  size_t scan = 0;
  bool repeat;
  bool pop = false;
  do {
    if (is_windows()) {
      scan = x.find_first_of("\\/", tok);
    } else {
      scan = x.find_first_of('/', tok);
    }
    repeat = scan != std::string::npos;
    std::string token(x, tok, repeat?(scan-tok):scan);
    tok = scan+1;
    if (token == "..") {
      if (!tokens.empty()) {
        tokens.pop_back();
      } else if (!abs) {
        str << "../";
        pop = true;
      }
    } else if (!token.empty() && token != ".") {
      tokens.emplace_back(std::move(token));
    }
  } while (repeat);

  if (tokens.empty()) {
    if (abs) {
      return "/";
    } else {
      if (pop) {
        std::string out = str.str();
        out.resize(out.size()-1);
        return out;
      } else {
        return ".";
      }
    }
  } else {
    str << tokens.front();
    for (auto i = tokens.begin() + 1; i != tokens.end(); ++i)
      str << "/" << *i;
    return str.str();
  }
}

struct WakeFilter {
    size_t prefix;
    bool allow;
    std::unique_ptr<RE2> exp;
    WakeFilter(size_t prefix_, bool allow_, const std::string &exp, const RE2::Options &opts)
    : prefix(prefix_), allow(allow_), exp(new RE2(exp, opts)) { }
};

std::string glob2regexp(const std::string &glob) {
  std::string exp("(?s)");
  size_t s = 0, e;
  while ((e = glob.find_first_of("\\[?*", s)) != std::string::npos) {
    re2::StringPiece piece(glob.c_str() + s, e - s);
    exp.append(RE2::QuoteMeta(piece));
    if (glob[e] == '\\') {
      // Trailing \ is left as a raw '\'
      // Don't bother escaping a codepoint start
      if (e+1 == glob.size() || static_cast<unsigned char>(glob[e+1]) >= 0x80) {
        s = e + 1;
      } else {
        re2::StringPiece piece(glob.c_str() + e + 1, 1);
        exp.append(RE2::QuoteMeta(piece));
        s = e + 2;
      }
    } else if (glob[e] == '[') {
      // To include ']' in a clause, use: []abc]
      // If no closing ']' is found, just stop processing the glob
      size_t c = glob.find_first_of("]", e+2);
      if (c == std::string::npos) { s = e; break; }
      exp.push_back('[');
      size_t is = e+1, ie;
      while ((ie = glob.find_first_of("\\[", is)) != std::string::npos) {
        exp.append(glob, is, ie - is);
        exp.push_back('\\');
        exp.push_back(glob[ie]);
        is = ie+1;
      }
      exp.append(glob, is, c - is);
      exp.push_back(']');
      s = c+1;
    } else if (glob[e] == '?') {
      // ? can match any non-/ character
      exp.append("[^/]");
      s = e+1;
    } else if (e > 0 && glob.size() == e+2 && glob[e-1] == '/' && glob[e+1] == '*') {
      // trailing /** -> match everything inside
      exp.append(".+");
      s = e+2;
    } else if (e == 0 && glob.size() > 2 && glob[1] == '*' && glob[2] == '/') {
      // leading **/ -> any subdirectory
      exp.append("([^/]*/)*");
      s = e+3;
    } else if (e > 0 && glob.size() > e+2 && glob[e-1] == '/' && glob[e+1] == '*' && glob[e+2] == '/') {
      // /**/ somewhere in the string
      exp.append("([^/]*/)*");
      s = e+3;
    } else {
      // just a normal * -> match any number of non-/ characters
      exp.append("[^/]*");
      s = e+1;
    }
  }
  exp.append(RE2::QuoteMeta(glob.c_str() + s));
  return exp;
}

class DiagnosticIgnorer : public DiagnosticReporter {
  void report(Diagnostic diagnostic) { }
};

static void process_ignorefile(const std::string &path, std::vector<WakeFilter> &filters) {
  DiagnosticIgnorer ignorer;
  std::string wakeignore = path + ".wakeignore";
  ExternalFile file(ignorer, wakeignore.c_str());
  StringSegment segment = file.segment();
  std::stringstream in;
  in.write(reinterpret_cast<const char*>(segment.start), segment.size());

  RE2::Options options;
  options.set_log_errors(false);
  options.set_one_line(true);

  std::string line;
  while (std::getline(in, line)) {
    // strip trailing whitespace (including windows CR)
    size_t found = line.find_last_not_of(" \t\f\v\n\r");
    if (found != std::string::npos) {
      line.erase(found+1);
    } else {
      line.clear(); // entirely whitespace
    }
    // allow empty lines and comments
    if (line == "" || line[0] == '#') continue;
    // check for negation
    bool allow = line[0] == '!';
    if (allow) line.erase(0, 1);
    // create a regular expression for .gitignore style syntax
    filters.emplace_back(path.size(), allow, glob2regexp(line), options);
  }
}

static std::vector<std::string> filter_wakefiles(std::vector<std::string> &&wakefiles, const std::string &basedir, bool verbose) {
  std::string curdir = basedir; // Either "" or ".+/"
  if (curdir == ".") {
    curdir.clear();
  } else if (!curdir.empty() && curdir.back() != '/') {
    curdir.push_back('/');
  }

  std::vector<WakeFilter> filters;
  std::vector<std::string> output;
  output.reserve(wakefiles.size());

  process_ignorefile(curdir, filters);

  for (auto &wakefile : wakefiles) {
    // Unwind curdir
    while (!curdir.empty() && wakefile.compare(0, curdir.size(), curdir) != 0) {
      size_t slash = curdir.find_last_of('/', curdir.size()-2);
      if (slash == std::string::npos) {
        curdir.clear();
      } else {
        curdir.erase(slash+1);
      }
    }

    // Expire any patterns from directories we've left
    while (!filters.empty() && filters.back().prefix > curdir.size())
      filters.pop_back();

    // Descend into directory
    size_t slash;
    while ((slash = wakefile.find_first_of('/', curdir.size())) != std::string::npos) {
      curdir.append(wakefile, curdir.size(), slash+1 - curdir.size());
      process_ignorefile(curdir, filters);
    }

    // See if any rules exclude this file
    bool skip = false;
    size_t prefix = std::string::npos;
    for (auto &filter : filters) {
      re2::StringPiece piece(wakefile.c_str() + filter.prefix, wakefile.size() - filter.prefix);
      if (skip == filter.allow && RE2::FullMatch(piece, *filter.exp)) {
        skip = !filter.allow;
        prefix = filter.prefix;
      }
    }

    if (skip) {
      if (verbose) {
        fprintf(stderr, "Skipping %s due to %s.wakeignore\n",
                wakefile.c_str(), wakefile.substr(0, prefix).c_str());
      }
    } else {
      output.emplace_back(std::move(wakefile));
    }
  }

  return output;
}

std::vector<std::string> find_all_wakefiles(bool &ok, bool workspace, bool verbose, const std::string &libdir, const std::string &workdir) {
  RE2::Options options;
  options.set_log_errors(false);
  options.set_one_line(true);
  RE2 exp("(?s).*[^/]\\.wake", options);

  std::vector<std::string> libfiles, workfiles;

  std::string boolean = workdir + "/share/wake/lib/core/boolean.wake";
  if (!workspace || !is_readable(boolean.c_str())) {
    if (push_files(libfiles, libdir, exp, 0)) ok = false;
    std::sort(libfiles.begin(), libfiles.end());
    libfiles = filter_wakefiles(std::move(libfiles), libdir, verbose);
  }

  if (workspace) {
    if (push_files(workfiles, workdir, exp, 0)) ok = false;
    std::sort(workfiles.begin(), workfiles.end());
    workfiles = filter_wakefiles(std::move(workfiles), workdir, verbose);
  }

  // Combine the two sorted vectors into one sorted vector
  std::vector<std::string> output;
  output.reserve(libfiles.size() + workfiles.size());
  std::merge(
    std::make_move_iterator(libfiles.begin()),
    std::make_move_iterator(libfiles.end()),
    std::make_move_iterator(workfiles.begin()),
    std::make_move_iterator(workfiles.end()),
    std::back_inserter(output));

  // Eliminate any files present in both vectors
  auto it = std::unique(output.begin(), output.end());
  output.resize(std::distance(output.begin(), it));

  return output;
}
