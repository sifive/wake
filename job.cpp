#include "job.h"
#include "prim.h"
#include "value.h"
#include "heap.h"
#include "database.h"
#include "location.h"
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <sys/time.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sstream>
#include <iostream>
#include <list>
#include <vector>
#include <cstring>

#define STATE_FORKED	1  // in database and running
#define STATE_STDOUT	2  // stdout fully in database
#define STATE_STDERR	4  // stderr fully in database
#define STATE_MERGED	8  // exit status in struct
#define STATE_FINISHED	16 // inputs+outputs+status+runtime in database

// Can be queried at multiple stages of the job's lifetime
struct JobResult : public Value {
  Database *db;
  int state;
  Hash code; // hash(dir, stdin, environ, cmdline)
  pid_t pid;
  long job;
  double runtime;
  int status; // -signal, +code
  std::shared_ptr<Value> bad_finish;

  // There are 4 distinct wait queues for jobs
  std::unique_ptr<Receiver> q_stdout;  // waken once stdout closed
  std::unique_ptr<Receiver> q_stderr;  // waken once stderr closed
  std::unique_ptr<Receiver> q_merge;   // waken once job status available (merged/waitpid)
  std::unique_ptr<Receiver> q_inputs;  // waken once job is merged+finished (inputs+outputs available)
  std::unique_ptr<Receiver> q_outputs; // waken once job is merged+finished (inputs+outputs available)

  static const char *type;
  JobResult(Database *db_, const std::string &dir, const std::string &stdin, const std::string &environ, const std::string &cmdline);

  void stream(std::ostream &os) const;
  void hash(std::unique_ptr<Hasher> hasher);

  void process(ThunkQueue &queue); // Run commands based on state
};

const char *JobResult::type = "JobResult";

void JobResult::stream(std::ostream &os) const { os << "JobResult(" << job << ")"; }
void JobResult::hash(std::unique_ptr<Hasher> hasher) { hasher->receive(code); }

// A Task is a job that is not yet forked
struct Task {
  std::shared_ptr<JobResult> job;
  std::string dir;
  std::string stdin;
  std::string environ;
  std::string cmdline;
  std::string stack;
  Task(Database *db_, const std::string &dir_, const std::string &stdin_, const std::string &environ_, const std::string &cmdline_, const std::string &stack_);
};

// A Job is a forked job not yet merged
struct Job {
  std::shared_ptr<JobResult> job;
  int pipe_stdout;
  int pipe_stderr;
  struct timeval start;
  pid_t pid;
  Job() : pid(0) { }
  double runtime(struct timeval now);
};

double Job::runtime(struct timeval now) {
  return now.tv_sec - start.tv_sec + (now.tv_usec - start.tv_usec)/1000000.0;
}

// Implementation details for a JobTable
struct JobTable::detail {
  std::vector<Job> table;
  std::list<Task> tasks;
  sigset_t sigset;
  Database *db;
  bool verbose;
};

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
    // if (imp->verbose) std::cerr << "<<< " << pid << std::endl;
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
      i.job = std::move(task.job);
      i.pipe_stdout = pipe_stdout[0];
      i.pipe_stderr = pipe_stderr[0];
      gettimeofday(&i.start, 0);
      i.job->db->insert_job(task.dir, task.stdin, task.environ, task.cmdline, task.stack, &i.job->job);
      i.job->pid = i.pid = fork();
      if (i.pid == 0) {
        dup2(pipe_stdout[1], 1);
        dup2(pipe_stderr[1], 2);
        close(pipe_stdout[1]);
        close(pipe_stderr[1]);
        int stdin = open(task.stdin.empty() ? "/dev/null" : task.stdin.c_str(), O_RDONLY);
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
        auto cmdline = split_null(task.cmdline);
        auto environ = split_null(task.environ);
        execve(cmdline[0], cmdline, environ);
        perror("execve");
        exit(1);
      }
      close(pipe_stdout[1]);
      close(pipe_stderr[1]);
      for (char &c : task.cmdline) if (c == 0) c = ' ';
      if (jobtable->imp->verbose) std::cerr << task.cmdline << std::endl;
      jobtable->imp->tasks.pop_front();
    }
  }
}

bool JobTable::wait(ThunkQueue &queue) {
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

    struct timeval now;
    gettimeofday(&now, 0);

    if (retval > 0) for (auto &i : imp->table) {
      if (i.pid != 0 && i.pipe_stdout != -1 && FD_ISSET(i.pipe_stdout, &set)) {
        int got = read(i.pipe_stdout, buffer, sizeof(buffer));
        if (got <= 0) {
          close(i.pipe_stdout);
          i.pipe_stdout = -1;
          i.job->state |= STATE_STDOUT;
          i.job->process(queue);
        } else {
          i.job->db->save_output(i.job->job, 1, buffer, got, i.runtime(now));
        }
      }
      if (i.pid != 0 && i.pipe_stderr != -1 && FD_ISSET(i.pipe_stderr, &set)) {
        int got = read(i.pipe_stderr, buffer, sizeof(buffer));
        if (got <= 0) {
          close(i.pipe_stderr);
          i.pipe_stderr = -1;
          i.job->state |= STATE_STDERR;
          i.job->process(queue);
        } else {
          i.job->db->save_output(i.job->job, 2, buffer, got, i.runtime(now));
        }
      }
    }

    int done = 0;
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
      if (WIFSTOPPED(status)) continue;
      // if (imp->verbose) std::cerr << "<<< " << pid << std::endl;

      ++done;
      int code = 0;
      if (WIFEXITED(status)) {
        code = WEXITSTATUS(status);
      } else if (WIFSIGNALED(status)) {
        code = -WTERMSIG(status);
      }

      for (auto &i : imp->table) {
        if (i.pid == pid) {
          i.job->state |= STATE_MERGED;
          i.job->status = code;
          if (i.pipe_stdout != -1) {
            int got;
            while ((got = read(i.pipe_stdout, buffer, sizeof(buffer))) > 0)
              i.job->db->save_output(i.job->job, 1, buffer, got, i.runtime(now));
            close(i.pipe_stdout);
            i.job->state |= STATE_STDOUT;
          }
          if (i.pipe_stderr != -1) {
            int got;
            while ((got = read(i.pipe_stderr, buffer, sizeof(buffer))) > 0)
              i.job->db->save_output(i.job->job, 2, buffer, got, i.runtime(now));
            close(i.pipe_stderr);
            i.job->state |= STATE_STDERR;
          }
          i.pid = 0;
          i.pipe_stdout = -1;
          i.pipe_stderr = -1;
          // if (imp->verbose) std::cout << i.job->db->get_output(i.job->job, 1);
          if (imp->verbose) std::cerr << i.job->db->get_output(i.job->job, 2);
          i.job->process(queue);
          i.job.reset();
        }
      }
    }

    if (done > 0) {
      launch(this);
      return true;
    }
  }
}

JobResult::JobResult(Database *db_, const std::string &dir, const std::string &stdin, const std::string &environ, const std::string &cmdline)
  : Value(type), db(db_), state(0), code(), pid(0), job(-1), runtime(0), status(0)
{
  std::vector<uint64_t> codes;
  Hash acc;
  HASH(dir    .data(), dir    .size(), (long)type, acc); acc.push(codes);
  HASH(stdin  .data(), stdin  .size(), (long)type, acc); acc.push(codes);
  HASH(environ.data(), environ.size(), (long)type, acc); acc.push(codes);
  HASH(cmdline.data(), cmdline.size(), (long)type, acc); acc.push(codes);
  HASH(codes.data(), 8*codes.size(), (long)type, code);
}

Task::Task(Database *db_, const std::string &dir_, const std::string &stdin_, const std::string &environ_, const std::string &cmdline_, const std::string &stack_)
  : job(std::make_shared<JobResult>(db_, dir_, stdin_, environ_, cmdline_)),
    dir(dir_), stdin(stdin_), environ(environ_), cmdline(cmdline_), stack(stack_)
{
}

static std::unique_ptr<Receiver> cast_jobresult(ThunkQueue &queue, std::unique_ptr<Receiver> completion, const std::shared_ptr<Binding> &binding, const std::shared_ptr<Value> &value, JobResult **job) {
  if (value->type != JobResult::type) {
    Receiver::receiveM(queue, std::move(completion),
      std::make_shared<Exception>(value->to_str() + " is not a JobResult", binding));
    return std::unique_ptr<Receiver>();
  } else {
    *job = reinterpret_cast<JobResult*>(value.get());
    return completion;
  }
}

#define JOBRESULT(arg, i) 									\
  JobResult *arg;										\
  do {												\
    completion = cast_jobresult(queue, std::move(completion), binding, args[i], &arg);		\
    if (!completion) return;									\
  } while(0)

static PRIMFN(prim_job_launch) {
  JobTable *jobtable = reinterpret_cast<JobTable*>(data);
  EXPECT(4);
  STRING(dir, 0);
  STRING(stdin, 1);
  STRING(env, 2);
  STRING(cmd, 3);
  std::stringstream stack;
  for (auto &i : Binding::stack_trace(binding)) stack << i << std::endl;

  jobtable->imp->tasks.emplace_back(
    jobtable->imp->db,
    dir->value,
    stdin->value,
    env->value,
    cmd->value,
    stack.str());

  std::shared_ptr<Value> out = jobtable->imp->tasks.back().job;
  launch(jobtable);

  RETURN(out);
}

static PRIMFN(prim_job_cache) {
  JobTable *jobtable = reinterpret_cast<JobTable*>(data);
  EXPECT(5);
  STRING(dir, 0);
  STRING(stdin, 1);
  STRING(env, 2);
  STRING(cmd, 3);
  STRING(visible, 4);

  long job;
  bool cached = jobtable->imp->db->reuse_job(
    dir->value,
    stdin->value,
    env->value,
    cmd->value,
    visible->value,
    &job);

  if (!cached) RAISE("not cached");

  auto out = std::make_shared<JobResult>(jobtable->imp->db, dir->value, stdin->value, env->value, cmd->value);
  out->state = STATE_FORKED|STATE_STDOUT|STATE_STDERR|STATE_MERGED|STATE_FINISHED;
  out->job = job;

  RETURN(out);
}

void JobResult::process(ThunkQueue &queue) {
  if ((state & STATE_STDOUT) && q_stdout) {
    auto out = std::make_shared<String>(db->get_output(job, 1));
    std::unique_ptr<Receiver> iter, next;
    for (iter = std::move(q_stdout); iter; iter = std::move(next)) {
      next = std::move(iter->next);
      Receiver::receiveC(queue, std::move(iter), out);
    }
    q_stdout.reset();
  }

  if ((state & STATE_STDERR) && q_stderr) {
    auto out = std::make_shared<String>(db->get_output(job, 2));
    std::unique_ptr<Receiver> iter, next;
    for (iter = std::move(q_stderr); iter; iter = std::move(next)) {
      next = std::move(iter->next);
      Receiver::receiveC(queue, std::move(iter), out);
    }
    q_stderr.reset();
  }

  if ((state & STATE_MERGED) && q_merge) {
    auto out = std::make_shared<Integer>(status);
    std::unique_ptr<Receiver> iter, next;
    for (iter = std::move(q_merge); iter; iter = std::move(next)) {
      next = std::move(iter->next);
      Receiver::receiveC(queue, std::move(iter), out);
    }
    q_merge.reset();
  }

  if ((state & STATE_FINISHED) && q_inputs) {
    auto files = db->get_tree(1, job);
    std::vector<std::shared_ptr<Value> > vals;
    vals.reserve(files.size());
    for (auto &i : files) vals.emplace_back(std::make_shared<String>(std::move(i)));
    auto out = make_list(std::move(vals));
    std::unique_ptr<Receiver> iter, next;
    if (bad_finish) out = bad_finish;
    for (iter = std::move(q_inputs); iter; iter = std::move(next)) {
      next = std::move(iter->next);
      Receiver::receiveC(queue, std::move(iter), out);
    }
    q_inputs.reset();
  }

  if ((state & STATE_FINISHED) && q_outputs) {
    auto files = db->get_tree(2, job);
    std::vector<std::shared_ptr<Value> > vals;
    vals.reserve(files.size());
    for (auto &i : files) vals.emplace_back(std::make_shared<String>(std::move(i)));
    auto out = make_list(std::move(vals));
    if (bad_finish) out = bad_finish;
    std::unique_ptr<Receiver> iter, next;
    for (iter = std::move(q_outputs); iter; iter = std::move(next)) {
      next = std::move(iter->next);
      Receiver::receiveC(queue, std::move(iter), out);
    }
    q_outputs.reset();
  }
}

static PRIMFN(prim_job_output) {
  EXPECT(2);
  JOBRESULT(arg0, 0);
  INTEGER(arg1, 1);
  if (mpz_cmp_si(arg1->value, 1) == 0) {
    completion->next = std::move(arg0->q_stdout);
    arg0->q_stdout = std::move(completion);
    arg0->process(queue);
  } else if (mpz_cmp_si(arg1->value, 2) == 0) {
    completion->next = std::move(arg0->q_stderr);
    arg0->q_stderr = std::move(completion);
    arg0->process(queue);
  } else {
    RAISE("argument neither stdout(1) nor stderr(2)");
  }
}

static PRIMFN(prim_job_kill) {
  EXPECT(2);
  JOBRESULT(arg0, 0);
  INTEGER(arg1, 1);
  REQUIRE(mpz_cmp_si(arg1->value, 256) < 0, "signal too large (> 256)");
  REQUIRE(mpz_cmp_si(arg1->value, 0) >= 0, "signal too small (< 0)");
  int sig = mpz_get_si(arg1->value);
  if ((arg0->state & STATE_FORKED) && !(arg0->state & STATE_MERGED))
    kill(arg0->pid, sig);
  completion->next = std::move(arg0->q_merge);
  arg0->q_merge = std::move(completion);
  arg0->process(queue);
}

static PRIMFN(prim_job_tree) {
  EXPECT(2);
  JOBRESULT(arg0, 0);
  INTEGER(arg1, 1);
  if (mpz_cmp_si(arg1->value, 1) == 0) {
    completion->next = std::move(arg0->q_inputs);
    arg0->q_inputs = std::move(completion);
    arg0->process(queue);
  } else if (mpz_cmp_si(arg1->value, 2) == 0) {
    completion->next = std::move(arg0->q_outputs);
    arg0->q_outputs = std::move(completion);
    arg0->process(queue);
  } else {
    RAISE("argument neither inputs(1) nor outputs(2)");
  }
}

static PRIMFN(prim_job_finish) {
  REQUIRE (args.size() == 3, "prim_job_finish not called on 3 arguments");
  JOBRESULT(job, 0);
  if (!(job->state & STATE_MERGED)) {
    // fatal because it means the queue will not converge
    std::cerr << "ERROR: attempted to finish an unmerged job" << std::endl;
    exit(1);
  }

  // On an exception, we need to still FINISH, but with an exception from the inputs/outputs
  std::string empty;
  std::string *inputs;
  std::string *outputs;

  if (args[1]->type == String::type) {
    inputs = &reinterpret_cast<String*>(args[1].get())->value;
  } else if (args[1]->type == Exception::type) {
    job->bad_finish = args[1];
    inputs = &empty;
  } else {
    job->bad_finish = std::make_shared<Exception>("prim_job_finish arg1 not a string", binding);
    inputs = &empty;
  }

  if (args[2]->type == String::type) {
    outputs = &reinterpret_cast<String*>(args[2].get())->value;
  } else if (args[2]->type == Exception::type) {
    job->bad_finish = args[2];
    outputs = &empty;
  } else {
    job->bad_finish = std::make_shared<Exception>("prim_job_finish arg2 not a string", binding);
    outputs = &empty;
  }

  job->db->finish_job(job->job, *inputs, *outputs, job->status, job->runtime);
  job->state |= STATE_FINISHED;
  job->process(queue);

  auto out = make_true();
  RETURN(out);
}

static PRIMFN(prim_add_hash) {
  JobTable *jobtable = reinterpret_cast<JobTable*>(data);
  EXPECT(2);
  STRING(file, 0);
  STRING(hash, 1);
  jobtable->imp->db->add_hash(file->value, hash->value);
  RETURN(args[0]);
}

void prim_register_job(JobTable *jobtable, PrimMap &pmap) {
  pmap["job_launch" ].second = jobtable;
  pmap["job_cache"  ].second = jobtable;
  pmap["add_hash"   ].second = jobtable;
  pmap["job_launch" ].first = prim_job_launch;
  pmap["job_cache"  ].first = prim_job_cache;
  pmap["job_output" ].first = prim_job_output;
  pmap["job_kill"   ].first = prim_job_kill;
  pmap["job_tree"   ].first = prim_job_tree;
  pmap["job_finish" ].first = prim_job_finish;
  pmap["add_hash"   ].first = prim_add_hash;
}
