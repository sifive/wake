#include "job.h"
#include "prim.h"
#include "value.h"
#include "heap.h"
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <signal.h>
#include <unistd.h>
#include <sstream>
#include <iostream>
#include <list>
#include <vector>
#include <cstring>

struct Task {
  bool cache;
  std::string environ;
  std::string dir;
  std::string files;
  std::string cmdline;
  std::unique_ptr<Receiver> completion;
  Task(int cache_, const std::string &environ_, const std::string &dir_, const std::string &files_, const std::string &cmdline_, std::unique_ptr<Receiver> completion_) :
    cache(cache_), environ(environ_), dir(dir_), files(files_), cmdline(cmdline_), completion(std::move(completion_)) { }
};

struct Job {
  pid_t pid;
  int pipe;
  std::stringstream output;
  std::unique_ptr<Receiver> completion;
  Job() : pid(0) { }
};

struct JobTable::detail {
  std::vector<Job> table;
  std::list<Task> tasks;
  sigset_t sigset;
  bool verbose;
};

static void handle_SIGCHLD(int sig) {
  /* noop -- we just want to interrupt select */
}

JobTable::JobTable(int max_jobs, bool verbose) : imp(new JobTable::detail) {
  imp->table.resize(max_jobs);
  imp->verbose = verbose;

  sigset_t block;
  sigemptyset(&block);
  sigaddset(&block, SIGCHLD);
  sigprocmask(SIG_BLOCK, &block, &imp->sigset);

  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = handle_SIGCHLD;
  sa.sa_flags = SA_NOCLDSTOP | SA_RESTART;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGCHLD, &sa, 0);
}

JobTable::~JobTable() {
  for (auto &i : imp->table) {
    if (i.pid != 0) kill(i.pid, SIGKILL);
  }
  pid_t pid;
  int status;
  while ((pid = waitpid(-1, &status, 0)) > 0) {
    if (imp->verbose) std::cerr << "<<< " << pid << std::endl;
  }
}

static char **split_null(std::string &str) {
  int nulls = 0;
  for (char c : str) nulls += (c == 0);
  char **out = new char*[nulls+1];

  nulls = 0;
  out[0] = const_cast<char*>(str.c_str());
  char *end = out[0] + str.size();
  for (char *scan = out[0]; scan != end; ++scan) {
    if (*scan == 0) {
      ++nulls;
      out[nulls] = scan+1;
    }
  }
  out[nulls] = 0;
    
  return out;
}

static void launch(JobTable *jobtable) {
  for (auto &i : jobtable->imp->table) {
    if (jobtable->imp->tasks.empty()) break;
    if (i.pid == 0) {
      Task &task = jobtable->imp->tasks.front();
      int pipefd[2];
      if (pipe(pipefd) == -1) {
        perror("pipe");
        exit(1);
      }
      i.pid = fork();
      if (i.pid == 0) {
        dup2(pipefd[1], 1);
        dup2(pipefd[1], 2);
        close(pipefd[0]);
        close(pipefd[1]);
        if (chdir(task.dir.c_str())) {
          perror("chdir");
          exit(1);
        }
        // !!! use 'files' and 'cache'
        auto cmdline = split_null(task.cmdline);
        auto environ = split_null(task.environ);
        execve(cmdline[0], cmdline, environ);
        perror("execve");
        exit(1);
      }
      for (char &c : task.cmdline) if (c == 0) c = ' ';
      if (jobtable->imp->verbose)
        std::cerr << ">>> " << i.pid << ": " << task.cmdline << std::endl;
      close(pipefd[1]);
      i.pipe = pipefd[0];
      i.completion = std::move(task.completion);
      jobtable->imp->tasks.pop_front();
    }
  }
}

bool JobTable::wait() {
  char buffer[4096];

  while (1) {
    fd_set set;
    int nfds = 0;
    int npids = 0;

    FD_ZERO(&set);
    for (auto &i : imp->table) {
      npids += (i.pid != 0);
      if (i.pid != 0 && i.pipe != -1) {
        if (i.pipe >= nfds) nfds = i.pipe + 1;
        FD_SET(i.pipe, &set);
      }
    }
    if (npids == 0) return false;

    int retval = pselect(nfds, &set, 0, 0, 0, &imp->sigset);
    if (retval == -1 && errno != EINTR) {
      perror("pselect");
      exit(1);
    }

    if (retval > 0) for (auto &i : imp->table) {
      if (i.pid != 0 && i.pipe != -1 && FD_ISSET(i.pipe, &set)) {
        int got = read(i.pipe, buffer, sizeof(buffer));
        if (got <= 0) {
          close(i.pipe);
          i.pipe = -1;
        } else {
          i.output.write(buffer, got);
        }
      }
    }

    int done = 0;
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
      if (WIFSTOPPED(status)) continue;
      if (imp->verbose) std::cerr << "<<< " << pid << std::endl;

      ++done;
      int code = 0;
      if (WIFEXITED(status)) {
        code = WEXITSTATUS(status);
      } else if (WIFSIGNALED(status)) {
        code = -WTERMSIG(status);
      }

      for (auto &i : imp->table) {
        if (i.pid == pid) {
          if (i.pipe != 0) {
            int got;
            while ((got = read(i.pipe, buffer, sizeof(buffer))) > 0)
              i.output.write(buffer, got);
          }

          std::vector<std::shared_ptr<Value> > out;
          out.emplace_back(std::make_shared<String>(i.output.str()));

          if (i.pipe != -1) close(i.pipe);
          i.pid = 0;
          i.pipe = -1;
          if (imp->verbose) std::cerr << i.output.str();
          i.output.clear();

          if (code == 0) {
            resume(std::move(i.completion), make_list(std::move(out)));
          } else {
            resume(std::move(i.completion), std::make_shared<Exception>("Non-zero exit status (" + std::to_string(code) + ")"));
          }
        }
      }
    }

    if (done > 0) {
      launch(this);
      return true;
    }
  }
}

static void prim_job(void *data, std::vector<std::shared_ptr<Value> > &&args, std::unique_ptr<Receiver> completion) {
  JobTable *jobtable = reinterpret_cast<JobTable*>(data);
  EXPECT(5);
  INTEGER(cache, 0);
  STRING(env, 1);
  STRING(dir, 2);
  STRING(files, 3);
  STRING(cmd, 4);
  jobtable->imp->tasks.emplace_back(cache->str() == "1", env->value, dir->value, files->value, cmd->value, std::move(completion));
  launch(jobtable);
}

void prim_register_job(JobTable *jobtable, PrimMap &pmap) {
  pmap["job"].first = prim_job;
  pmap["job"].second = jobtable;
}
