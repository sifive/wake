#include "job.h"
#include "prim.h"
#include "value.h"
#include "heap.h"
#include "database.h"
#include "location.h"
#include "sources.h"
#include "status.h"
#include "thunk.h"
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/resource.h>
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
// How many times to SIGTERM a process before SIGKILL
#define TERM_ATTEMPTS 6
// How long between first and second SIGTERM attempt (exponentially increasing)
#define TERM_BASE_GAP_MS 100

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
  std::shared_ptr<Value> bad_launch;
  std::shared_ptr<Value> bad_finish;
  Usage record;  // retrieved from DB (user-facing usage)
  Usage predict; // prediction of Runners given record (used by scheduler)
  Usage reality; // actual measured local usage
  Usage report;  // usage to save into DB + report in Job API

  // There are 4 distinct wait queues for jobs
  std::unique_ptr<Receiver> q_stdout;  // waken once stdout closed
  std::unique_ptr<Receiver> q_stderr;  // waken once stderr closed
  std::unique_ptr<Receiver> q_reality; // waken once job merged (reality available)
  std::unique_ptr<Receiver> q_inputs;  // waken once job finished (inputs+outputs+report available)
  std::unique_ptr<Receiver> q_outputs; // waken once job finished (inputs+outputs+report available)
  std::unique_ptr<Receiver> q_report;  // waken once job finished (inputs+outputs+report available)

  static const TypeDescriptor type;
  static TypeVar typeVar;
  Job(Database *db_, const std::string &dir, const std::string &stdin, const std::string &environ, const std::string &cmdline, bool keep);

  void format(std::ostream &os, FormatState &state) const;
  TypeVar &getType();
  Hash hash() const;

  void process(WorkQueue &queue); // Run commands based on state
};

const TypeDescriptor Job::type("Job");

void Job::format(std::ostream &os, FormatState &state) const {
  if (APP_PRECEDENCE < state.p()) os << "(";
  os << "Job " << job;
  if (APP_PRECEDENCE < state.p()) os << ")";
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
  sigset_t block; // signals that can race with pselect()
  Database *db;
  bool verbose;
  bool quiet;
  bool check;
};

static volatile bool child_ready = false;
static volatile bool exit_asap = false;

static void handle_SIGCHLD(int sig) {
  (void)sig;
  child_ready = true;
}

static void handle_exit(int sig) {
  (void)sig;
  exit_asap = true;
}

bool JobTable::exit_now() {
  return exit_asap;
}

JobTable::JobTable(Database *db, int max_jobs, bool verbose, bool quiet, bool check) : imp(new JobTable::detail) {
  imp->table.resize(max_jobs*POOLS);
  imp->tasks.resize(POOLS);
  imp->verbose = verbose;
  imp->quiet = quiet;
  imp->check = check;
  imp->db = db;
  sigemptyset(&imp->block);

  for (unsigned i = 0; i < imp->table.size(); ++i)
    imp->table[i].pool = i / max_jobs;

  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));

  // Ignore these signals
  sa.sa_handler = SIG_IGN;
  sa.sa_flags = SA_RESTART;
  sigaction(SIGPIPE, &sa, 0);
  sigaction(SIGUSR1, &sa, 0);
  sigaction(SIGUSR2, &sa, 0);

  // SIGCHLD interrupts pselect()
  sa.sa_handler = handle_SIGCHLD;
  sa.sa_flags = SA_NOCLDSTOP;
  // no SA_RESTART, because we need to interrupt pselect() portably
  // to protect other syscalls, we keep SIGCHLD blocked except in pselect()
  sigaddset(&imp->block, SIGCHLD);
  sigprocmask(SIG_BLOCK, &imp->block, 0);
  sigaction(SIGCHLD, &sa, 0);

  // These signals cause wake to exit cleanly
  sa.sa_handler = handle_exit;
  sa.sa_flags = 0; // no SA_RESTART, because we want to terminate blocking calls
  sigaction(SIGHUP,  &sa, 0);
  sigaction(SIGINT,  &sa, 0);
  sigaction(SIGQUIT, &sa, 0);
  sigaction(SIGTERM, &sa, 0);
  sigaction(SIGXCPU, &sa, 0);
  sigaction(SIGXFSZ, &sa, 0);

  // Add to the set of signals we need to interrupt pselect()
  sigaddset(&imp->block, SIGHUP);
  sigaddset(&imp->block, SIGINT);
  sigaddset(&imp->block, SIGQUIT);
  sigaddset(&imp->block, SIGTERM);
  sigaddset(&imp->block, SIGXCPU);
  sigaddset(&imp->block, SIGXFSZ);

  // These are handled in status.cpp
  sigaddset(&imp->block, SIGALRM);
  sigaddset(&imp->block, SIGWINCH);
}

static struct timeval mytimersub(struct timeval a, struct timeval b) {
  struct timeval out;
  out.tv_sec = a.tv_sec - b.tv_sec;
  out.tv_usec = a.tv_usec - b.tv_usec;
  if (out.tv_usec < 0) {
    --out.tv_sec;
    out.tv_usec += 1000000;
  }
  return out;
}

static struct timeval mytimerdouble(struct timeval a) {
  a.tv_sec <<= 1;
  a.tv_usec <<= 1;
  if (a.tv_usec > 1000000) {
    ++a.tv_sec;
    a.tv_usec -= 1000000;
  }
  return a;
}

JobTable::~JobTable() {
  // Disable the status refresh signal
  struct itimerval timer;
  memset(&timer, 0, sizeof(timer));
  setitimer(ITIMER_REAL, &timer, 0);

  // SIGTERM strategy is to double the gap between termination attempts every retry
  struct timeval limit;
  limit.tv_sec  = TERM_BASE_GAP_MS / 1000;
  limit.tv_usec = (TERM_BASE_GAP_MS % 1000) * 1000;

  // Try to kill children gently first, once every second
  bool children = true;
  for (int retry = 0; children && retry < TERM_ATTEMPTS; ++retry, limit = mytimerdouble(limit)) {
    children = false;

    // Send every child SIGTERM
    for (auto &i : imp->table) {
      if (i.pid == 0) continue;
      children = true;
      kill(i.pid, SIGTERM);
    }

    // Reap children for one second; exit early if none remain
    struct timeval start, now, remain;
    struct timespec timeout;
    gettimeofday(&start, 0);
    for (now = start; children && (remain = mytimersub(limit, mytimersub(now, start))).tv_sec >= 0; gettimeofday(&now, 0)) {
      // Block racey signals between here and pselect()
      sigset_t saved;
      sigprocmask(SIG_BLOCK, &imp->block, &saved);
      sigdelset(&saved, SIGCHLD);

      // Continue waiting for the full second
      timeout.tv_sec = 0;
      timeout.tv_nsec = remain.tv_usec * 1000;

      // Sleep until timeout or a signal arrives
      if (!child_ready) pselect(0, 0, 0, 0, &timeout, &saved);

      // Restore signals
      child_ready = false;
      sigaddset(&saved, SIGCHLD);
      sigprocmask(SIG_SETMASK, &saved, 0);

      pid_t pid;
      int status;
      while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        if (WIFSTOPPED(status)) continue;

        children = false;
        for (auto &i : imp->table) {
          if (i.pid == pid) i.pid = 0;
          if (i.pid != 0) children = true;
        }
      }
    }
  }

  // Force children to die
  for (auto &i : imp->table) {
    if (i.pid == 0) continue;
    std::cerr << "Force killing " << i.pid << " after " << TERM_ATTEMPTS << " attempts with SIGTERM" << std::endl;
    kill(i.pid, SIGKILL);
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
    std::stringstream prelude;
    prelude << find_execpath() << "/../lib/wake/shim-wake" << '\0'
      << (task.stdin.empty() ? "/dev/null" : task.stdin.c_str()) << '\0'
      << std::to_string(pipe_stdout[1]) << '\0'
      << std::to_string(pipe_stderr[1]) << '\0'
      << task.dir << '\0';
    std::string shim = prelude.str() + task.cmdline;
    auto cmdline = split_null(shim);
    auto environ = split_null(task.environ);

    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGCHLD);
    sigprocmask(SIG_UNBLOCK, &set, 0);
    pid_t pid = vfork();
    if (pid == 0) {
      execve(cmdline[0], cmdline, environ);
      _exit(127);
    }
    sigprocmask(SIG_BLOCK, &set, 0);

    delete [] cmdline;
    delete [] environ;
    i.job->pid = i.pid = pid;
    i.job->state |= STATE_FORKED;
    close(pipe_stdout[1]);
    close(pipe_stderr[1]);
    for (char &c : task.cmdline) if (c == 0) c = ' ';
    if (i.pool) {
      double predict = i.job->predict.status == 0 ? i.job->predict.runtime : 0;
      i.status = status_state.emplace(status_state.end(), task.cmdline, predict, i.start);
    }
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
  struct timespec nowait;
  memset(&nowait, 0, sizeof(nowait));

  bool compute = false;
  while (!exit_now()) {
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
    if (njobs == 0) break;

    // Block all signals we expect to interrupt pselect
    sigset_t saved;
    sigprocmask(SIG_BLOCK, &imp->block, &saved);
    sigdelset(&saved, SIGCHLD);

    // Check for all signals that are now blocked
    struct timespec *timeout = 0;
    if (child_ready) timeout = &nowait;
    if (exit_now()) timeout = &nowait;

#if !defined(__linux__)
    // In case SIGALRM with SA_RESTART doesn't stop pselect
    if (!timeout) {
      struct itimerval timer;
      struct timespec alarm;
      getitimer(ITIMER_REAL, &timer);
      if (timer.it_value.tv_sec || timer.it_value.tv_usec) {
        alarm.tv_sec = timer.it_value.tv_sec;
        alarm.tv_nsec = (10000 + timer.it_value.tv_usec) * 1000;
        timeout = &alarm;
      }
    }
#endif
    status_refresh();

    // Wait for a status change, with signals atomically unblocked in pselect
    int retval = pselect(nfds, &set, 0, 0, timeout, &saved);

    // Restore signal mask
    sigaddset(&saved, SIGCHLD);
    sigprocmask(SIG_SETMASK, &saved, 0);

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
        if (got == 0 || (got < 0 && errno != EINTR)) {
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
        if (got == 0 || (got < 0 && errno != EINTR)) {
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
    struct rusage rusage;
    child_ready = false;
    while ((pid = wait4(-1, &status, WNOHANG, &rusage)) > 0) {
      if (WIFSTOPPED(status)) continue;

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
          if (i.pool) i.status->merged = true;
          i.job->state |= STATE_MERGED;
          i.job->reality.found    = true;
          i.job->reality.status   = code;
          i.job->reality.runtime  = i.runtime(now);
          i.job->reality.cputime  = (rusage.ru_utime.tv_sec  + rusage.ru_stime.tv_sec) +
                                    (rusage.ru_utime.tv_usec + rusage.ru_stime.tv_usec)/1000000.0;
          i.job->reality.membytes = rusage.ru_maxrss;
          i.job->reality.ibytes   = rusage.ru_inblock * UINT64_C(512);
          i.job->reality.obytes   = rusage.ru_oublock * UINT64_C(512);
          i.job->process(queue);
        }
      }
    }

    if (done > 0) {
      launch(this);
      compute = true;
      break;
    }
  }

  return compute;
}

Job::Job(Database *db_, const std::string &dir, const std::string &stdin, const std::string &environ, const std::string &cmdline, bool keep_)
  : Value(&type), db(db_), state(0), code(), pid(0), job(-1), keep(keep_)
{
  std::vector<uint64_t> codes;
  type.hashcode.push(codes);
  Hash(dir).push(codes);
  Hash(stdin).push(codes);
  Hash(environ).push(codes);
  Hash(cmdline).push(codes);
  code = Hash(codes);
}

static std::unique_ptr<Receiver> cast_jobresult(WorkQueue &queue, std::unique_ptr<Receiver> completion, const std::shared_ptr<Binding> &binding, const std::shared_ptr<Value> &value, Job **job) {
  if (value->type != &Job::type) {
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

static void parse_usage(Usage *usage, std::shared_ptr<Value> &bad, const std::shared_ptr<Binding> &binding, std::shared_ptr<Value> *args) {
  if (args[0]->type == &Integer::type) {
    Integer *status = reinterpret_cast<Integer*>(args[0].get());
    usage->status = mpz_get_si(status->value);
  } else if (args[0]->type == &Exception::type) {
    bad = args[0];
    usage->status = 128;
  } else {
    bad = std::make_shared<Exception>("prim_job status not an Integer", binding);
    usage->status = 128;
  }

  if (args[1]->type == &Double::type) {
    Double *runtime = reinterpret_cast<Double*>(args[1].get());
    usage->runtime = runtime->value;
  } else if (args[1]->type == &Exception::type) {
    bad = args[1];
    usage->runtime = 0;
  } else {
    bad = std::make_shared<Exception>("prim_job runtime not a Double", binding);
    usage->runtime = 0;
  }

  if (args[2]->type == &Double::type) {
    Double *cputime = reinterpret_cast<Double*>(args[2].get());
    usage->cputime = cputime->value;
  } else if (args[2]->type == &Exception::type) {
    bad = args[2];
    usage->cputime = 0;
  } else {
    bad = std::make_shared<Exception>("prim_job cputime not a Double", binding);
    usage->cputime = 0;
  }

  if (args[3]->type == &Integer::type) {
    Integer *membytes = reinterpret_cast<Integer*>(args[3].get());
    usage->membytes = mpz_get_si(membytes->value);
  } else if (args[3]->type == &Exception::type) {
    bad = args[3];
    usage->membytes = 0;
  } else {
    bad = std::make_shared<Exception>("prim_job membytes not an Integer", binding);
    usage->membytes = 0;
  }

  if (args[4]->type == &Integer::type) {
    Integer *ibytes = reinterpret_cast<Integer*>(args[4].get());
    usage->ibytes = mpz_get_si(ibytes->value);
  } else if (args[4]->type == &Exception::type) {
    bad = args[4];
    usage->ibytes = 0;
  } else {
    bad = std::make_shared<Exception>("prim_job ibytes not an Integer", binding);
    usage->ibytes = 0;
  }

  if (args[5]->type == &Integer::type) {
    Integer *obytes = reinterpret_cast<Integer*>(args[5].get());
    usage->obytes = mpz_get_si(obytes->value);
  } else if (args[5]->type == &Exception::type) {
    bad = args[5];
    usage->obytes = 0;
  } else {
    bad = std::make_shared<Exception>("prim_job obytes not an Integer", binding);
    usage->obytes = 0;
  }
}

static PRIMTYPE(type_job_launch) {
  return args.size() == 12 &&
    args[0]->unify(Job::typeVar) &&
    args[1]->unify(Integer::typeVar) &&
    args[2]->unify(String::typeVar) &&
    args[3]->unify(String::typeVar) &&
    args[4]->unify(String::typeVar) &&
    args[5]->unify(String::typeVar) &&
    args[6]->unify(Integer::typeVar) &&
    args[7]->unify(Double::typeVar) &&
    args[8]->unify(Double::typeVar) &&
    args[9]->unify(Integer::typeVar) &&
    args[10]->unify(Integer::typeVar) &&
    args[11]->unify(Integer::typeVar) &&
    out->unify(Data::typeUnit);
}

static PRIMFN(prim_job_launch) {
  JobTable *jobtable = reinterpret_cast<JobTable*>(data);
  (void)data; // silence unused variable warning (EXPECT not called)
  REQUIRE (args.size() == 12, "prim_job_launch not called on 12 arguments");
  JOBRESULT(job, 0);

  int poolv = 0;
  if (args[1]->type == &Integer::type) {
    Integer *pool = reinterpret_cast<Integer*>(args[1].get());
    if (mpz_cmp_si(pool->value, 0) < 0) {
      job->bad_launch = std::make_shared<Exception>("prim_job_launch: pool must be >= 0", binding);
    } else if (mpz_cmp_si(pool->value, POOLS) >= 0) {
      job->bad_launch = std::make_shared<Exception>("prim_job_launch: pool must be < POOLS", binding);
    } else {
      poolv = mpz_get_si(pool->value);
    }
  } else if (args[1]->type == &Exception::type) {
    job->bad_launch = args[1];
  } else {
    job->bad_launch = std::make_shared<Exception>("prim_job_launch arg1 not an Integer", binding);
  }

  std::string *dirv = 0;
  if (args[2]->type == &String::type) {
    String *dir = reinterpret_cast<String*>(args[2].get());
    dirv = &dir->value;
  } else if (args[2]->type == &Exception::type) {
    job->bad_launch = args[2];
  } else {
    job->bad_launch = std::make_shared<Exception>("prim_job_launch arg2 not a String", binding);
  }

  std::string *stdinv = 0;
  if (args[3]->type == &String::type) {
    String *stdin = reinterpret_cast<String*>(args[3].get());
    stdinv = &stdin->value;
  } else if (args[3]->type == &Exception::type) {
    job->bad_launch = args[3];
  } else {
    job->bad_launch = std::make_shared<Exception>("prim_job_launch arg3 not a String", binding);
  }

  std::string *envv = 0;
  if (args[4]->type == &String::type) {
    String *env = reinterpret_cast<String*>(args[4].get());
    envv = &env->value;
  } else if (args[4]->type == &Exception::type) {
    job->bad_launch = args[4];
  } else {
    job->bad_launch = std::make_shared<Exception>("prim_job_launch arg4 not a String", binding);
  }

  std::string *cmdv = 0;
  if (args[5]->type == &String::type) {
    String *cmd = reinterpret_cast<String*>(args[5].get());
    cmdv = &cmd->value;
  } else if (args[5]->type == &Exception::type) {
    job->bad_launch = args[5];
  } else {
    job->bad_launch = std::make_shared<Exception>("prim_job_launch arg5 not a String", binding);
  }

  if (job->state != 0) {
    std::cerr << "ERROR: attempted to launch a FORKED job" << std::endl;
    exit(1);
  }

  parse_usage(&job->predict, job->bad_launch, binding, args.data()+6);
  job->predict.found = true;

  if (job->bad_launch) {
    job->reality.found    = true;
    job->reality.status   = 128;
    job->reality.runtime  = 0;
    job->reality.cputime  = 0;
    job->reality.membytes = 0;
    job->reality.ibytes   = 0;
    job->reality.obytes   = 0;
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
  return args.size() == 9 &&
    args[0]->unify(Job::typeVar) &&
    args[1]->unify(String::typeVar) &&
    args[2]->unify(String::typeVar) &&
    args[3]->unify(Integer::typeVar) &&
    args[4]->unify(Double::typeVar) &&
    args[5]->unify(Double::typeVar) &&
    args[6]->unify(Integer::typeVar) &&
    args[7]->unify(Integer::typeVar) &&
    args[8]->unify(Integer::typeVar) &&
    out->unify(Data::typeUnit);
}

static PRIMFN(prim_job_virtual) {
  (void)data; // silence unused variable warning (EXPECT not called)
  REQUIRE (args.size() == 9, "prim_job_virtual not called on 8 arguments");
  JOBRESULT(job, 0);

  if (job->state != 0) {
    std::cerr << "ERROR: attempted to virtualize a FORKED job" << std::endl;
    exit(1);
  }

  if (args[1]->type == &String::type) {
    String *stdout = reinterpret_cast<String*>(args[1].get());
    if (!stdout->value.empty())
      job->db->save_output(job->job, 1, stdout->value.data(), stdout->value.size(), 0);
  } else if (args[1]->type == &Exception::type) {
    job->bad_launch = args[1];
  } else {
    job->bad_launch = std::make_shared<Exception>("prim_job_virtual arg1 not a String", binding);
  }

  if (args[2]->type == &String::type) {
    String *stderr = reinterpret_cast<String*>(args[2].get());
    if (!stderr->value.empty())
      job->db->save_output(job->job, 2, stderr->value.data(), stderr->value.size(), 0);
  } else if (args[2]->type == &Exception::type) {
    job->bad_launch = args[2];
  } else {
    job->bad_launch = std::make_shared<Exception>("prim_job_virtual arg2 not a String", binding);
  }

  parse_usage(&job->predict, job->bad_launch, binding, args.data()+3);
  job->predict.found = true;
  job->reality = job->predict;

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

  out->record = jobtable->imp->db->predict_job(out->code.data[0]);

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
  Usage reuse = jobtable->imp->db->reuse_job(
    dir->value,
    stdin->value,
    env->value,
    cmd->value,
    visible->value,
    jobtable->imp->check,
    job,
    files);

  std::vector<std::shared_ptr<Value> > jobs;
  if (reuse.found && !jobtable->imp->check) {
    auto out = std::make_shared<Job>(jobtable->imp->db, dir->value, stdin->value, env->value, cmd->value, true);
    out->state = STATE_FORKED|STATE_STDOUT|STATE_STDERR|STATE_MERGED|STATE_FINISHED;
    out->job = job;
    out->record  = reuse;
    // predict + reality unusued since Job not run
    out->report  = reuse;
    jobs.emplace_back(std::move(out));
  }

  auto out = make_tuple2(make_list(std::move(jobs)), convert_tree(std::move(files)));
  RETURN(out);
}

static std::shared_ptr<Value> make_usage(const Usage &usage) {
  return
    make_tuple2(
      make_tuple2(
        std::make_shared<Integer>(usage.status),
        std::make_shared<Double>(usage.runtime)),
      make_tuple2(
        make_tuple2(
          std::make_shared<Double>(usage.cputime),
          std::make_shared<Integer>(usage.membytes)),
        make_tuple2(
          std::make_shared<Integer>(usage.ibytes),
          std::make_shared<Integer>(usage.obytes))));
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

  if ((state & STATE_MERGED) && q_reality) {
    auto out = bad_launch ? bad_launch : make_usage(reality);
    std::unique_ptr<Receiver> iter, next;
    for (iter = std::move(q_reality); iter; iter = std::move(next)) {
      next = std::move(iter->next);
      Receiver::receive(queue, std::move(iter), out);
    }
    q_reality.reset();
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

  if ((state & STATE_FINISHED) && q_report) {
    auto out = bad_launch ? bad_launch : make_usage(report);
    std::unique_ptr<Receiver> iter, next;
    for (iter = std::move(q_report); iter; iter = std::move(next)) {
      next = std::move(iter->next);
      Receiver::receive(queue, std::move(iter), out);
    }
    q_report.reset();
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
    out->unify(Data::typeUnit);
}

static PRIMFN(prim_job_kill) {
  EXPECT(2);
  JOBRESULT(arg0, 0);
  INTEGER(arg1, 1);
  REQUIRE(mpz_cmp_si(arg1->value, 256) < 0, "signal too large (> 256)");
  REQUIRE(mpz_cmp_si(arg1->value, 0) > 0, "signal too small (<= 0)");
  int sig = mpz_get_si(arg1->value);
  if ((arg0->state & STATE_FORKED) && !(arg0->state & STATE_MERGED))
    kill(arg0->pid, sig);

  auto out = make_unit();
  RETURN(out);
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
  return args.size() == 9 &&
    args[0]->unify(Job::typeVar) &&
    args[1]->unify(String::typeVar) &&
    args[2]->unify(String::typeVar) &&
    args[3]->unify(Integer::typeVar) &&
    args[4]->unify(Double::typeVar) &&
    args[5]->unify(Double::typeVar) &&
    args[6]->unify(Integer::typeVar) &&
    args[7]->unify(Integer::typeVar) &&
    args[8]->unify(Integer::typeVar) &&
    out->unify(Data::typeUnit);
}

static PRIMFN(prim_job_finish) {
  (void)data; // silence unused variable warning (EXPECT not called)
  REQUIRE (args.size() == 9, "prim_job_finish not called on 9 arguments");
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

  if (args[1]->type == &String::type) {
    inputs = &reinterpret_cast<String*>(args[1].get())->value;
  } else if (args[1]->type == &Exception::type) {
    job->bad_finish = args[1];
    inputs = &empty;
  } else {
    job->bad_finish = std::make_shared<Exception>("prim_job_finish arg1 not a string", binding);
    inputs = &empty;
  }

  if (args[2]->type == &String::type) {
    outputs = &reinterpret_cast<String*>(args[2].get())->value;
  } else if (args[2]->type == &Exception::type) {
    job->bad_finish = args[2];
    outputs = &empty;
  } else {
    job->bad_finish = std::make_shared<Exception>("prim_job_finish arg2 not a string", binding);
    outputs = &empty;
  }

  parse_usage(&job->report, job->bad_finish, binding, args.data()+3);
  job->report.found = true;

  bool keep = !job->bad_launch && !job->bad_finish && job->keep && job->report.status == 0;
  job->db->finish_job(job->job, *inputs, *outputs, job->code.data[0], keep, job->report);
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

static void usage_type(TypeVar &pair) {
  TypeVar pair0;
  TypeVar pair1;
  TypeVar pair10;
  TypeVar pair11;
  Data::typePair.clone(pair);
  Data::typePair.clone(pair0);
  Data::typePair.clone(pair1);
  Data::typePair.clone(pair10);
  Data::typePair.clone(pair11);
  pair[0].unify(pair0);
  pair[1].unify(pair1);
  pair0[0].unify(Integer::typeVar);
  pair0[1].unify(Double::typeVar);
  pair1[0].unify(pair10);
  pair10[0].unify(Double::typeVar);
  pair10[1].unify(Integer::typeVar);
  pair1[1].unify(pair11);
  pair11[0].unify(Integer::typeVar);
  pair11[1].unify(Integer::typeVar);
}

static PRIMTYPE(type_job_reality) {
  TypeVar pair;
  usage_type(pair);
  return args.size() == 1 &&
    args[0]->unify(Job::typeVar) &&
    out->unify(pair);
}

static PRIMFN(prim_job_reality) {
  EXPECT(1);
  JOBRESULT(job, 0);
  completion->next = std::move(job->q_reality);
  job->q_reality = std::move(completion);
  job->process(queue);
}

static PRIMTYPE(type_job_report) {
  TypeVar pair;
  usage_type(pair);
  return args.size() == 1 &&
    args[0]->unify(Job::typeVar) &&
    out->unify(pair);
}

static PRIMFN(prim_job_report) {
  EXPECT(1);
  JOBRESULT(job, 0);
  completion->next = std::move(job->q_report);
  job->q_report = std::move(completion);
  job->process(queue);
}

static PRIMTYPE(type_job_record) {
  TypeVar list;
  TypeVar pair;
  Data::typeList.clone(list);
  usage_type(pair);
  list[0].unify(pair);
  return args.size() == 1 &&
    args[0]->unify(Job::typeVar) &&
    out->unify(list);
}

static PRIMFN(prim_job_record) {
  EXPECT(1);
  JOBRESULT(job, 0);

  std::vector<std::shared_ptr<Value> > stats;
  if (job->record.found) stats.emplace_back(make_usage(job->record));

  auto out = make_list(std::move(stats));
  RETURN(out);
}

void prim_register_job(JobTable *jobtable, PrimMap &pmap) {
  pmap.emplace("job_cache",  PrimDesc(prim_job_cache,  type_job_cache,   PRIM_SHALLOW, jobtable));
  pmap.emplace("job_create", PrimDesc(prim_job_create, type_job_create,  PRIM_SHALLOW, jobtable));
  pmap.emplace("job_launch", PrimDesc(prim_job_launch, type_job_launch,  PRIM_SHALLOW, jobtable));
  pmap.emplace("job_virtual",PrimDesc(prim_job_virtual,type_job_virtual, PRIM_SHALLOW, jobtable));
  pmap.emplace("job_finish", PrimDesc(prim_job_finish, type_job_finish,  PRIM_SHALLOW));
  pmap.emplace("job_kill",   PrimDesc(prim_job_kill,   type_job_kill,    PRIM_SHALLOW));
  pmap.emplace("job_output", PrimDesc(prim_job_output, type_job_output,  PRIM_SHALLOW|PRIM_PURE));
  pmap.emplace("job_tree",   PrimDesc(prim_job_tree,   type_job_tree,    PRIM_SHALLOW|PRIM_PURE));
  pmap.emplace("job_reality",PrimDesc(prim_job_reality,type_job_reality, PRIM_SHALLOW|PRIM_PURE));
  pmap.emplace("job_report", PrimDesc(prim_job_report, type_job_report,  PRIM_SHALLOW|PRIM_PURE));
  pmap.emplace("job_record", PrimDesc(prim_job_record, type_job_record,  PRIM_SHALLOW|PRIM_PURE));
  pmap.emplace("add_hash",   PrimDesc(prim_add_hash,   type_add_hash,    PRIM_SHALLOW, jobtable));
  pmap.emplace("get_hash",   PrimDesc(prim_get_hash,   type_get_hash,    PRIM_SHALLOW, jobtable));
  pmap.emplace("get_modtime",PrimDesc(prim_get_modtime,type_get_modtime, PRIM_SHALLOW));
  pmap.emplace("search_path",PrimDesc(prim_search_path,type_search_path, PRIM_SHALLOW));
}
