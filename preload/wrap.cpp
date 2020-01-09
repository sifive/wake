/* Wake runner to capture inputs/outputs using an ELF preload
 *
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

#include <algorithm>
#include <string>
#include <set>
#include <vector>
#include <iostream>
#include <fstream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <unistd.h>
#include <dirent.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include "json5.h"
#include "execpath.h"
#include "unlink.h"

#define STR2(x) #x
#define STR(x) STR2(x)

typedef std::set<std::string> sset;
typedef std::vector<std::string> svec;

static std::string makeGuard(const std::string &file) {
  size_t slash = file.find_last_of('/');
  if (slash == std::string::npos) slash = 0; else ++slash;
  return file.substr(0, slash) + ".guard-" + file.substr(slash);
}

static void make_shadow_tree(const std::string &root, const JAST &jast, sset &visible, sset &guards) {
  std::string roots = root + "/";

  for (auto &x : jast.get("visible").children) {
    const std::string &file = x.second.value;

    // absolute paths do need to made visible
    if (file.empty() || file[0] == '/') continue;
    // the workspace root also not
    if (file == ".") continue;

    for (size_t pos = file.find('/'); pos != std::string::npos; pos = file.find('/', pos+1)) {
      std::string dir = file.substr(0, pos);
      if (visible.insert(dir + "/").second) {
        std::string target = roots + dir;
        if (mkdir(target.c_str(), 0775)  != 0) {
          std::cerr << "mkdir " << target << ": " << strerror(errno) << std::endl;
          exit(1);
        }
      }
    }

    struct stat s;
    if (stat(file.c_str(), &s) != 0) {
      std::cerr << "stat " << file << ": " << strerror(errno) << std::endl;
      exit(1);
    }

    std::string target = roots + file;
    if (s.st_mode & S_IFDIR) {
      if (visible.insert(file + "/").second) {
        if (mkdir(target.c_str(), 0775) != 0) {
          std::cerr << "mkdir " << target << ": " << strerror(errno) << std::endl;
          exit(1);
        }
      }
    } else {
      if (visible.insert(file).second) {
        if (link(file.c_str(), target.c_str()) != 0) {
          std::cerr << "link " << target << ": " << strerror(errno) << std::endl;
          exit(1);
        }
        std::string guard = makeGuard(file);
        std::string target_guard = roots + guard;
        int fd = open(target_guard.c_str(), O_CREAT|O_EXCL, 0664);
        if (fd == -1) {
          std::cerr << "open " << target_guard << ": " << strerror(errno) << std::endl;
          exit(1);
        }
        close(fd);
        guards.insert(guard);
      }
    }
  }
}

static void scan_shadow_tree(sset &exist, const std::string &path, int dirfd) {
  auto dir = fdopendir(dirfd);
  if (!dir) {
    std::cerr << "fdopendir " << path << ": " << strerror(errno) << std::endl;
    close(dirfd);
    exit(1);
  }

  struct dirent *f;
  for (errno = 0; (f = readdir(dir)); errno = 0) {
    // Skip . and ..
    if (f->d_name[0] == '.' && (f->d_name[1] == 0 || (f->d_name[1] == '.' && f->d_name[2] == 0))) continue;

    std::string name(path + f->d_name);
    bool recurse;

#ifdef DT_DIR
    if (f->d_type != DT_UNKNOWN) {
      recurse = f->d_type == DT_DIR;
    } else {
#endif
      struct stat sbuf;
      if (fstatat(dirfd, f->d_name, &sbuf, AT_SYMLINK_NOFOLLOW) != 0) {
        std::cerr << "fstatat " << name << ": " << strerror(errno) << std::endl;
        exit(1);
      } else {
        recurse = S_ISDIR(sbuf.st_mode);
      }
#ifdef DT_DIR
    }
#endif

    if (recurse) {
      int fd = openat(dirfd, f->d_name, O_RDONLY);
      if (fd == -1) {
        std::cerr << "openat " << name << ": " << strerror(errno) << std::endl;
        exit(1);
      } else {
        name += "/";
        scan_shadow_tree(exist, name, fd);
      }
    }

    exist.insert(std::move(name));
  }

  if (errno != 0) {
    std::cerr << "readdir " << path << ": " << strerror(errno) << std::endl;
    exit(1);
  }

  if (closedir(dir) != 0) {
    std::cerr << "closedir " << path << ": " << strerror(errno) << std::endl;
    exit(1);
  }
}

static void scan_shadow_tree(sset &exist, const std::string &path) {
  int dirfd = open(path.c_str(), O_RDONLY);
  scan_shadow_tree(exist, "", dirfd);
}

#ifdef __APPLE__
#define st_mtim st_mtimespec
#endif

static void compute_inout(const sset &exist, const sset &guards, const sset &visible, const struct timeval &start, svec &inputs, svec &outputs) {
  // First, compute exist - guards
  svec emg;
  auto e = exist.begin(), g = guards.begin();
  while (e != exist.end() && g != guards.end()) {
    int comp = e->compare(*g);
    if      (comp < 0) emg.emplace_back(*e++);
    else if (comp > 0) ++g;
    else { ++e; ++g; }
  }
  while (e != exist.end()) emg.emplace_back(*e++);

  // Next, consider the set relationships between emg and visible
  auto m = emg.begin();
  auto v = visible.begin();
  while (m != emg.end() && v != visible.end()) {
    int comp = m->compare(*v);
    if (comp < 0) {
      outputs.emplace_back(std::move(*m));
      ++m;
    } else if (comp > 0) {
      std::cerr << "Visible file was deleted: " << *v << std::endl;
      exit(1);
    } else if (m->back() != '/') {
      struct stat sbuf;
      if (stat(m->c_str(), &sbuf) != 0) {
        std::cerr << "stat " << *m << ": " << strerror(errno) << std::endl;
        exit(1);
      }
      if (sbuf.st_mtim.tv_sec > start.tv_sec ||
          (sbuf.st_mtim.tv_sec == start.tv_sec &&
           sbuf.st_mtim.tv_nsec > start.tv_usec*1000)) {
        outputs.emplace_back(std::move(*m));
      } else if (exist.find(makeGuard(*m)) == exist.end()) {
        inputs.emplace_back(std::move(*m));
      }
      ++m;
      ++v;
    } else {
      ++m;
      ++v;
    }
  }
  for (; m != emg.end(); ++m) outputs.emplace_back(std::move(*m));
  if (v != visible.end()) {
    std::cerr << "Visible file was deleted: " << *v << std::endl;
    exit(1);
  }
}

static void relink_shadow_tree(const std::string &root, const svec &outputs) {
  std::string roots = root + "/";
  for (auto &x : outputs) {
    if (x.back() == '/') {
      std::string dir(x, 0, x.size()-1);
      std::string target = roots + dir;
      if (mkdir(dir.c_str(), 0775) != 0 && errno != EEXIST) {
        std::cerr << "mkdir " << dir << ": " << strerror(errno) << std::endl;
        exit(1);
      }
      struct stat sbuf;
      if (stat(target.c_str(), &sbuf) != 0) {
        std::cerr << "stat " << target << ": " << strerror(errno) << std::endl;
        exit(1);
      }
      if (chmod(dir.c_str(), sbuf.st_mode) != 0) {
        std::cerr << "chmod " << dir << ": " << strerror(errno) << std::endl;
        exit(1);
      }
    } else {
      std::string target = roots + x;
      unlink(x.c_str());
      if (link(target.c_str(), x.c_str()) != 0) {
        bool die = errno != EEXIST;
        std::cerr << "link " << x << ": " << strerror(errno) << std::endl;
        if (die) exit(1);
      }
    }
  }
}

int main(int argc, const char **argv) {
  JAST jast;

  if (argc != 3) {
    std::cerr << "Syntax: preload-wake <input-json> <output-json>" << std::endl;
    exit(1);
  }

  if (!JAST::parse(argv[1], std::cerr, jast)) {
    exit(1);
  }

  mkdir(".build", 0775);
  std::string root = ".build/" + std::to_string(getpid());
  if (mkdir(root.c_str(), 0775) != 0) {
    std::cerr << "mkdir " << root << ": " << strerror(errno) << std::endl;
    exit(1);
  }

  sset visible, guards;
  make_shadow_tree(root, jast, visible, guards);

  // Prepare the subcommand inputs
  std::vector<char *> arg, env;
  std::string preload = STR(ENV) "=" + find_execpath() + "/libpreload-wake." STR(EXT);
  env.push_back(const_cast<char*>(preload.c_str()));

  for (auto &x : jast.get("command").children)
    arg.push_back(const_cast<char*>(x.second.value.c_str()));
  arg.push_back(0);
  for (auto &x : jast.get("environment").children)
    env.push_back(const_cast<char*>(x.second.value.c_str()));
  env.push_back(0);

  std::string dir = root + "/" + jast.get("directory").value;
  std::string stdin = jast.get("stdin").value;
  if (stdin.empty()) stdin = "/dev/null";

  struct timeval start;
  gettimeofday(&start, 0);

  pid_t pid = fork();
  if (pid == 0) {
    if (chdir(dir.c_str()) != 0) {
      std::cerr << "chdir " << dir << ": " << strerror(errno) << std::endl;
      exit(1);
    }
    int fd = open(stdin.c_str(), O_RDONLY);
    if (fd == -1) {
      std::cerr << "open " << stdin << ":" << strerror(errno) << std::endl;
      exit(1);
    }
    if (fd != 0) {
      dup2(fd, 0);
      close(fd);
    }

    std::string command = find_in_path(arg[0], find_path(env.data()));
    unlink(makeGuard(command).c_str());
    execve(command.c_str(), arg.data(), env.data());
    std::cerr << "execve " << command << ": " << strerror(errno) << std::endl;
    exit(1);
  }

  int status;
  struct rusage rusage;
  do wait4(pid, &status, 0, &rusage);
  while (WIFSTOPPED(status));

  if (WIFEXITED(status)) {
    status = WEXITSTATUS(status);
  } else {
    status = -WTERMSIG(status);
  }

  struct timeval stop;
  gettimeofday(&stop, 0);

  sset exist;
  svec inputs, outputs;

  scan_shadow_tree(exist, root);
  compute_inout(exist, guards, visible, start, inputs, outputs);
  relink_shadow_tree(root, outputs);
  deep_unlink(AT_FDCWD, root.c_str());
  
  std::ofstream out(argv[2], std::ios_base::trunc);
  if (out.fail()) {
    std::cerr << "ofstream " << argv[2] << ": " << strerror(errno) << std::endl;
    exit(1);
  }

  bool first;
  out << "{\"usage\":{\"status\":" << status
    << ",\"runtime\":" << (stop.tv_sec - start.tv_sec + (stop.tv_usec - start.tv_usec)/1000000.0)
    << ",\"cputime\":" << (rusage.ru_utime.tv_sec + rusage.ru_stime.tv_sec + (rusage.ru_utime.tv_usec + rusage.ru_stime.tv_usec)/1000000.0)
    << ",\"membytes\":" << rusage.ru_maxrss
    << ",\"inbytes\":" << rusage.ru_inblock * UINT64_C(512)
    << ",\"outbytes\":" << rusage.ru_oublock * UINT64_C(512)
    << "},\"inputs\":[";

  first = true; 
  for (auto &x : inputs) {
    out << (first?"":",") << "\"" << json_escape(x) << "\"";
    first = false;
  }

  out << "],\"outputs\":[";

  first = true; 
  for (auto &x : outputs) {
    out << (first?"":",") << "\"" << json_escape(x) << "\"";
    first = false;
  }

  out << "]}" << std::endl;

  if (out.bad()) {
    std::cerr << "bad " << argv[2] << ": " << strerror(errno) << std::endl;
    exit(1);
  }

  return 0;
}
