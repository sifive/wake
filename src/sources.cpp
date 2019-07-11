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
#include "value.h"
#include "heap.h"
#include "execpath.h"

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
  std::string workspace = get_workspace();
  prefix.assign(cwd.begin() + workspace.size(), cwd.end());
  if (!prefix.empty())
    std::rotate(prefix.begin(), prefix.begin()+1, prefix.end());
  return attempts != 0;
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
    execlp("git", "git", "ls-files", "-z", 0);
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

static bool scan(std::vector<std::shared_ptr<String> > &out, const std::string &path, int dirfd) {
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
          out.emplace_back(std::make_shared<String>(prefix + tok));
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

static bool scan(std::vector<std::shared_ptr<String> > &out) {
  int flags, dirfd = open(".", O_RDONLY);
  if ((flags = fcntl(dirfd, F_GETFD, 0)) != -1)
    fcntl(dirfd, F_SETFD, flags | FD_CLOEXEC);
  return dirfd == -1 || scan(out, ".", dirfd);
}

static bool push_files(std::vector<std::shared_ptr<String> > &out, const std::string &path, int dirfd) {
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
        failed = push_files(out, name, fd);
      }
    } else {
      out.emplace_back(std::make_shared<String>(name));
    }
  }

  failed = errno         != 0 || failed;
  failed = closedir(dir) != 0 || failed;
  return failed;
}

static bool push_files(std::vector<std::shared_ptr<String> > &out, const std::string &path) {
  int flags, dirfd = open(path.c_str(), O_RDONLY);
  if ((flags = fcntl(dirfd, F_GETFD, 0)) != -1)
    fcntl(dirfd, F_SETFD, flags | FD_CLOEXEC);
  return dirfd == -1 || push_files(out, path, dirfd);
}

static bool str_lexical(const std::shared_ptr<String> &a, const std::shared_ptr<String> &b) {
  return a->value < b->value;
}

static bool str_equal(const std::shared_ptr<String> &a, const std::shared_ptr<String> &b) {
  return a->value == b->value;
}

static void distinct(std::vector<std::shared_ptr<String> > &sources) {
  std::sort(sources.begin(), sources.end(), str_lexical);
  auto it = std::unique(sources.begin(), sources.end(), str_equal);
  sources.resize(std::distance(sources.begin(), it));
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

std::string get_workspace() {
  static std::string cwd;
  if (cwd.empty()) cwd = get_cwd();
  return cwd;
}

std::vector<std::shared_ptr<String> > find_all_sources(bool &ok, bool workspace) {
  std::vector<std::shared_ptr<String> > out;
  if (workspace) ok = ok && !scan(out);
  return out;
}

std::vector<std::string> find_all_wakefiles(bool &ok, bool workspace) {
  std::vector<std::shared_ptr<String> > acc;
  std::string abs_libdir = find_execpath() + "/../share/wake/lib";
  std::string rel_libdir = make_relative(get_workspace(), make_canonical(abs_libdir));
  ok = ok && !push_files(acc, rel_libdir);
  if (workspace) ok = ok && !push_files(acc, ".");
  distinct(acc);

  RE2::Options options;
  options.set_log_errors(false);
  options.set_one_line(true);
  RE2 exp("(?s).*[^/]\\.wake", options);

  std::vector<std::string> out;
  for (auto &i : acc) if (RE2::FullMatch(i->value, exp)) out.push_back(std::move(i->value));
  return out;
}

static std::vector<std::shared_ptr<String> > sources(const std::vector<std::shared_ptr<String> > &all, const std::string &base, const RE2 &exp) {
  std::vector<std::shared_ptr<String> > out;

  if (base == ".") {
    for (auto &i : all) if (RE2::FullMatch(i->value, exp)) out.push_back(i);
  } else {
    long skip = base.size() + 1;
    auto prefixL = std::make_shared<String>(base + "/");
    auto prefixH = std::make_shared<String>(base + "0"); // '/' + 1 = '0'
    auto low  = std::lower_bound(all.begin(), all.end(), prefixL, str_lexical);
    auto high = std::lower_bound(all.begin(), all.end(), prefixH, str_lexical);
    for (auto i = low; i != high; ++i) {
      re2::StringPiece piece((*i)->value.data() + skip, (*i)->value.size() - skip);
      if (RE2::FullMatch(piece, exp)) out.push_back(*i);
    }
  }

  return out;
}

std::vector<std::shared_ptr<String> > sources(const std::vector<std::shared_ptr<String> > &all, const std::string &base, const std::string &regexp) {
  RE2::Options options;
  options.set_log_errors(false);
  options.set_one_line(true);
  RE2 exp("(?s)" + regexp, options);
  return sources(all, base, exp);
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

static PRIMFN(prim_sources) {
  EXPECT(2);
  STRING(arg0, 0);
  REGEXP(arg1, 1);

  std::string root = make_canonical(arg0->value);

  std::vector<std::shared_ptr<String> > *all = reinterpret_cast<std::vector<std::shared_ptr<String> >*>(data);
  auto match = sources(*all, root, arg1->exp);

  std::vector<std::shared_ptr<Value> > downcast;
  downcast.reserve(match.size());
  for (auto &i : match) downcast.emplace_back(std::move(i));

  auto out = make_list(std::move(downcast));
  RETURN(out);
}

static PRIMFN(prim_files) {
  EXPECT(2);
  STRING(arg0, 0);
  REGEXP(arg1, 1);

  std::string root = make_canonical(arg0->value);

  std::vector<std::shared_ptr<String> > files;
  bool fail = push_files(files, root);

  std::vector<std::shared_ptr<Value> > downcast;
  if (!fail) {
    auto match = sources(files, root, arg1->exp);
    downcast.reserve(match.size());
    for (auto &i : match) downcast.emplace_back(std::move(i));
  }

  auto out = make_list(std::move(downcast));
  RETURN(out);
}

static PRIMTYPE(type_add_sources) {
  return args.size() == 1 &&
    args[0]->unify(String::typeVar) &&
    out->unify(Data::typeUnit);
}

static PRIMFN(prim_add_sources) {
  EXPECT(1);
  STRING(arg0, 0);

  std::vector<std::shared_ptr<String> > *all = reinterpret_cast<std::vector<std::shared_ptr<String> >*>(data);

  const char *tok = arg0->value.data();
  const char *end = tok + arg0->value.size();
  for (const char *scan = tok; scan != end; ++scan) {
    if (*scan == 0 && scan != tok) {
      std::string name(tok);
      all->emplace_back(std::make_shared<String>(make_canonical(name)));
      tok = scan+1;
    }
  }

  distinct(*all);
  auto out = make_unit();
  RETURN(out);
}

static PRIMTYPE(type_simplify) {
  return args.size() == 1 &&
    args[0]->unify(String::typeVar) &&
    out->unify(String::typeVar);
}

static PRIMFN(prim_simplify) {
  EXPECT(1);
  STRING(arg0, 0);

  auto out = std::make_shared<String>(make_canonical(arg0->value));
  RETURN(out);
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

  auto out = std::make_shared<String>(make_relative(
    make_canonical(dir->value),
    make_canonical(path->value)));
  RETURN(out);
}

static PRIMTYPE(type_execpath) {
  return args.size() == 0 &&
    out->unify(String::typeVar);
}

static PRIMFN(prim_execpath) {
  EXPECT(0);
  auto out = std::make_shared<String>(find_execpath());
  RETURN(out);
}

static PRIMTYPE(type_workspace) {
  return args.size() == 0 &&
    out->unify(String::typeVar);
}

static PRIMFN(prim_workspace) {
  EXPECT(0);
  auto out = std::make_shared<String>(get_workspace());
  RETURN(out);
}

static PRIMTYPE(type_pid) {
  return args.size() == 0 &&
    out->unify(Integer::typeVar);
}

static PRIMFN(prim_pid) {
  EXPECT(0);
  auto out = std::make_shared<Integer>(getpid());
  RETURN(out);
}

void prim_register_sources(std::vector<std::shared_ptr<String> > *sources, PrimMap &pmap) {
  // Re-ordering of sources/files would break their behaviour, so they are not pure.
  prim_register(pmap, "sources",     prim_sources,     type_sources,     PRIM_SHALLOW, sources);
  prim_register(pmap, "add_sources", prim_add_sources, type_add_sources, PRIM_SHALLOW, sources);
  prim_register(pmap, "files",       prim_files,       type_sources,     PRIM_SHALLOW);
  prim_register(pmap, "simplify",    prim_simplify,    type_simplify,    PRIM_PURE|PRIM_SHALLOW);
  prim_register(pmap, "relative",    prim_relative,    type_relative,    PRIM_PURE|PRIM_SHALLOW);
  prim_register(pmap, "execpath",    prim_execpath,    type_execpath,    PRIM_PURE|PRIM_SHALLOW);
  prim_register(pmap, "workspace",   prim_workspace,   type_workspace,   PRIM_PURE|PRIM_SHALLOW);
  prim_register(pmap, "pid",         prim_pid,         type_pid,         PRIM_PURE|PRIM_SHALLOW);
}
