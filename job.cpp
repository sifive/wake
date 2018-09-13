#include "job.h"
#include "prim.h"
#include "value.h"
#include "heap.h"
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sstream>
#include <iostream>
#include <list>
#include <vector>
#include <cstring>

struct Task {
  bool cache;
  std::string environ;
  std::string stdin;
  std::string dir;
  std::string files;
  std::string cmdline;
  std::unique_ptr<Receiver> completion;
  Task(int cache_, const std::string &environ_, const std::string &stdin_, const std::string &dir_, const std::string &files_, const std::string &cmdline_, std::unique_ptr<Receiver> completion_) :
    cache(cache_), environ(environ_), stdin(stdin_), dir(dir_), files(files_), cmdline(cmdline_), completion(std::move(completion_)) { }
};

struct Job {
  pid_t pid;
  int pipe_stdout;
  int pipe_stderr;
  std::stringstream stdout;
  std::stringstream stderr;
  std::unique_ptr<Receiver> completion;
  Job() : pid(0) { }
};

struct JobTable::detail {
  std::vector<Job> table;
  std::list<Task> tasks;
  sigset_t sigset;
  bool verbose;
};

/* We return this fancy type, instead of a tuple, because we want to load the results from the database */
struct JobResult : public Value {
  std::shared_ptr<Value> stdout;
  std::shared_ptr<Value> stderr;
  std::shared_ptr<Value> inputs;
  std::shared_ptr<Value> outputs;
  static const char *type;
  JobResult() : Value(type) { }
};

const char *JobResult::type = "JobResult";

static std::unique_ptr<Receiver> cast_jobresult(std::unique_ptr<Receiver> completion, const std::shared_ptr<Value> &value, JobResult **job) {
  if (value->type != JobResult::type) {
    resume(std::move(completion), std::make_shared<Exception>(value->to_str() + " is not a JobResult"));
    return std::unique_ptr<Receiver>();
  } else {
    *job = reinterpret_cast<JobResult*>(value.get());
    return completion;
  }
}
#define JOBRESULT(arg, i) 							\
  JobResult *arg;								\
  do {										\
    completion = cast_jobresult(std::move(completion), args[i], &arg);		\
    if (!completion) return;							\
  } while(0)

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
      int pipe_stdout[2];
      int pipe_stderr[2];
      if (pipe(pipe_stdout) == -1 || pipe(pipe_stderr) == -1) {
        perror("pipe");
        exit(1);
      }
      i.pid = fork();
      if (i.pid == 0) {
        dup2(pipe_stdout[1], 1);
        dup2(pipe_stderr[1], 2);
        close(pipe_stdout[0]);
        close(pipe_stdout[1]);
        close(pipe_stderr[0]);
        close(pipe_stderr[1]);
        int stdin = open(task.stdin.c_str(), O_RDONLY);
        if (!stdin) {
          perror(("open " + task.stdin).c_str());
          exit(1);
        }
        dup2(stdin, 0);
        close(stdin);
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
      close(pipe_stdout[1]);
      close(pipe_stderr[1]);
      i.pipe_stdout = pipe_stdout[0];
      i.pipe_stderr = pipe_stderr[0];
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
      if (i.pid != 0 && i.pipe_stdout != -1) {
        if (i.pipe_stdout >= nfds) nfds = i.pipe_stdout + 1;
        FD_SET(i.pipe_stdout, &set);
      }
      if (i.pid != 0 && i.pipe_stderr != -1) {
        if (i.pipe_stderr >= nfds) nfds = i.pipe_stderr + 1;
        FD_SET(i.pipe_stderr, &set);
      }
    }
    if (npids == 0) return false;

    int retval = pselect(nfds, &set, 0, 0, 0, &imp->sigset);
    if (retval == -1 && errno != EINTR) {
      perror("pselect");
      exit(1);
    }

    if (retval > 0) for (auto &i : imp->table) {
      if (i.pid != 0 && i.pipe_stdout != -1 && FD_ISSET(i.pipe_stdout, &set)) {
        int got = read(i.pipe_stdout, buffer, sizeof(buffer));
        if (got <= 0) {
          close(i.pipe_stdout);
          i.pipe_stdout = -1;
        } else {
          i.stdout.write(buffer, got);
        }
      }
      if (i.pid != 0 && i.pipe_stderr != -1 && FD_ISSET(i.pipe_stderr, &set)) {
        int got = read(i.pipe_stderr, buffer, sizeof(buffer));
        if (got <= 0) {
          close(i.pipe_stderr);
          i.pipe_stderr = -1;
        } else {
          i.stderr.write(buffer, got);
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
          if (i.pipe_stdout != -1) {
            int got;
            while ((got = read(i.pipe_stdout, buffer, sizeof(buffer))) > 0)
              i.stdout.write(buffer, got);
          }
          if (i.pipe_stderr != -1) {
            int got;
            while ((got = read(i.pipe_stderr, buffer, sizeof(buffer))) > 0)
              i.stderr.write(buffer, got);
          }

          if (i.pipe_stdout != -1) close(i.pipe_stdout);
          if (i.pipe_stderr != -1) close(i.pipe_stderr);
          i.pid = 0;
          i.pipe_stdout = -1;
          i.pipe_stderr = -1;
          auto stdout = std::make_shared<String>(i.stdout.str());
          auto stderr = std::make_shared<String>(i.stderr.str());
          i.stdout.clear();
          i.stderr.clear();
          if (imp->verbose) std::cout << stdout->value;
          std::cerr << stderr->value;

          if (code == 0) {
            std::vector<std::shared_ptr<Value> > nil;
            auto out = std::make_shared<JobResult>();
            out->stdout  = std::move(stdout);
            out->stderr  = std::move(stderr);
            out->inputs  = make_list(std::move(nil));
            out->outputs = make_list(std::move(nil));
            resume(std::move(i.completion), std::move(out));
          } else {
            resume(std::move(i.completion), std::make_shared<Exception>("Non-zero exit status (" + std::to_string(code) + ")\n" + stderr->value));
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
  EXPECT(6);
  INTEGER(cache, 0);
  STRING(env, 1);
  STRING(stdin, 2);
  STRING(dir, 3);
  STRING(files, 4);
  STRING(cmd, 5);
  jobtable->imp->tasks.emplace_back(cache->str() == "1", env->value, stdin->value, dir->value, files->value, cmd->value, std::move(completion));
  launch(jobtable);
}

static void prim_stdout(void *data, std::vector<std::shared_ptr<Value> > &&args, std::unique_ptr<Receiver> completion) {
  EXPECT(1);
  JOBRESULT(arg0, 0);
  auto out = arg0->stdout;
  RETURN(out);
}

static void prim_stderr(void *data, std::vector<std::shared_ptr<Value> > &&args, std::unique_ptr<Receiver> completion) {
  EXPECT(1);
  JOBRESULT(arg0, 0);
  auto out = arg0->stderr;
  RETURN(out);
}

static void prim_inputs(void *data, std::vector<std::shared_ptr<Value> > &&args, std::unique_ptr<Receiver> completion) {
  EXPECT(1);
  JOBRESULT(arg0, 0);
  auto out = arg0->inputs;
  RETURN(out);
}

static void prim_outputs(void *data, std::vector<std::shared_ptr<Value> > &&args, std::unique_ptr<Receiver> completion) {
  EXPECT(1);
  JOBRESULT(arg0, 0);
  auto out = arg0->outputs;
  RETURN(out);
}

void prim_register_job(JobTable *jobtable, PrimMap &pmap) {
  pmap["job"].first = prim_job;
  pmap["job"].second = jobtable;
  pmap["job_stdout" ].first = prim_stdout;
  pmap["job_stderr" ].first = prim_stderr;
  pmap["job_inputs" ].first = prim_inputs;
  pmap["job_outputs"].first = prim_outputs;
}
