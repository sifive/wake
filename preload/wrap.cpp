#include <string>
#include <set>
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
#include "json5.h"

typedef std::set<std::string> sset;

static const JAST &get(const std::string &key, const JAST &jast) {
  static JAST null(JSON_NULLVAL);
  if (jast.kind == JSON_OBJECT)
    for (auto &x : jast.children)
      if (x.first == key)
        return x.second;
  return null;
}

static void make_shadow_tree(const std::string &root, const JAST &jast, sset &visible) {
  std::string roots = root + "/";

  for (auto &x : get("visible", jast).children) {
    const std::string &file = x.second.value;

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

static void relink_shadow_tree(const std::string &root, const sset &outputs) {
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
        std::cerr << "link " << x << ": " << strerror(errno) << std::endl;
        exit(1);
      }
    }
  }
}

static void remove_shadow_tree(const std::string &root, const sset &exist) {
  std::string roots = root + "/";
  for (auto it = exist.rbegin(); it != exist.rend(); ++it) {
    std::string target = roots + *it;
    if (it->back() == '/') {
      target.resize(target.size()-1);
      if (rmdir(target.c_str()) != 0) {
        std::cerr << "rmdir " << target << ": " << strerror(errno) << std::endl;
        exit(1);
      }
    } else {
      if (unlink(target.c_str()) != 0) {
        std::cerr << "unlink " << target << ": " << strerror(errno) << std::endl;
        exit(1);
      }
    }
  }

  if (rmdir(root.c_str()) != 0) {
    std::cerr << "rmdir " << root << ": " << strerror(errno) << std::endl;
    exit(1);
  }
}

static char hex(unsigned char x) {
  if (x < 10) return '0' + x;
  return 'a' + x - 10;
}

static std::string escape(const std::string &x) {
  std::string out;
  char escape[] = "\\u0000";
  for (char z : x) {
    unsigned char c = z;
    if (c == '"') out.append("\\\"");
    else if (c == '\\') out.append("\\\\");
    else if (z >= 0x20) {
      out.push_back(c);
    } else {
      escape[4] = hex(z >> 4);
      escape[5] = hex(z & 0xf);
      out.append(escape);
    }
  }
  return out;
}

int main(int argc, const char **argv) {
  JAST jast;

  if (argc != 3) {
    std::cerr << "Syntax: preload-fuse <input-json> <output-json>" << std::endl;
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

  sset visible;
  make_shadow_tree(root, jast, visible);

  // Prepare the subcommand inputs
  std::vector<char *> arg, env;
  for (auto &x : get("command", jast).children)
    arg.push_back(const_cast<char*>(x.second.value.c_str()));
  arg.push_back(0);
  for (auto &x : get("environment", jast).children)
    env.push_back(const_cast<char*>(x.second.value.c_str()));
  env.push_back(0);

  std::string dir = root + "/" + get("directory", jast).value;
  std::string stdin = get("stdin", jast).value;
  if (stdin.empty()) stdin = "/dev/null";

  std::string command = arg[0];

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
    status = WTERMSIG(status);
  }

  struct timeval stop;
  gettimeofday(&stop, 0);

  sset exist;
  scan_shadow_tree(exist, root);

  sset inputs, outputs;
  auto v = visible.begin(), e = exist.begin();
  while (v != visible.end() && e != exist.end()) {
    int comp = e->compare(*v);
    if (comp < 0) { outputs.insert(*e); ++e; }
    else if (comp > 0) { std::cerr << "Visible file was deleted: " << *v << std::endl; exit(1); }
    else { ++e; ++v; }
  }

  relink_shadow_tree(root, outputs);
  remove_shadow_tree(root, exist);
  
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
  for (auto &x : visible) {
    out << (first?"":",") << "\"" << escape(x) << "\"";
    first = false;
  }

  out << "],\"outputs\":[";

  first = true; 
  for (auto &x : outputs) {
    out << (first?"":",") << "\"" << escape(x) << "\"";
    first = false;
  }

  out << "],\"indexes\":[]}" << std::endl;

  if (out.bad()) {
    std::cerr << "bad " << argv[2] << ": " << strerror(errno) << std::endl;
    exit(1);
  }

  return 0;
}
