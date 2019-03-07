#include "job.h"
#include "prim.h"
#include "value.h"
#include "heap.h"
#include "database.h"
#include "location.h"
#include "sources.h"
#include "status.h"
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sstream>
#include <iostream>
#include <list>
#include <vector>
#include <cstring>

// How many job categories to support
#define POOLS 2

#define STATE_FORKED	1  // in database and running
#define STATE_STDOUT	2  // stdout fully in database
#define STATE_STDERR	4  // stderr fully in database
#define STATE_MERGED	8  // exit status in struct
#define STATE_FINISHED	16 // inputs+outputs+status+runtime in database

// Can be queried at multiple stages of the job's lifetime
struct Job : public Value {
  Database *db;
  int state;
  Hash code; // hash(dir, stdin, environ, cmdline)
  pid_t pid;
  long job;
  bool keep;
  double runtime;
  int status; // -signal, +code
  std::shared_ptr<Value> bad_launch;
  std::shared_ptr<Value> bad_finish;

  // There are 4 distinct wait queues for jobs
  std::unique_ptr<Receiver> q_stdout;  // waken once stdout closed
  std::unique_ptr<Receiver> q_stderr;  // waken once stderr closed
  std::unique_ptr<Receiver> q_merge;   // waken once job status available (merged/waitpid)
  std::unique_ptr<Receiver> q_inputs;  // waken once job is merged+finished (inputs+outputs available)
  std::unique_ptr<Receiver> q_outputs; // waken once job is merged+finished (inputs+outputs available)

  static const char *type;
  static TypeVar typeVar;
  Job(Database *db_, const std::string &dir, const std::string &stdin, const std::string &environ, const std::string &cmdline, bool keep);

  void format(std::ostream &os, int depth) const;
  TypeVar &getType();
  Hash hash() const;

  void process(WorkQueue &queue); // Run commands based on state
};

const char *Job::type = "Job";

void Job::format(std::ostream &os, int p) const {
  if (APP_PRECEDENCE < p) os << "(";
  os << "Job " << job;
  if (APP_PRECEDENCE < p) os << ")";
  if (p < 0) os << std::endl;
}

TypeVar Job::typeVar("Job", 0);
TypeVar &Job::getType() {
  return typeVar;
}

Hash Job::hash() const { return code; }

// A Task is a job that is not yet forked
struct Task {
  std::shared_ptr<Job> job;
  std::string dir;
  std::string stdin;
  std::string environ;
  std::string cmdline;
  Task(const std::shared_ptr<Job> &job_, const std::string &dir_, const std::string &stdin_, const std::string &environ_, const std::string &cmdline_)
  : job(job_), dir(dir_), stdin(stdin_), environ(environ_), cmdline(cmdline_) { }
};

// A JobEntry is a forked job with pid|stdout|stderr incomplete
struct JobEntry {
  int pool;
  std::shared_ptr<Job> job; // if unset, available for reuse
  int internal;
  pid_t pid;       //  0 if merged
  int pipe_stdout; // -1 if closed
  int pipe_stderr; // -1 if closed
  std::string stdout_buf;
  std::string stderr_buf;
  struct timeval start;
  std::list<Status>::iterator status;
  JobEntry() : pid(0), pipe_stdout(-1), pipe_stderr(-1) { }
  double runtime(struct timeval now);
};

double JobEntry::runtime(struct timeval now) {
  return now.tv_sec - start.tv_sec + (now.tv_usec - start.tv_usec)/1000000.0;
}

// Implementation details for a JobTable
struct JobTable::detail {
  std::vector<JobEntry> table;
  std::vector<std::list<Task> > tasks;
  sigset_t sigset;
  Database *db;
  bool verbose;
  bool quiet;
  bool check;
};

static void handle_SIGCHLD(int sig) {
  /* noop -- we just want to interrupt select */
  (void)sig;
}

static void handle_SIGALRM(int sig) {
  (void)sig;
  refresh_needed = true;
}

JobTable::JobTable(Database *db, int max_jobs, bool verbose, bool quiet, bool check) : imp(new JobTable::detail) {
  imp->table.resize(max_jobs*POOLS);
  imp->tasks.resize(POOLS);
  imp->verbose = verbose;
  imp->quiet = quiet;
  imp->check = check;
  imp->db = db;

  for (unsigned i = 0; i < imp->table.size(); ++i)
    imp->table[i].pool = i / max_jobs;

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

  sa.sa_handler = handle_SIGALRM;
  sa.sa_flags = SA_RESTART;
  sigaction(SIGALRM, &sa, 0);

  struct itimerval timer;
  timer.it_value.tv_sec = 0;
  timer.it_value.tv_usec = 1000000/5; // refresh at 5Hz
  timer.it_interval = timer.it_value;

  setitimer(ITIMER_REAL, &timer, 0);
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
    if (i.pid == 0 && i.pipe_stdout == -1 && i.pipe_stderr == -1) {
      if (i.job && i.pool) status_state.erase(i.status);
      i.job.reset();
    }

    if (i.job) continue;
    if (jobtable->imp->tasks[i.pool].empty()) continue;

    Task &task = jobtable->imp->tasks[i.pool].front();
    int pipe_stdout[2];
    int pipe_stderr[2];
    if (pipe(pipe_stdout) == -1 || pipe(pipe_stderr) == -1) {
      perror("pipe");
      exit(1);
    }
    fcntl(pipe_stdout[0], F_SETFD, fcntl(pipe_stdout[0], F_GETFD, 0) | FD_CLOEXEC);
    fcntl(pipe_stderr[0], F_SETFD, fcntl(pipe_stderr[0], F_GETFD, 0) | FD_CLOEXEC);
    i.job = std::move(task.job);
    i.internal = !strncmp(task.cmdline.c_str(), "<hash>", 6);
    i.pipe_stdout = pipe_stdout[0];
    i.pipe_stderr = pipe_stderr[0];
    gettimeofday(&i.start, 0);
    std::cout << std::flush;
    std::cerr << std::flush;
    fflush(stdout);
    fflush(stderr);
    std::stringstream prelude;
    prelude << find_execpath() << "/../lib/wake/shim-wake" << '\0'
      << (task.stdin.empty() ? "/dev/null" : task.stdin.c_str()) << '\0'
      << std::to_string(pipe_stdout[1]) << '\0'
      << std::to_string(pipe_stderr[1]) << '\0'
      << task.dir << '\0';
    std::string shim = prelude.str() + task.cmdline;
    auto cmdline = split_null(shim);
    auto environ = split_null(task.environ);
    pid_t pid = vfork();
    if (pid == 0) {
      execve(cmdline[0], cmdline, environ);
      _exit(127);
    }
    delete [] cmdline;
    delete [] environ;
    i.job->pid = i.pid = pid;
    i.job->state |= STATE_FORKED;
    close(pipe_stdout[1]);
    close(pipe_stderr[1]);
    for (char &c : task.cmdline) if (c == 0) c = ' ';
    if (i.pool)
      i.status = status_state.emplace(status_state.end(),
        task.cmdline, 0, i.start); // !!! task; budget
    if (!jobtable->imp->quiet && i.pool && (jobtable->imp->verbose || !i.internal)) {
      std::stringstream s;
      s << task.cmdline << std::endl;
      std::string out = s.str();
      status_write(2, out.data(), out.size());
    }
    jobtable->imp->tasks[i.pool].pop_front();
  }
}

bool JobTable::wait(WorkQueue &queue) {
  char buffer[4096];

  while (1) {
    if (refresh_needed) status_refresh();

    fd_set set;
    int nfds = 0;
    int njobs = 0;

    FD_ZERO(&set);
    for (auto &i : imp->table) {
      if (i.job) ++njobs;
      if (i.pipe_stdout != -1) {
        if (i.pipe_stdout >= nfds) nfds = i.pipe_stdout + 1;
        FD_SET(i.pipe_stdout, &set);
      }
      if (i.pipe_stderr != -1) {
        if (i.pipe_stderr >= nfds) nfds = i.pipe_stderr + 1;
        FD_SET(i.pipe_stderr, &set);
      }
    }
    if (njobs == 0) return false;

    int retval = pselect(nfds, &set, 0, 0, 0, &imp->sigset);
    if (retval == -1 && errno != EINTR) {
      perror("pselect");
      exit(1);
    }

    struct timeval now;
    gettimeofday(&now, 0);

    int done = 0;

    if (retval > 0) for (auto &i : imp->table) {
      if (i.pipe_stdout != -1 && FD_ISSET(i.pipe_stdout, &set)) {
        int got = read(i.pipe_stdout, buffer, sizeof(buffer));
        if (got <= 0) {
          close(i.pipe_stdout);
          i.pipe_stdout = -1;
          if (i.pool) i.status->stdout = false;
          i.job->state |= STATE_STDOUT;
          i.job->process(queue);
          ++done;
          if (imp->verbose && i.pool && !i.internal) {
            if (!i.stdout_buf.empty() && i.stdout_buf.back() != '\n')
              i.stdout_buf.push_back('\n');
            status_write(1, i.stdout_buf.data(), i.stdout_buf.size());
            i.stdout_buf.clear();
          }
        } else {
          i.job->db->save_output(i.job->job, 1, buffer, got, i.runtime(now));
          if (imp->verbose && i.pool && !i.internal) {
            i.stdout_buf.append(buffer, got);
            size_t dump = i.stdout_buf.rfind('\n');
            if (dump != std::string::npos) {
              status_write(1, i.stdout_buf.data(), dump+1);
              i.stdout_buf.erase(0, dump+1);
            }
          }
        }
      }
      if (i.pipe_stderr != -1 && FD_ISSET(i.pipe_stderr, &set)) {
        int got = read(i.pipe_stderr, buffer, sizeof(buffer));
        if (got <= 0) {
          close(i.pipe_stderr);
          i.pipe_stderr = -1;
          if (i.pool) i.status->stderr = false;
          i.job->state |= STATE_STDERR;
          i.job->process(queue);
          ++done;
          if (!imp->quiet && i.pool) { // print stderr also for internal
            if (!i.stderr_buf.empty() && i.stderr_buf.back() != '\n')
              i.stderr_buf.push_back('\n');
            status_write(2, i.stderr_buf.data(), i.stderr_buf.size());
            i.stderr_buf.clear();
          }
        } else {
          i.job->db->save_output(i.job->job, 2, buffer, got, i.runtime(now));
          if (!imp->quiet && i.pool) { // print stderr also for internal
            i.stderr_buf.append(buffer, got);
            size_t dump = i.stderr_buf.rfind('\n');
            if (dump != std::string::npos) {
              status_write(2, i.stderr_buf.data(), dump+1);
              i.stderr_buf.erase(0, dump+1);
            }
          }
        }
      }
    }

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
          i.pid = 0;
          i.job->state |= STATE_MERGED;
          i.job->status = code;
          i.job->runtime = i.runtime(now);
          i.job->process(queue);
        }
      }
    }

    if (done > 0) {
      launch(this);
      return true;
    }
  }
}

Job::Job(Database *db_, const std::string &dir, const std::string &stdin, const std::string &environ, const std::string &cmdline, bool keep_)
  : Value(type), db(db_), state(0), code(), pid(0), job(-1), keep(keep_), runtime(0), status(0)
{
  std::vector<uint64_t> codes;
  codes.push_back((long)type);
  Hash acc;
  hash3(dir    .data(), dir    .size(), acc); acc.push(codes);
  hash3(stdin  .data(), stdin  .size(), acc); acc.push(codes);
  hash3(environ.data(), environ.size(), acc); acc.push(codes);
  hash3(cmdline.data(), cmdline.size(), acc); acc.push(codes);
  hash3(codes.data(), 8*codes.size(), code);
}

static std::unique_ptr<Receiver> cast_jobresult(WorkQueue &queue, std::unique_ptr<Receiver> completion, const std::shared_ptr<Binding> &binding, const std::shared_ptr<Value> &value, Job **job) {
  if (value->type != Job::type) {
    Receiver::receive(queue, std::move(completion),
      std::make_shared<Exception>(value->to_str() + " is not a Job", binding));
    return std::unique_ptr<Receiver>();
  } else {
    *job = reinterpret_cast<Job*>(value.get());
    return completion;
  }
}

#define JOBRESULT(arg, i) 									\
  Job *arg;										\
  do {												\
    completion = cast_jobresult(queue, std::move(completion), binding, args[i], &arg);		\
    if (!completion) return;									\
  } while(0)

static PRIMTYPE(type_job_launch) {
  return args.size() == 6 &&
    args[0]->unify(Job::typeVar) &&
    args[1]->unify(Integer::typeVar) &&
    args[2]->unify(String::typeVar) &&
    args[3]->unify(String::typeVar) &&
    args[4]->unify(String::typeVar) &&
    args[5]->unify(String::typeVar) &&
    out->unify(Data::typeUnit);
}

static PRIMFN(prim_job_launch) {
  JobTable *jobtable = reinterpret_cast<JobTable*>(data);
  (void)data; // silence unused variable warning (EXPECT not called)
  REQUIRE (args.size() == 6, "prim_job_launch not called on 6 arguments");
  JOBRESULT(job, 0);

  int poolv = 0;
  if (args[1]->type == Integer::type) {
    Integer *pool = reinterpret_cast<Integer*>(args[1].get());
    if (mpz_cmp_si(pool->value, 0) < 0) {
      job->bad_launch = std::make_shared<Exception>("prim_job_launch: pool must be >= 0", binding);
    } else if (mpz_cmp_si(pool->value, POOLS) >= 0) {
      job->bad_launch = std::make_shared<Exception>("prim_job_launch: pool must be < POOLS", binding);
    } else {
      poolv = mpz_get_si(pool->value);
    }
  } else if (args[1]->type == Exception::type) {
    job->bad_launch = args[1];
  } else {
    job->bad_launch = std::make_shared<Exception>("prim_job_launch arg1 not an Integer", binding);
  }

  std::string *dirv = 0;
  if (args[2]->type == String::type) {
    String *dir = reinterpret_cast<String*>(args[2].get());
    dirv = &dir->value;
  } else if (args[2]->type == Exception::type) {
    job->bad_launch = args[2];
  } else {
    job->bad_launch = std::make_shared<Exception>("prim_job_launch arg2 not a String", binding);
  }

  std::string *stdinv = 0;
  if (args[3]->type == String::type) {
    String *stdin = reinterpret_cast<String*>(args[3].get());
    stdinv = &stdin->value;
  } else if (args[3]->type == Exception::type) {
    job->bad_launch = args[3];
  } else {
    job->bad_launch = std::make_shared<Exception>("prim_job_launch arg3 not a String", binding);
  }

  std::string *envv = 0;
  if (args[4]->type == String::type) {
    String *env = reinterpret_cast<String*>(args[4].get());
    envv = &env->value;
  } else if (args[4]->type == Exception::type) {
    job->bad_launch = args[4];
  } else {
    job->bad_launch = std::make_shared<Exception>("prim_job_launch arg4 not a String", binding);
  }

  std::string *cmdv = 0;
  if (args[5]->type == String::type) {
    String *cmd = reinterpret_cast<String*>(args[5].get());
    cmdv = &cmd->value;
  } else if (args[5]->type == Exception::type) {
    job->bad_launch = args[5];
  } else {
    job->bad_launch = std::make_shared<Exception>("prim_job_launch arg5 not a String", binding);
  }

  if (job->state != 0) {
    std::cerr << "ERROR: attempted to launch a FORKED job" << std::endl;
    exit(1);
  }

  if (job->bad_launch) {
    job->status = 128;
    job->runtime = 0;
    job->state = STATE_FORKED|STATE_STDOUT|STATE_STDERR|STATE_MERGED;
    job->process(queue);
  } else {
    jobtable->imp->tasks[poolv].emplace_back(
      std::dynamic_pointer_cast<Job>(args[0]), *dirv, *stdinv, *envv, *cmdv);
    launch(jobtable);
  }

  auto out = make_unit();
  RETURN(out);
}

static PRIMTYPE(type_job_virtual) {
  return args.size() == 5 &&
    args[0]->unify(Job::typeVar) &&
    args[1]->unify(String::typeVar) &&
    args[2]->unify(String::typeVar) &&
    args[3]->unify(Integer::typeVar) &&
    args[4]->unify(Double::typeVar) &&
    out->unify(Data::typeUnit);
}

static PRIMFN(prim_job_virtual) {
  (void)data; // silence unused variable warning (EXPECT not called)
  REQUIRE (args.size() == 5, "prim_job_virtual not called on 5 arguments");
  JOBRESULT(job, 0);

  if (job->state != 0) {
    std::cerr << "ERROR: attempted to virtualize a FORKED job" << std::endl;
    exit(1);
  }

  if (args[1]->type == String::type) {
    String *stdout = reinterpret_cast<String*>(args[1].get());
    if (!stdout->value.empty())
      job->db->save_output(job->job, 1, stdout->value.data(), stdout->value.size(), 0);
  } else if (args[1]->type == Exception::type) {
    job->bad_launch = args[1];
  } else {
    job->bad_launch = std::make_shared<Exception>("prim_job_virtual arg1 not a String", binding);
  }

  if (args[2]->type == String::type) {
    String *stderr = reinterpret_cast<String*>(args[2].get());
    if (!stderr->value.empty())
      job->db->save_output(job->job, 2, stderr->value.data(), stderr->value.size(), 0);
  } else if (args[2]->type == Exception::type) {
    job->bad_launch = args[2];
  } else {
    job->bad_launch = std::make_shared<Exception>("prim_job_virtual arg2 not a String", binding);
  }

  if (args[3]->type == Integer::type) {
    Integer *status = reinterpret_cast<Integer*>(args[3].get());
    job->status = mpz_get_si(status->value);
  } else if (args[3]->type == Exception::type) {
    job->bad_launch = args[3];
    job->status = 128;
  } else {
    job->bad_launch = std::make_shared<Exception>("prim_job_virtual arg3 not an Integer", binding);
    job->status = 128;
  }

  if (args[4]->type == Double::type) {
    Double *runtime = reinterpret_cast<Double*>(args[4].get());
    job->runtime = runtime->value;
  } else if (args[4]->type == Exception::type) {
    job->bad_launch = args[4];
    job->runtime = 0;
  } else {
    job->bad_launch = std::make_shared<Exception>("prim_job_virtual arg4 not a Double", binding);
    job->runtime = 0;
  }

  job->state = STATE_FORKED|STATE_STDOUT|STATE_STDERR|STATE_MERGED;
  job->process(queue);

  auto out = make_unit();
  RETURN(out);
}

static PRIMTYPE(type_job_create) {
  return args.size() == 6 &&
    args[0]->unify(String::typeVar) &&
    args[1]->unify(String::typeVar) &&
    args[2]->unify(String::typeVar) &&
    args[3]->unify(String::typeVar) &&
    args[4]->unify(String::typeVar) &&
    args[5]->unify(Integer::typeVar) &&
    out->unify(Job::typeVar);
}

static PRIMFN(prim_job_create) {
  JobTable *jobtable = reinterpret_cast<JobTable*>(data);
  EXPECT(6);
  STRING(dir, 0);
  STRING(stdin, 1);
  STRING(env, 2);
  STRING(cmd, 3);
  STRING(visible, 4);
  INTEGER(keep, 5);

  std::stringstream stack;
  for (auto &i : binding->stack_trace()) stack << i << std::endl;
  auto out = std::make_shared<Job>(
    jobtable->imp->db,
    dir->value,
    stdin->value,
    env->value,
    cmd->value,
    mpz_cmp_si(keep->value,0));

  out->db->insert_job(
    dir->value,
    stdin->value,
    env->value,
    cmd->value,
    stack.str(),
    &out->job);

  RETURN(out);
}

static std::shared_ptr<Value> convert_tree(std::vector<FileReflection> &&files) {
  std::vector<std::shared_ptr<Value> > vals;
  vals.reserve(files.size());
  for (auto &i : files)
    vals.emplace_back(make_tuple2(
      std::static_pointer_cast<Value>(std::make_shared<String>(std::move(i.path))),
      std::static_pointer_cast<Value>(std::make_shared<String>(std::move(i.hash)))));
  return make_list(std::move(vals));
}

static PRIMTYPE(type_job_cache) {
  TypeVar spair;
  TypeVar plist;
  TypeVar jlist;
  TypeVar pair;
  Data::typePair.clone(spair);
  Data::typeList.clone(plist);
  Data::typeList.clone(jlist);
  Data::typePair.clone(pair);
  spair[0].unify(String::typeVar);
  spair[1].unify(String::typeVar);
  plist[0].unify(spair);
  jlist[0].unify(Job::typeVar);
  pair[0].unify(jlist);
  pair[1].unify(plist);
  return args.size() == 5 &&
    args[0]->unify(String::typeVar) &&
    args[1]->unify(String::typeVar) &&
    args[2]->unify(String::typeVar) &&
    args[3]->unify(String::typeVar) &&
    args[4]->unify(String::typeVar) &&
    out->unify(pair);
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
  std::vector<FileReflection> files;
  bool cached = jobtable->imp->db->reuse_job(
    dir->value,
    stdin->value,
    env->value,
    cmd->value,
    visible->value,
    jobtable->imp->check,
    job,
    files);

  std::vector<std::shared_ptr<Value> > jobs;
  if (cached && !jobtable->imp->check) {
    auto out = std::make_shared<Job>(jobtable->imp->db, dir->value, stdin->value, env->value, cmd->value, true);
    out->state = STATE_FORKED|STATE_STDOUT|STATE_STDERR|STATE_MERGED|STATE_FINISHED;
    out->job = job;
    jobs.emplace_back(std::move(out));
  }

  auto out = make_tuple2(make_list(std::move(jobs)), convert_tree(std::move(files)));
  RETURN(out);
}

void Job::process(WorkQueue &queue) {
  if ((state & STATE_STDOUT) && q_stdout) {
    auto out = bad_launch ? bad_launch : std::make_shared<String>(db->get_output(job, 1));
    std::unique_ptr<Receiver> iter, next;
    for (iter = std::move(q_stdout); iter; iter = std::move(next)) {
      next = std::move(iter->next);
      Receiver::receive(queue, std::move(iter), out);
    }
    q_stdout.reset();
  }

  if ((state & STATE_STDERR) && q_stderr) {
    auto out = bad_launch ? bad_launch : std::make_shared<String>(db->get_output(job, 2));
    std::unique_ptr<Receiver> iter, next;
    for (iter = std::move(q_stderr); iter; iter = std::move(next)) {
      next = std::move(iter->next);
      Receiver::receive(queue, std::move(iter), out);
    }
    q_stderr.reset();
  }

  if ((state & STATE_MERGED) && q_merge) {
    auto out = bad_launch ? bad_launch : std::make_shared<Integer>(status);
    std::unique_ptr<Receiver> iter, next;
    for (iter = std::move(q_merge); iter; iter = std::move(next)) {
      next = std::move(iter->next);
      Receiver::receive(queue, std::move(iter), out);
    }
    q_merge.reset();
  }

  if ((state & STATE_FINISHED) && q_inputs) {
    auto files = db->get_tree(1, job);
    auto out = bad_finish ? bad_finish : convert_tree(std::move(files));
    std::unique_ptr<Receiver> iter, next;
    for (iter = std::move(q_inputs); iter; iter = std::move(next)) {
      next = std::move(iter->next);
      Receiver::receive(queue, std::move(iter), out);
    }
    q_inputs.reset();
  }

  if ((state & STATE_FINISHED) && q_outputs) {
    auto files = db->get_tree(2, job);
    auto out = bad_finish ? bad_finish : convert_tree(std::move(files));
    std::unique_ptr<Receiver> iter, next;
    for (iter = std::move(q_outputs); iter; iter = std::move(next)) {
      next = std::move(iter->next);
      Receiver::receive(queue, std::move(iter), out);
    }
    q_outputs.reset();
  }
}

static PRIMTYPE(type_job_output) {
  return args.size() == 2 &&
    args[0]->unify(Job::typeVar) &&
    args[1]->unify(Integer::typeVar) &&
    out->unify(String::typeVar);
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

static PRIMTYPE(type_job_kill) {
  return args.size() == 2 &&
    args[0]->unify(Job::typeVar) &&
    args[1]->unify(Integer::typeVar) &&
    out->unify(Integer::typeVar);
}

static PRIMFN(prim_job_kill) {
  EXPECT(2);
  JOBRESULT(arg0, 0);
  INTEGER(arg1, 1);
  REQUIRE(mpz_cmp_si(arg1->value, 256) < 0, "signal too large (> 256)");
  REQUIRE(mpz_cmp_si(arg1->value, 0) >= 0, "signal too small (< 0)");
  int sig = mpz_get_si(arg1->value);
  if (sig && (arg0->state & STATE_FORKED) && !(arg0->state & STATE_MERGED))
    kill(arg0->pid, sig);
  completion->next = std::move(arg0->q_merge);
  arg0->q_merge = std::move(completion);
  arg0->process(queue);
}

static PRIMTYPE(type_job_tree) {
  TypeVar list;
  TypeVar pair;
  Data::typeList.clone(list);
  Data::typePair.clone(pair);
  list[0].unify(pair);
  pair[0].unify(String::typeVar);
  pair[1].unify(String::typeVar);
  return args.size() == 2 &&
    args[0]->unify(Job::typeVar) &&
    args[1]->unify(Integer::typeVar) &&
    out->unify(list);
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

static PRIMTYPE(type_job_finish) {
  return args.size() == 3 &&
    args[0]->unify(Job::typeVar) &&
    args[1]->unify(String::typeVar) &&
    args[2]->unify(String::typeVar) &&
    out->unify(Data::typeUnit);
}

static PRIMFN(prim_job_finish) {
  (void)data; // silence unused variable warning (EXPECT not called)
  REQUIRE (args.size() == 3, "prim_job_finish not called on 3 arguments");
  JOBRESULT(job, 0);

  if (!(job->state & STATE_MERGED)) {
    // fatal because it means the queue will not converge
    std::cerr << "ERROR: attempted to finish an unmerged job" << std::endl;
    exit(1);
  }
  if ((job->state & STATE_FINISHED)) {
    std::cerr << "ERROR: attempted to finish a finished job" << std::endl;
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

  bool keep = !job->bad_launch && !job->bad_finish && job->keep;
  job->db->finish_job(job->job, *inputs, *outputs, keep, job->status, job->runtime);
  job->state |= STATE_FINISHED;
  job->process(queue);

  auto out = make_unit();
  RETURN(out);
}

static PRIMTYPE(type_add_hash) {
  return args.size() == 2 &&
    args[0]->unify(String::typeVar) &&
    args[1]->unify(String::typeVar) &&
    out->unify(String::typeVar);
}

#ifdef __APPLE__
#define st_mtim st_mtimespec
#endif

static long stat_mod_ns(const std::string &file) {
  struct stat sbuf;
  stat(file.c_str(), &sbuf);
  long modified = sbuf.st_mtim.tv_sec;
  modified *= 1000000000L;
  modified += sbuf.st_mtim.tv_nsec;
  return modified;
}

static PRIMFN(prim_add_hash) {
  JobTable *jobtable = reinterpret_cast<JobTable*>(data);
  EXPECT(2);
  STRING(file, 0);
  STRING(hash, 1);
  jobtable->imp->db->add_hash(file->value, hash->value, stat_mod_ns(file->value));
  RETURN(args[0]);
}

static PRIMTYPE(type_get_hash) {
  return args.size() == 1 &&
    args[0]->unify(String::typeVar) &&
    out->unify(String::typeVar);
}

static PRIMFN(prim_get_hash) {
  JobTable *jobtable = reinterpret_cast<JobTable*>(data);
  EXPECT(1);
  STRING(file, 0);
  std::string hash = jobtable->imp->db->get_hash(file->value, stat_mod_ns(file->value));
  auto out = std::make_shared<String>(std::move(hash));
  RETURN(out);
}

static PRIMTYPE(type_get_modtime) {
  return args.size() == 1 &&
    args[0]->unify(String::typeVar) &&
    out->unify(Integer::typeVar);
}

static PRIMFN(prim_get_modtime) {
  EXPECT(1);
  STRING(file, 0);
  auto out = std::make_shared<Integer>(stat_mod_ns(file->value));
  RETURN(out);
}

static bool check_exec(const char *tok, size_t len, const std::string &exec, std::string &out) {
  out.assign(tok, len);
  out += "/";
  out += exec;
  return access(out.c_str(), X_OK) == 0;
}

static PRIMTYPE(type_search_path) {
  return args.size() == 2 &&
    args[0]->unify(String::typeVar) &&
    args[1]->unify(String::typeVar) &&
    out->unify(String::typeVar);
}

static PRIMFN(prim_search_path) {
  EXPECT(2);
  STRING(path, 0);
  STRING(exec, 1);

  auto out = std::make_shared<String>("");
  const char *tok = path->value.c_str();
  const char *end = tok + path->value.size();
  for (const char *scan = tok; scan != end; ++scan) {
    if (*scan == ':' && scan != tok) {
      if (check_exec(tok, scan-tok, exec->value, out->value)) RETURN(out);
      std::string path(tok, scan-tok);
      path += "/";
      path += exec->value;
      tok = scan+1;
    }
  }

  if (check_exec(tok, end-tok, exec->value, out->value)) RETURN(out);
  RAISE(exec->value + " not found in " + path->value);
}

void prim_register_job(JobTable *jobtable, PrimMap &pmap) {
  pmap.emplace("job_create", PrimDesc(prim_job_create, type_job_create, jobtable));
  pmap.emplace("job_launch", PrimDesc(prim_job_launch, type_job_launch, jobtable));
  pmap.emplace("job_virtual",PrimDesc(prim_job_virtual,type_job_virtual,jobtable));
  pmap.emplace("job_cache",  PrimDesc(prim_job_cache,  type_job_cache,  jobtable));
  pmap.emplace("job_output", PrimDesc(prim_job_output, type_job_output));
  pmap.emplace("job_kill",   PrimDesc(prim_job_kill,   type_job_kill));
  pmap.emplace("job_tree",   PrimDesc(prim_job_tree,   type_job_tree));
  pmap.emplace("job_finish", PrimDesc(prim_job_finish, type_job_finish));
  pmap.emplace("add_hash",   PrimDesc(prim_add_hash,   type_add_hash,   jobtable));
  pmap.emplace("get_hash",   PrimDesc(prim_get_hash,   type_get_hash,   jobtable));
  pmap.emplace("get_modtime",PrimDesc(prim_get_modtime,type_get_modtime));
  pmap.emplace("search_path",PrimDesc(prim_search_path,type_search_path));
}
