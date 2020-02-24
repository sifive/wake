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

#include "sources.h"
#include "prim.h"
#include "primfn.h"
#include "type.h"
#include "value.h"
#include "execpath.h"
#include "datatype.h"

#include <re2/re2.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <cstring>
#include <sstream>
#include <fstream>
#include <iostream>
#include <algorithm>

bool make_workspace(const std::string &dir) {
  if (chdir(dir.c_str()) != 0) return false;
  int perm = S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH;
  int fd = open("wake.db", O_RDWR|O_CREAT|O_TRUNC, perm);
  if (fd == -1) return false;
  close(fd);
  return true;
}

bool chdir_workspace(std::string &prefix) {
  int attempts;
  std::string cwd = get_cwd();
  for (attempts = 100; attempts && access("wake.db", F_OK) == -1; --attempts) {
    if (chdir("..") == -1) return false;
  }
  if (attempts == 0) return false;
  std::string workspace = get_cwd();
  if (cwd.substr(0, workspace.size()) != workspace) {
    fprintf(stderr, "Workspace directory is not a parent of current directory\n");
    return false;
  }
  prefix.assign(cwd.begin() + workspace.size(), cwd.end());
  if (!prefix.empty())
    std::rotate(prefix.begin(), prefix.begin()+1, prefix.end());
  return true;
}

static std::string slurp(int dirfd, bool &fail) {
  std::stringstream str;
  char buf[4096];
  int got, status, pipefd[2];
  pid_t pid;

  if (pipe(pipefd) == -1) {
    fail = true;
    fprintf(stderr, "Failed to open pipe %s\n", strerror(errno));
  } else if ((pid = fork()) == 0) {
    if (fchdir(dirfd) == -1) {
      fprintf(stderr, "Failed to chdir: %s\n", strerror(errno));
      exit(1);
    }
    close(pipefd[0]);
    if (pipefd[1] != 1) {
      dup2(pipefd[1], 1);
      close(pipefd[1]);
    }
    execlp("git", "git", "ls-files", "-z", nullptr);
    fprintf(stderr, "Failed to execlp(git): %s\n", strerror(errno));
    exit(1);
  } else {
    close(pipefd[1]);
    while ((got = read(pipefd[0], buf, sizeof(buf))) > 0) str.write(buf, got);
    if (got == -1) {
      fprintf(stderr, "Failed to read from git: %s\n", strerror(errno));
      fail = true;
    }
    close(pipefd[0]);
    while (waitpid(pid, &status, 0) != pid) { }
    if (WIFSIGNALED(status)) {
      fprintf(stderr, "Failed to reap git: killed by %d\n", WTERMSIG(status));
      fail = true;
    } else if (WEXITSTATUS(status)) {
      fprintf(stderr, "Failed to reap git: exited with %d\n", WEXITSTATUS(status));
      fail = true;
    }
  }
  return str.str();
}

static bool scan(std::vector<std::string> &out, const std::string &path, int dirfd) {
  auto dir = fdopendir(dirfd);
  if (!dir) {
    fprintf(stderr, "Failed to fdopendir %s: %s\n", path.c_str(), strerror(errno));
    close(dirfd);
    return true;
  }

  struct dirent *f;
  bool failed = false;
  for (errno = 0; !failed && (f = readdir(dir)); errno = 0) {
    if (f->d_name[0] == '.' && (f->d_name[1] == 0 || (f->d_name[1] == '.' && f->d_name[2] == 0))) continue;
    if (!strcmp(f->d_name, ".git")) {
      std::string files(slurp(dirfd, failed));
      std::string prefix(path == "." ? "" : (path + "/"));
      const char *tok = files.data();
      const char *end = tok + files.size();
      for (const char *scan = tok; scan != end; ++scan) {
        if (*scan == 0 && scan != tok) {
          out.emplace_back(prefix + tok);
          tok = scan+1;
        }
      }
    } else {
      bool recurse;
#ifdef DT_DIR
      if (f->d_type == DT_UNKNOWN) {
#endif
        struct stat sbuf;
        if (fstatat(dirfd, f->d_name, &sbuf, AT_SYMLINK_NOFOLLOW) != 0) {
          fprintf(stderr, "Failed to fstatat %s/%s: %s\n", path.c_str(), f->d_name, strerror(errno));
          failed = true;
          recurse = false;
        } else {
          recurse = S_ISDIR(sbuf.st_mode);
        }
#ifdef DT_DIR
      } else {
        recurse = f->d_type == DT_DIR;
      }
#endif
      if (recurse) {
        std::string name(path == "." ? f->d_name : (path + "/" + f->d_name));
        if (name == ".build" || name == ".fuse") continue;
        int fd = openat(dirfd, f->d_name, O_RDONLY);
        if (fd == -1) {
          fprintf(stderr, "Failed to openat %s/%s: %s\n", path.c_str(), f->d_name, strerror(errno));
          failed = true;
        } else {
          failed = scan(out, name, fd);
        }
      }
    }
  }

  failed = errno         != 0 || failed;
  failed = closedir(dir) != 0 || failed;
  return failed;
}

static bool scan(std::vector<std::string> &out) {
  int flags, dirfd = open(".", O_RDONLY);
  if ((flags = fcntl(dirfd, F_GETFD, 0)) != -1)
    fcntl(dirfd, F_SETFD, flags | FD_CLOEXEC);
  return dirfd == -1 || scan(out, ".", dirfd);
}

static bool push_files(std::vector<std::string> &out, const std::string &path, int dirfd, const RE2 &re, size_t skip) {
  auto dir = fdopendir(dirfd);
  if (!dir) {
    close(dirfd);
    return true;
  }

  struct dirent *f;
  bool failed = false;
  for (errno = 0; !failed && (f = readdir(dir)); errno = 0) {
    if (f->d_name[0] == '.' && (f->d_name[1] == 0 || (f->d_name[1] == '.' && f->d_name[2] == 0))) continue;
    bool recurse;
    struct stat sbuf;
#ifdef DT_DIR
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
#ifdef DT_DIR
    }
#endif
    std::string name(path == "." ? f->d_name : (path + "/" + f->d_name));
    if (recurse) {
      if (name == ".build" || name == ".fuse") continue;
      int fd = openat(dirfd, f->d_name, O_RDONLY);
      if (fd == -1) {
        failed = true;
      } else {
        failed = push_files(out, name, fd, re, skip);
      }
    } else {
      re2::StringPiece p(name.c_str() + skip, name.size() - skip);
      if (RE2::FullMatch(p, re))
        out.emplace_back(std::move(name));
    }
  }

  failed = errno         != 0 || failed;
  failed = closedir(dir) != 0 || failed;
  return failed;
}

static bool push_files(std::vector<std::string> &out, const std::string &path, const RE2& re, size_t skip) {
  int flags, dirfd = open(path.c_str(), O_RDONLY);
  if ((flags = fcntl(dirfd, F_GETFD, 0)) != -1)
    fcntl(dirfd, F_SETFD, flags | FD_CLOEXEC);
  return dirfd == -1 || push_files(out, path, dirfd, re, skip);
}

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
    scan = x.find_first_of('/', tok);
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

// dir + path must be canonical
static std::string make_relative(std::string &&dir, std::string &&path) {
  if ((!path.empty() && path[0] == '/') !=
      (!dir .empty() && dir [0] == '/')) {
    return std::move(path);
  }

  if (dir == ".") dir = ""; else dir += '/';
  path += '/';

  size_t skip = 0, end = std::min(path.size(), dir.size());
  for (size_t i = 0; i < end; ++i) {
    if (dir[i] != path[i])
      break;
    if (dir[i] == '/') skip = i+1;
  }

  std::stringstream str;
  for (size_t i = skip; i < dir.size(); ++i)
    if (dir[i] == '/')
      str << "../";

  std::string x;
  std::string last(path, skip, path.size()-skip-1);
  if (last.empty() || last == ".") {
    // remove trailing '/'
    x = str.str();
    if (x.empty()) x = ".";
    else x.resize(x.size()-1);
  } else {
    str << last;
    x = str.str();
  }

  if (x.empty()) return ".";
  return x;
}

struct WakeFilter {
  size_t prefix;
  bool allow;
  std::unique_ptr<RE2> exp;
  WakeFilter(size_t prefix_, bool allow_, const std::string &exp, const RE2::Options &opts)
   : prefix(prefix_), allow(allow_), exp(new RE2(exp, opts)) { }
};

static std::string glob2regexp(const std::string &glob) {
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

static void process_ignorefile(const std::string &path, std::vector<WakeFilter> &filters) {
  std::ifstream in(path + ".wakeignore");
  if (!in.is_open()) return;

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

static void filter_wakefiles(std::vector<std::string> &wakefiles, bool verbose) {
  std::string curdir; // Either "" or ".+/"
  std::vector<WakeFilter> filters;

  process_ignorefile(curdir, filters);

  for (auto p = wakefiles.begin(); p != wakefiles.end(); /**/) {
    std::string &wakefile = *p;

    // Wake standard library cannot be excluded
    if (wakefile.compare(0, 3, "../") == 0) { ++p; continue; }

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
      p = wakefiles.erase(p);
    } else {
      ++p;
    }
  }
}

std::vector<std::string> find_all_wakefiles(bool &ok, bool workspace, bool verbose) {
  RE2::Options options;
  options.set_log_errors(false);
  options.set_one_line(true);
  RE2 exp("(?s).*[^/]\\.wake", options);

  std::string abs_libdir = find_execpath() + "/../share/wake/lib";
  std::string rel_libdir = make_relative(get_cwd(), make_canonical(abs_libdir));

  std::vector<std::string> acc;
  ok = ok && !push_files(acc, rel_libdir, exp, 0);
  if (workspace) ok = ok && !push_files(acc, ".", exp, 0);

  // make the output distinct
  std::sort(acc.begin(), acc.end());
  auto it = std::unique(acc.begin(), acc.end());
  acc.resize(std::distance(acc.begin(), it));

  filter_wakefiles(acc, verbose);

  return acc;
}

bool find_all_sources(Runtime &runtime, bool workspace) {
  bool ok = true;
  std::vector<std::string> found;
  if (workspace) ok = !scan(found);

  size_t need = Record::reserve(found.size());
  for (auto &x : found) need += String::reserve(x.size());
  runtime.heap.guarantee(need);

  Record *out = Record::claim(runtime.heap, &Constructor::array, found.size());
  for (size_t i = 0; i < out->size(); ++i)
    out->at(i)->instant_fulfill(String::claim(runtime.heap, found[i]));

  runtime.sources = out;
  return ok;
}

static PRIMTYPE(type_sources) {
  TypeVar list;
  Data::typeList.clone(list);
  list[0].unify(String::typeVar);
  return args.size() == 2 &&
    args[0]->unify(String::typeVar) &&
    args[1]->unify(RegExp::typeVar) &&
    out->unify(list);
}

static bool promise_lexical(const Promise &a, const std::string &b) {
  return a.coerce<String>()->compare(b) < 0;
}

static PRIMFN(prim_sources) {
  EXPECT(2);
  STRING(arg0, 0);
  REGEXP(arg1, 1);

  long skip = 0;
  Promise *low  = runtime.sources->at(0);
  Promise *high = low + runtime.sources->size();

  std::string root = make_canonical(arg0->as_str());
  if (root != ".") {
    auto prefixL = root + "/";
    auto prefixH = root + "0"; // '/' + 1 = '0'
    skip = root.size() + 1;
    low  = std::lower_bound(low, high, prefixL, promise_lexical);
    high = std::lower_bound(low, high, prefixH, promise_lexical);
  }

  std::vector<Value*> found;
  for (Promise *i = low; i != high; ++i) {
    String *s = i->coerce<String>();
    re2::StringPiece piece(s->c_str() + skip, s->size() - skip);
    if (RE2::FullMatch(piece, *arg1->exp)) found.push_back(s);
  }

  runtime.heap.reserve(reserve_list(found.size()));
  RETURN(claim_list(runtime.heap, found.size(), found.data()));
}

static PRIMFN(prim_files) {
  EXPECT(2);
  STRING(arg0, 0);
  REGEXP(arg1, 1);

  std::string root = make_canonical(arg0->as_str());
  size_t skip = (root == ".") ? 0 : (root.size() + 1);

  std::vector<std::string> match;
  bool fail = push_files(match, root, *arg1->exp, skip);
  if (fail) match.clear(); // !!! There's a hole in the API

  size_t need = reserve_list(match.size());
  for (auto &x : match) need += String::reserve(x.size());
  runtime.heap.reserve(need);

  std::vector<Value*> out;
  out.reserve(match.size());
  for (auto &x : match)
    out.push_back(String::claim(runtime.heap, x));

  RETURN(claim_list(runtime.heap, out.size(), out.data()));
}

static PRIMTYPE(type_add_sources) {
  return args.size() == 1 &&
    args[0]->unify(String::typeVar) &&
    out->unify(Data::typeBoolean);
}

static bool promise_lt(const Promise &a, const Promise &b) {
  return a.coerce<String>()->compare(*b.coerce<String>()) < 0;
}

static bool promise_eq(const Promise &a, const Promise &b) {
  return a.coerce<String>()->compare(*b.coerce<String>()) == 0;
}

static PRIMFN(prim_add_sources) {
  EXPECT(1);
  STRING(arg0, 0);

  size_t copy = runtime.sources->size();
  size_t num = copy;
  size_t need = reserve_bool();
  const char *tok = arg0->c_str();
  const char *end = tok + arg0->size();
  for (const char *scan = tok; scan != end; ++scan) {
    if (*scan == 0 && scan != tok) {
      ++num;
      need += String::reserve(scan - tok);
      tok = scan+1;
    }
  }

  need += 2*Record::reserve(num);
  runtime.heap.reserve(need);
  Record *tuple = Record::claim(runtime.heap, &Constructor::array, num);

  size_t i;
  for (i = 0; i < copy; ++i)
    tuple->at(i)->instant_fulfill(runtime.sources->at(i)->coerce<HeapObject>());

  tok = arg0->c_str();
  end = tok + arg0->size();
  for (const char *scan = tok; scan != end; ++scan) {
    if (*scan == 0 && scan != tok) {
      tuple->at(i++)->instant_fulfill(String::claim(runtime.heap, tok, scan-tok));
      tok = scan+1;
    }
  }

  // make the output distinct
  Promise *low = tuple->at(0);
  Promise *high = low + tuple->size();
  std::sort(low, high, promise_lt);
  size_t keep = std::unique(low, high, promise_eq) - low;

  Record *compact = Record::claim(runtime.heap, &Constructor::array, keep);
  for (size_t j = 0; j < keep; ++j)
    compact->at(j)->instant_fulfill(tuple->at(j)->coerce<HeapObject>());

  runtime.sources = compact;
  RETURN(claim_bool(runtime.heap, true));
}

static PRIMTYPE(type_simplify) {
  return args.size() == 1 &&
    args[0]->unify(String::typeVar) &&
    out->unify(String::typeVar);
}

static PRIMFN(prim_simplify) {
  EXPECT(1);
  STRING(arg0, 0);

  RETURN(String::alloc(runtime.heap, make_canonical(arg0->as_str())));
}

static PRIMTYPE(type_relative) {
  return args.size() == 2 &&
    args[0]->unify(String::typeVar) &&
    args[1]->unify(String::typeVar) &&
    out->unify(String::typeVar);
}

static PRIMFN(prim_relative) {
  EXPECT(2);
  STRING(dir, 0);
  STRING(path, 1);

  RETURN(String::alloc(runtime.heap, make_relative(
    make_canonical(dir->as_str()),
    make_canonical(path->as_str()))));
}

static PRIMTYPE(type_execpath) {
  return args.size() == 0 &&
    out->unify(String::typeVar);
}

static PRIMFN(prim_execpath) {
  EXPECT(0);
  RETURN(String::alloc(runtime.heap, find_execpath()));
}

static PRIMTYPE(type_workspace) {
  return args.size() == 0 &&
    out->unify(String::typeVar);
}

static PRIMFN(prim_workspace) {
  EXPECT(0);
  RETURN(String::alloc(runtime.heap, get_cwd()));
}

static PRIMTYPE(type_pid) {
  return args.size() == 0 &&
    out->unify(Integer::typeVar);
}

static PRIMFN(prim_pid) {
  EXPECT(0);
  MPZ out(getpid());
  RETURN(Integer::alloc(runtime.heap, out));
}

static PRIMTYPE(type_glob2regexp) {
  return args.size() == 1 &&
    args[0]->unify(String::typeVar) &&
    out->unify(String::typeVar);
}

static PRIMFN(prim_glob2regexp) {
  EXPECT(1);
  STRING(glob, 0);

  RETURN(String::alloc(runtime.heap, glob2regexp(glob->as_str())));
}

void prim_register_sources(PrimMap &pmap) {
  // Observes the filesystem, so must be ordered
  prim_register(pmap, "files",       prim_files,       type_sources,     PRIM_ORDERED);
  // Sources do not change after wake starts
  prim_register(pmap, "add_sources", prim_add_sources, type_add_sources, PRIM_PURE);
  prim_register(pmap, "sources",     prim_sources,     type_sources,     PRIM_PURE);
  // Simple functions
  prim_register(pmap, "simplify",    prim_simplify,    type_simplify,    PRIM_PURE);
  prim_register(pmap, "relative",    prim_relative,    type_relative,    PRIM_PURE);
  prim_register(pmap, "glob2regexp", prim_glob2regexp, type_glob2regexp, PRIM_PURE);
  prim_register(pmap, "execpath",    prim_execpath,    type_execpath,    PRIM_PURE);
  prim_register(pmap, "workspace",   prim_workspace,   type_workspace,   PRIM_PURE);
  prim_register(pmap, "pid",         prim_pid,         type_pid,         PRIM_PURE);
}
