#include "job.h"
#include "prim.h"
#include "value.h"
#include "heap.h"
#include "database.h"
#include "location.h"
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
  long job;
  std::string environ;
  std::string stdin;
  std::string dir;
  std::string files;
  std::string cmdline;
  std::unique_ptr<Receiver> completion;
  std::shared_ptr<Binding> binding;
  Task(long job_, const std::string &environ_, const std::string &stdin_, const std::string &dir_, const std::string &files_, const std::string &cmdline_, std::unique_ptr<Receiver> completion_, const std::shared_ptr<Binding> &binding_) :
    job(job_), environ(environ_), stdin(stdin_), dir(dir_), files(files_), cmdline(cmdline_), completion(std::move(completion_)), binding(binding_) { }
};

struct Job {
  pid_t pid;
  long job;
  int pipe_stdout;
  int pipe_stderr;
  std::unique_ptr<Receiver> completion;
  std::shared_ptr<Binding> binding;
  Job() : pid(0) { }
};

struct JobTable::detail {
  std::vector<Job> table;
  std::list<Task> tasks;
  sigset_t sigset;
  Database *db;
  bool verbose;
};

/* We return this fancy type, instead of a tuple, because we want to load the results from the database */
struct JobResult : public Value {
  Database *db;
  long job;
  static const char *type;
  JobResult(Database *db_, long job_) : Value(type), db(db_), job(job_) { }
};

const char *JobResult::type = "JobResult";

static std::unique_ptr<Receiver> cast_jobresult(std::unique_ptr<Receiver> completion, const std::shared_ptr<Binding> &binding, const std::shared_ptr<Value> &value, JobResult **job) {
  if (value->type != JobResult::type) {
    resume(std::move(completion), std::make_shared<Exception>(value->to_str() + " is not a JobResult", binding));
    return std::unique_ptr<Receiver>();
  } else {
    *job = reinterpret_cast<JobResult*>(value.get());
    return completion;
  }
}
#define JOBRESULT(arg, i) 								\
  JobResult *arg;									\
  do {											\
    completion = cast_jobresult(std::move(completion), binding, args[i], &arg);		\
    if (!completion) return;								\
  } while(0)

static void handle_SIGCHLD(int sig) {
  /* noop -- we just want to interrupt select */
}

JobTable::JobTable(Database *db, int max_jobs, bool verbose) : imp(new JobTable::detail) {
  imp->table.resize(max_jobs);
  imp->verbose = verbose;
  imp->db = db;

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
      fcntl(pipe_stdout[0], F_SETFL, fcntl(pipe_stdout[0], F_GETFL, 0) | FD_CLOEXEC);
      fcntl(pipe_stderr[0], F_SETFL, fcntl(pipe_stderr[0], F_GETFL, 0) | FD_CLOEXEC);
      i.pid = fork();
      if (i.pid == 0) {
        dup2(pipe_stdout[1], 1);
        dup2(pipe_stderr[1], 2);
        close(pipe_stdout[1]);
        close(pipe_stderr[1]);
        int stdin = open(task.stdin.c_str(), O_RDONLY);
        if (stdin == -1) {
          perror(("open " + task.stdin).c_str());
          exit(1);
        }
        dup2(stdin, 0);
        close(stdin);
        if (chdir(task.dir.c_str())) {
          perror("chdir");
          exit(1);
        }
        // !!! use 'files'
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
      i.job = task.job;
      i.pipe_stdout = pipe_stdout[0];
      i.pipe_stderr = pipe_stderr[0];
      i.completion = std::move(task.completion);
      i.binding = std::move(task.binding);
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
          imp->db->save_output(i.job, 1, buffer, got);
        }
      }
      if (i.pid != 0 && i.pipe_stderr != -1 && FD_ISSET(i.pipe_stderr, &set)) {
        int got = read(i.pipe_stderr, buffer, sizeof(buffer));
        if (got <= 0) {
          close(i.pipe_stderr);
          i.pipe_stderr = -1;
        } else {
          imp->db->save_output(i.job, 2, buffer, got);
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
              imp->db->save_output(i.job, 1, buffer, got);
            close(i.pipe_stdout);
          }
          if (i.pipe_stderr != -1) {
            int got;
            while ((got = read(i.pipe_stderr, buffer, sizeof(buffer))) > 0)
              imp->db->save_output(i.job, 2, buffer, got);
            close(i.pipe_stderr);
          }
          i.pid = 0;
          i.pipe_stdout = -1;
          i.pipe_stderr = -1;
          if (imp->verbose) std::cout << imp->db->get_output(i.job, 1);
          std::cerr << imp->db->get_output(i.job, 2);

          if (code == 0) {
            std::vector<std::shared_ptr<Value> > nil;
            auto out = std::make_shared<JobResult>(imp->db, i.job);
            resume(std::move(i.completion), std::move(out));
          } else {
            resume(std::move(i.completion), std::make_shared<Exception>("Non-zero exit status (" + std::to_string(code) + ")", i.binding));
          }

          i.binding.reset();
        }
      }
    }

    if (done > 0) {
      launch(this);
      return true;
    }
  }
}

static PRIMFN(prim_job) {
  JobTable *jobtable = reinterpret_cast<JobTable*>(data);
  EXPECT(6);
  INTEGER(cache, 0);
  STRING(env, 1);
  STRING(stdin, 2);
  STRING(dir, 3);
  STRING(files, 4);
  STRING(cmd, 5);
  int cached = cache->str() == "1";
  long job;
  std::stringstream stack;
  for (auto &i : Binding::stack_trace(binding)) stack << i << std::endl;
  if (jobtable->imp->db->needs_build(
      cached,
      dir->value,
      cmd->value,
      env->value,
      stdin->value,
      files->value,
      stack.str(),
      &job))
  {
    jobtable->imp->tasks.emplace_back(
      job,
      env->value,
      stdin->value,
      dir->value,
      files->value,
      cmd->value,
      std::move(completion),
      binding);
    launch(jobtable);
  }
}

static PRIMFN(prim_stdout) {
  EXPECT(1);
  JOBRESULT(arg0, 0);
  auto out = std::make_shared<String>(arg0->db->get_output(arg0->job, 1));
  RETURN(out);
}

static PRIMFN(prim_stderr) {
  EXPECT(1);
  JOBRESULT(arg0, 0);
  auto out = std::make_shared<String>(arg0->db->get_output(arg0->job, 2));
  RETURN(out);
}

static PRIMFN(prim_inputs) {
  EXPECT(1);
  JOBRESULT(arg0, 0);
  auto files = arg0->db->get_inputs(arg0->job);
  std::vector<std::shared_ptr<Value> > vals;
  vals.reserve(files.size());
  for (auto &i : files) vals.emplace_back(std::make_shared<String>(std::move(i)));
  auto out = make_list(std::move(vals));
  RETURN(out);
}

static PRIMFN(prim_outputs) {
  EXPECT(1);
  JOBRESULT(arg0, 0);
  auto files = arg0->db->get_outputs(arg0->job);
  std::vector<std::shared_ptr<Value> > vals;
  vals.reserve(files.size());
  for (auto &i : files) vals.emplace_back(std::make_shared<String>(std::move(i)));
  auto out = make_list(std::move(vals));
  RETURN(out);
}

static PRIMFN(prim_add_hash) {
  EXPECT(3);
  JOBRESULT(job, 0);
  STRING(file, 1);
  STRING(hash, 2);
  job->db->add_hash(file->value, hash->value);
  RETURN(args[1]);
}

void prim_register_job(JobTable *jobtable, PrimMap &pmap) {
  pmap["job"].first = prim_job;
  pmap["job"].second = jobtable;
  pmap["job_stdout" ].first = prim_stdout;
  pmap["job_stderr" ].first = prim_stderr;
  pmap["job_inputs" ].first = prim_inputs;
  pmap["job_outputs"].first = prim_outputs;
  pmap["add_hash"   ].first = prim_add_hash;
}
