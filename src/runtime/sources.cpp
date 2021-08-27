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

#include "runtime/sources.h"
#include "runtime/prim.h"
#include "primfn.h"
#include "types/type.h"
#include "types/data.h"
#include "runtime/value.h"
#include "execpath.h"
#include "types/datatype.h"
#include "frontend/wakefiles.h"

bool make_workspace(const std::string &dir) {
  if (chdir(dir.c_str()) != 0) return false;
  int perm = S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH;
  int fd = open("wake.db", O_RDWR|O_CREAT|O_TRUNC, perm);
  if (fd == -1) {
    std::cerr << "Could not create 'wake.db' in '" << dir << "': " << strerror(errno) << std::endl;
    return false;
  }
  close(fd);
  return true;
}

bool chdir_workspace(const char *chdirto, std::string &wake_cwd, std::string &src_dir) {
  wake_cwd = get_cwd();

  if (chdirto) {
    int res = chdir(chdirto);
    if (res == -1 && errno == ENOTDIR) {
      // allow -C path-to-a-file
      std::string tail(chdirto);
      size_t eop = tail.find_last_of('/');
      if (eop != std::string::npos) {
        tail.resize(eop);
        res = chdir(tail.c_str());
      }
    }
    if (res == -1) {
      std::cerr << "Failed to change directory to '" << chdirto << "': " << strerror(errno) << std::endl;
      return false;
    }
  }

  src_dir = get_cwd();

  int attempts;
  for (attempts = 100; attempts > 0; --attempts) {
    if (access("wake.db", F_OK) != -1) break;
    if (access(".wakeroot", R_OK) != -1) {
      make_workspace(".");
      break;
    }
    if (chdir("..") == -1) return false;
  }

  // Could not find a wake database in parent directories
  if (attempts == 0) return false;

  std::string workspace = get_cwd();
  if (src_dir.compare(0, workspace.size(), workspace) != 0 ||
       (workspace.size() < src_dir.size() && src_dir[workspace.size()] != '/')) {
    fprintf(stderr, "Workspace directory is not a parent of current directory (or --chdir)\n");
    return false;
  }

  // remove prefix
  src_dir.erase(src_dir.begin(), src_dir.begin() + workspace.size());
  // move leading '/' (if any) to end:
  if (!src_dir.empty())
    std::rotate(src_dir.begin(), src_dir.begin()+1, src_dir.end());

  wake_cwd = make_relative(std::move(workspace), std::move(wake_cwd));
  // Make wake_cwd suitable for prepend to paths in main.cpp
  if (wake_cwd == ".") {
    wake_cwd = "";
  } else {
    wake_cwd += '/';
  }

  return true;
}

static std::string slurp(int dirfd, const char * const * argv, bool &fail) {
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
    execvp(argv[0], const_cast<char *const *>(argv));
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

static bool scan(std::vector<std::string> &files, std::vector<std::string> &submods, const std::string &path, int dirfd) {
  auto dir = fdopendir(dirfd);
  if (!dir) {
    fprintf(stderr, "Failed to fdopendir %s: %s\n", path.c_str(), strerror(errno));
    close(dirfd);
    return true;
  }

  struct dirent *f;
  bool failed = false;
  for (errno = 0; 0 != (f = readdir(dir)); errno = 0) {
    if (f->d_name[0] == '.' && (f->d_name[1] == 0 || (f->d_name[1] == '.' && f->d_name[2] == 0))) continue;
    if (!strcmp(f->d_name, ".git")) {
      static const char *fileArgs[] = { "git", "ls-files", "-z", nullptr };
      std::string fileStr(slurp(dirfd, &fileArgs[0], failed));
      std::string submodStr;
      if (faccessat(dirfd, ".gitmodules", R_OK, 0) == 0) {
        static const char *submodArgs[] = { "git", "config", "-f", ".gitmodules", "-z", "--get-regexp", "^submodule[.].*[.]path$", nullptr };
        submodStr = slurp(dirfd, &submodArgs[0], failed);
      }
      std::string prefix(path == "." ? "" : (path + "/"));
      const char *tok = fileStr.data();
      const char *end = tok + fileStr.size();
      for (const char *scan = tok; scan != end; ++scan) {
        if (*scan == 0 && scan != tok) {
          files.emplace_back(prefix + tok);
          tok = scan+1;
        }
      }
      tok = submodStr.data();
      end = tok + submodStr.size();
      const char *value = nullptr;
      for (const char *scan = tok; scan != end; ++scan) {
        if (*scan == '\n' && !value) value = scan+1;
        if (*scan == 0 && scan != tok && value) {
          submods.emplace_back(prefix + value);
          tok = scan+1;
          value = nullptr;
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
          if (scan(files, submods, name, fd))
            failed = true;
        }
      }
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

static bool scan(std::vector<std::string> &files, std::vector<std::string> &submods) {
  int flags, dirfd = open(".", O_RDONLY);
  if ((flags = fcntl(dirfd, F_GETFD, 0)) != -1)
    fcntl(dirfd, F_SETFD, flags | FD_CLOEXEC);
  return dirfd == -1 || scan(files, submods, ".", dirfd);
}

bool find_all_sources(Runtime &runtime, bool workspace) {
  bool ok = true;
  std::vector<std::string> files, submods, difference;
  if (workspace) ok = !scan(files, submods);

  std::sort(files.begin(), files.end());
  std::sort(submods.begin(), submods.end());

  // Compute difference = files - submodes
  difference.reserve(files.size());
  auto fi = files.begin(), si = submods.begin();
  while (fi != files.end()) {
    if (si == submods.end()) {
      difference.emplace_back(std::move(*fi++));
    } else if (*fi < *si) {
      difference.emplace_back(std::move(*fi++));
    } else {
      if (*si == *fi) ++fi;
      ++si;
    }
  }

  size_t need = Record::reserve(difference.size());
  for (auto &x : difference) need += String::reserve(x.size());
  runtime.heap.guarantee(need);

  Record *out = Record::claim(runtime.heap, &Constructor::array, difference.size());
  for (size_t i = 0; i < out->size(); ++i)
    out->at(i)->instant_fulfill(String::claim(runtime.heap, difference[i]));

  runtime.sources = out;
  return ok;
}

static PRIMTYPE(type_sources) {
  TypeVar list;
  Data::typeList.clone(list);
  list[0].unify(Data::typeString);
  return args.size() == 2 &&
    args[0]->unify(Data::typeString) &&
    args[1]->unify(Data::typeRegExp) &&
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
  (void)fail; // !!! There's a hole in the API

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
    args[0]->unify(Data::typeString) &&
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
    args[0]->unify(Data::typeString) &&
    out->unify(Data::typeString);
}

static PRIMFN(prim_simplify) {
  EXPECT(1);
  STRING(arg0, 0);

  RETURN(String::alloc(runtime.heap, make_canonical(arg0->as_str())));
}

static PRIMTYPE(type_relative) {
  return args.size() == 2 &&
    args[0]->unify(Data::typeString) &&
    args[1]->unify(Data::typeString) &&
    out->unify(Data::typeString);
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
    out->unify(Data::typeString);
}

static PRIMFN(prim_execpath) {
  EXPECT(0);
  RETURN(String::alloc(runtime.heap, find_execpath()));
}

static PRIMTYPE(type_workspace) {
  return args.size() == 0 &&
    out->unify(Data::typeString);
}

static PRIMFN(prim_workspace) {
  EXPECT(0);
  RETURN(String::alloc(runtime.heap, get_cwd()));
}

static PRIMTYPE(type_pid) {
  return args.size() == 0 &&
    out->unify(Data::typeInteger);
}

static PRIMFN(prim_pid) {
  EXPECT(0);
  MPZ out(getpid());
  RETURN(Integer::alloc(runtime.heap, out));
}

static PRIMTYPE(type_glob2regexp) {
  return args.size() == 1 &&
    args[0]->unify(Data::typeString) &&
    out->unify(Data::typeString);
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
