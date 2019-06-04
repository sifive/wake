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

#include "job.h"
#include "prim.h"
#include "value.h"
#include "heap.h"
#include "database.h"
#include "location.h"
#include "execpath.h"
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
#include <math.h>
#include <sstream>
#include <iostream>
#include <list>
#include <vector>
#include <cstring>
#include <queue>

// How many times to SIGTERM a process before SIGKILL
#define TERM_ATTEMPTS 6
// How long between first and second SIGTERM attempt (exponentially increasing)
#define TERM_BASE_GAP_MS 100

#define STATE_FORKED	1  // in database and running
#define STATE_STDOUT	2  // stdout fully in database
#define STATE_STDERR	4  // stderr fully in database
#define STATE_MERGED	8  // exit status in struct
#define STATE_FINISHED	16 // inputs+outputs+status+runtime in database

#define LOG_STDOUT(x) (x & 3)
#define LOG_STDERR(x) ((x >> 2) & 3)
#define LOG_ECHO(x) (x & 0x10)

// Can be queried at multiple stages of the job's lifetime
struct Job : public Value {
  Database *db;
  std::string cmdline, stdin;
  int state;
  Hash code; // hash(dir, stdin, environ, cmdline)
  pid_t pid;
  long job;
  bool keep;
  int log;
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
  Job(Database *db_, const std::string &dir, const std::string &stdin, const std::string &environ, const std::string &cmdline, bool keep, int log);

  void format(std::ostream &os, FormatState &state) const;
  TypeVar &getType();
  Hash hash() const;

  double threads() const;
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

double Job::threads() const {
  double estimate;

  if (predict.runtime == 0) {
    // We have no prior execution statistics
    // However, a Runner can still predict thread usage in this case using cputime.
    estimate = predict.cputime;
  } else {
    // Estimate the threads required
    estimate = predict.cputime / predict.runtime;
  }

  if (estimate > 1.0) {
    // This is a multi-threaded Job. It needs more than one slot.
    // Multiply by 1.3 to prevent runaway effect; see below
    return estimate*1.3;
  }

  // This is probably a single-threaded job.

  // If the job is bottlenecked by IO or something else, scheduling more jobs might help.
  // However, suppose we previously 2N 100% CPU jobs on N cores. These jobs would also
  // have an estimate of 0.5. If we made our decision based on that, we'd schedule 2N again.
  // Worse, if there is even any additional overhead (there probably is), we will get an
  // even lower estimate next time and schedule more and more and more jobs each run.

  // To combat this effect, we conservatively double the CPU utilization of jobs.
  // However, it's probably still single-threaded, so cap this pessimism there.
  estimate *= 2.0;
  if (estimate > 1.0) estimate = 1.0;

  // Finally, the cputime/realtime will be VERY low if the job was executed remotely.
  // However, even in that case we don't want to fork-bomb the local machine, so impose
  // an absolute lower limit of 0.01 (ie: 100*max_jobs).
  if (estimate < 0.01) estimate = 0.01;

  return estimate;
}

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

static bool operator < (const std::unique_ptr<Task> &x, const std::unique_ptr<Task> &y) {
  // 0 (unknown runtime) is infinity for this comparison (ie: run first)
  double xr = x->job->predict.runtime;
  double yr = y->job->predict.runtime;
  return xr != 0 && (yr == 0 || xr < yr);
}

// A JobEntry is a forked job with pid|stdout|stderr incomplete
struct JobEntry {
  std::shared_ptr<Job> job; // if unset, available for reuse
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
  std::list<JobEntry> running;
  std::priority_queue<std::unique_ptr<Task> > pending;
  sigset_t block; // signals that can race with pselect()
  Database *db;
  double active, limit; // threads
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
  imp->verbose = verbose;
  imp->quiet = quiet;
  imp->check = check;
  imp->db = db;
  imp->active = 0;
  imp->limit = max_jobs;
  sigemptyset(&imp->block);

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

  // Allow wake to open as many file descriptors as possible
  struct rlimit limit;
  if (getrlimit(RLIMIT_NOFILE, &limit) != 0) {
    perror("getrlimit(RLIMIT_NOFILE)");
    exit(1);
  }
  limit.rlim_cur = limit.rlim_max;
  if (setrlimit(RLIMIT_NOFILE, &limit) != 0) {
    perror("setrlimit(RLIMIT_NOFILE)");
    exit(1);
  }
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
    for (auto &i : imp->running) {
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
        for (auto &i : imp->running) {
          if (i.pid == pid) i.pid = 0;
          if (i.pid != 0) children = true;
        }
      }
    }
  }

  // Force children to die
  for (auto &i : imp->running) {
    if (i.pid == 0) continue;
    std::stringstream s;
    s << "Force killing " << i.pid << " after " << TERM_ATTEMPTS << " attempts with SIGTERM" << std::endl;
    std::string out = s.str();
    status_write(2, out.data(), out.size());
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

static std::string pretty_cmd(std::string x) {
  for (char &c : x) if (c == 0) c = ' ';
  x.resize(x.size()-1); // trim trailing ' '
  return x;
}

struct CompletedJobEntry {
  JobTable *jobtable;
  CompletedJobEntry(JobTable *jobtable_) : jobtable(jobtable_) { }

  bool operator () (const JobEntry &i) {
    if (i.pid == 0 && i.pipe_stdout == -1 && i.pipe_stderr == -1) {
      status_state.erase(i.status);
      jobtable->imp->active -= i.job->threads();
      return true;
    }
    return false;
  }
};

static void launch(JobTable *jobtable) {
  CompletedJobEntry pred(jobtable);
  jobtable->imp->running.remove_if(pred);

  // Note: We schedule jobs whenever we are under quota, without considering if the
  // new job will cause us to exceed the quota. This is necessary, for two reasons:
  //   1 - a job could require more compute than allowed; we require forward progress
  //   2 - if the next optimal job to schedule needs more compute than available
  //     a - it would waste idle compute if we don't schedule something
  //     b - it would hurt the build critical path if we schedule a sub-optimal job
  // => just oversubscribe compute and let the kernel sort it out
  while (!jobtable->imp->pending.empty() && jobtable->imp->active < jobtable->imp->limit) {
    Task &task = *jobtable->imp->pending.top();
    jobtable->imp->active += task.job->threads();

    jobtable->imp->running.emplace_back();
    JobEntry &i = jobtable->imp->running.back();

    int pipe_stdout[2];
    int pipe_stderr[2];
    if (pipe(pipe_stdout) == -1 || pipe(pipe_stderr) == -1) {
      perror("pipe");
      exit(1);
    }
    int flags;
    if ((flags = fcntl(pipe_stdout[0], F_GETFD, 0)) != -1) fcntl(pipe_stdout[0], F_SETFD, flags | FD_CLOEXEC);
    if ((flags = fcntl(pipe_stderr[0], F_GETFD, 0)) != -1) fcntl(pipe_stderr[0], F_SETFD, flags | FD_CLOEXEC);
    i.job = std::move(task.job);
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
    bool indirect = i.job->cmdline != task.cmdline;
    double predict = i.job->predict.status == 0 ? i.job->predict.runtime : 0;
    i.status = status_state.emplace(status_state.end(), pretty_cmd(i.job->cmdline), predict, i.start);
    if (LOG_ECHO(i.job->log)) {
      std::stringstream s;
      s << pretty_cmd(i.job->cmdline);
      if (!i.job->stdin.empty()) s << " < " << i.job->stdin;
      if (indirect && jobtable->imp->verbose) {
        s << " # launched by: " << pretty_cmd(task.cmdline);
        if (!task.stdin.empty()) s << " < " << task.stdin;
      }
      s << std::endl;
      std::string out = s.str();
      status_write(1, out.data(), out.size());
    }

#if 0
    std::stringstream s;
    s << "Scheduled " << i.job->threads()
      << " for a total of " << jobtable->imp->active
      << " utilized cores and " << jobtable->imp->running.size()
      << " running tasks." << std::endl;
    std::string out = s.str();
    status_write(2, out.data(), out.size());
#endif

    // i.job->stdin.clear();
    // i.job->cmdline.clear();
    jobtable->imp->pending.pop();
  }
}

bool JobTable::wait(WorkQueue &queue) {
  char buffer[4096];
  struct timespec nowait;
  memset(&nowait, 0, sizeof(nowait));

  bool compute = false;
  while (!exit_now() && !imp->running.empty()) {
    fd_set set;
    int nfds = 0;

    FD_ZERO(&set);
    for (auto &i : imp->running) {
      if (i.pipe_stdout != -1) {
        if (i.pipe_stdout >= nfds) nfds = i.pipe_stdout + 1;
        FD_SET(i.pipe_stdout, &set);
      }
      if (i.pipe_stderr != -1) {
        if (i.pipe_stderr >= nfds) nfds = i.pipe_stderr + 1;
        FD_SET(i.pipe_stderr, &set);
      }
    }

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

    if (retval > 0) for (auto &i : imp->running) {
      if (i.pipe_stdout != -1 && FD_ISSET(i.pipe_stdout, &set)) {
        int got = read(i.pipe_stdout, buffer, sizeof(buffer));
        if (got == 0 || (got < 0 && errno != EINTR)) {
          close(i.pipe_stdout);
          i.pipe_stdout = -1;
          i.status->stdout = false;
          i.job->state |= STATE_STDOUT;
          i.job->process(queue);
          ++done;
          if (LOG_STDOUT(i.job->log)) {
            if (!i.stdout_buf.empty() && i.stdout_buf.back() != '\n')
              i.stdout_buf.push_back('\n');
            status_write(LOG_STDOUT(i.job->log), i.stdout_buf.data(), i.stdout_buf.size());
            i.stdout_buf.clear();
          }
        } else {
          i.job->db->save_output(i.job->job, 1, buffer, got, i.runtime(now));
          if (LOG_STDOUT(i.job->log)) {
            i.stdout_buf.append(buffer, got);
            size_t dump = i.stdout_buf.rfind('\n');
            if (dump != std::string::npos) {
              status_write(LOG_STDOUT(i.job->log), i.stdout_buf.data(), dump+1);
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
          i.status->stderr = false;
          i.job->state |= STATE_STDERR;
          i.job->process(queue);
          ++done;
          if (LOG_STDERR(i.job->log)) {
            if (!i.stderr_buf.empty() && i.stderr_buf.back() != '\n')
              i.stderr_buf.push_back('\n');
            status_write(LOG_STDERR(i.job->log), i.stderr_buf.data(), i.stderr_buf.size());
            i.stderr_buf.clear();
          }
        } else {
          i.job->db->save_output(i.job->job, 2, buffer, got, i.runtime(now));
          if (LOG_STDERR(i.job->log)) {
            i.stderr_buf.append(buffer, got);
            size_t dump = i.stderr_buf.rfind('\n');
            if (dump != std::string::npos) {
              status_write(LOG_STDERR(i.job->log), i.stderr_buf.data(), dump+1);
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

      for (auto &i : imp->running) {
        if (i.pid == pid) {
          i.pid = 0;
          i.status->merged = true;
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

Job::Job(Database *db_, const std::string &dir, const std::string &stdin_, const std::string &environ, const std::string &cmdline_, bool keep_, int log_)
  : Value(&type), db(db_), cmdline(cmdline_), stdin(stdin_), state(0), code(), pid(0), job(-1), keep(keep_), log(log_)
{
  std::vector<uint64_t> codes;
  type.hashcode.push(codes);
  Hash(dir).push(codes);
  Hash(stdin).push(codes);
  Hash(environ).push(codes);
  Hash(cmdline).push(codes);
  code = Hash(codes);
}

#define JOB(arg, i) REQUIRE(args[i]->type == &Job::type); Job *arg = reinterpret_cast<Job*>(args[i].get());

static void parse_usage(Usage *usage, std::shared_ptr<Value> *args, WorkQueue &queue, const std::shared_ptr<Binding> &binding) {
  INTEGER(status, 0);
  DOUBLE(runtime, 1);
  DOUBLE(cputime, 2);
  INTEGER(membytes, 3);
  INTEGER(ibytes, 4);
  INTEGER(obytes, 5);

  usage->status = mpz_get_si(status->value);
  usage->runtime = runtime->value;
  usage->cputime = cputime->value;
  usage->membytes = mpz_get_si(membytes->value);
  usage->ibytes = mpz_get_si(ibytes->value);
  usage->obytes = mpz_get_si(obytes->value);
}

static PRIMTYPE(type_job_fail) {
  return args.size() == 2 &&
    args[0]->unify(Job::typeVar) &&
    args[1]->unify(Data::typeError) &&
    out->unify(Data::typeUnit);
}

static PRIMFN(prim_job_fail_launch) {
  EXPECT(2);
  JOB(job, 0);

  REQUIRE (job->state == 0);

  job->bad_launch = args[1];
  job->reality.found    = true;
  job->reality.status   = 128;
  job->reality.runtime  = 0;
  job->reality.cputime  = 0;
  job->reality.membytes = 0;
  job->reality.ibytes   = 0;
  job->reality.obytes   = 0;
  job->state = STATE_FORKED|STATE_STDOUT|STATE_STDERR|STATE_MERGED;
  job->process(queue);

  auto out = make_unit();
  RETURN(out);
}

static PRIMFN(prim_job_fail_finish) {
  EXPECT(2);
  JOB(job, 0);

  REQUIRE(job->state & STATE_MERGED);
  REQUIRE(!(job->state & STATE_FINISHED));

  job->bad_finish = args[1];
  job->report.found    = true;
  job->report.status   = 128;
  job->report.runtime  = 0;
  job->report.cputime  = 0;
  job->report.membytes = 0;
  job->report.ibytes   = 0;
  job->report.obytes   = 0;
  job->state |= STATE_FINISHED;
  job->process(queue);

  auto out = make_unit();
  RETURN(out);
}

static PRIMTYPE(type_job_launch) {
  return args.size() == 11 &&
    args[0]->unify(Job::typeVar) &&
    args[1]->unify(String::typeVar) &&
    args[2]->unify(String::typeVar) &&
    args[3]->unify(String::typeVar) &&
    args[4]->unify(String::typeVar) &&
    args[5]->unify(Integer::typeVar) &&
    args[6]->unify(Double::typeVar) &&
    args[7]->unify(Double::typeVar) &&
    args[8]->unify(Integer::typeVar) &&
    args[9]->unify(Integer::typeVar) &&
    args[10]->unify(Integer::typeVar) &&
    out->unify(Data::typeUnit);
}

static PRIMFN(prim_job_launch) {
  JobTable *jobtable = reinterpret_cast<JobTable*>(data);
  EXPECT(11);
  JOB(job, 0);
  STRING(dir, 1);
  STRING(stdin, 2);
  STRING(env, 3);
  STRING(cmd, 4);

  parse_usage(&job->predict, args.data()+5, queue, binding);
  job->predict.found = true;

  REQUIRE (job->state == 0);

  jobtable->imp->pending.emplace(new Task(
    std::dynamic_pointer_cast<Job>(args[0]),
    dir->value,
    stdin->value,
    env->value,
    cmd->value));
  launch(jobtable);

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
  EXPECT(9);
  JOB(job, 0);
  STRING(stdout, 1);
  STRING(stderr, 2);

  parse_usage(&job->predict, args.data()+3, queue, binding);
  job->predict.found = true;
  job->reality = job->predict;

  if (!stdout->value.empty())
    job->db->save_output(job->job, 1, stdout->value.data(), stdout->value.size(), 0);
  if (!stderr->value.empty())
    job->db->save_output(job->job, 2, stderr->value.data(), stderr->value.size(), 0);

  REQUIRE (job->state == 0);

  if (LOG_ECHO(job->log)) {
    std::stringstream s;
    s << pretty_cmd(job->cmdline);
    if (!job->stdin.empty()) s << " < " << job->stdin;
    s << std::endl;
    std::string out = s.str();
    status_write(1, out.data(), out.size());
  }

  job->state = STATE_FORKED|STATE_STDOUT|STATE_STDERR|STATE_MERGED;
  job->process(queue);

  auto out = make_unit();
  RETURN(out);
}

static PRIMTYPE(type_job_create) {
  return args.size() == 7 &&
    args[0]->unify(String::typeVar) &&
    args[1]->unify(String::typeVar) &&
    args[2]->unify(String::typeVar) &&
    args[3]->unify(String::typeVar) &&
    args[4]->unify(String::typeVar) &&
    args[5]->unify(Integer::typeVar) &&
    args[6]->unify(Integer::typeVar) &&
    out->unify(Job::typeVar);
}

static PRIMFN(prim_job_create) {
  JobTable *jobtable = reinterpret_cast<JobTable*>(data);
  EXPECT(7);
  STRING(dir, 0);
  STRING(stdin, 1);
  STRING(env, 2);
  STRING(cmd, 3);
  STRING(visible, 4);
  INTEGER(keep, 5);
  INTEGER(log, 6);

  std::stringstream stack;
  for (auto &i : binding->stack_trace()) stack << i.file() << std::endl;
  auto out = std::make_shared<Job>(
    jobtable->imp->db,
    dir->value,
    stdin->value,
    env->value,
    cmd->value,
    mpz_cmp_si(keep->value,0),
    mpz_get_si(log->value));

  out->record = jobtable->imp->db->predict_job(out->code.data[0]);

  out->db->insert_job(
    dir->value,
    stdin->value,
    env->value,
    cmd->value,
    visible->value,
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
    auto out = std::make_shared<Job>(jobtable->imp->db, dir->value, stdin->value, env->value, cmd->value, true, 0);
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
    auto out = bad_launch
      ? make_result(false, std::shared_ptr<Value>(bad_launch))
      : make_result(true, std::make_shared<String>(db->get_output(job, 1)));
    std::unique_ptr<Receiver> iter, next;
    for (iter = std::move(q_stdout); iter; iter = std::move(next)) {
      next = std::move(iter->next);
      Receiver::receive(queue, std::move(iter), out);
    }
    q_stdout.reset();
  }

  if ((state & STATE_STDERR) && q_stderr) {
    auto out = bad_launch
      ? make_result(false, std::shared_ptr<Value>(bad_launch))
      : make_result(true, std::make_shared<String>(db->get_output(job, 2)));
    std::unique_ptr<Receiver> iter, next;
    for (iter = std::move(q_stderr); iter; iter = std::move(next)) {
      next = std::move(iter->next);
      Receiver::receive(queue, std::move(iter), out);
    }
    q_stderr.reset();
  }

  if ((state & STATE_MERGED) && q_reality) {
    auto out = bad_launch
      ? make_result(false, std::shared_ptr<Value>(bad_launch))
      : make_result(true, make_usage(reality));
    std::unique_ptr<Receiver> iter, next;
    for (iter = std::move(q_reality); iter; iter = std::move(next)) {
      next = std::move(iter->next);
      Receiver::receive(queue, std::move(iter), out);
    }
    q_reality.reset();
  }

  if ((state & STATE_FINISHED) && q_inputs) {
    auto files = db->get_tree(1, job);
    auto out = bad_finish
      ? make_result(false, std::shared_ptr<Value>(bad_finish))
      : make_result(true, convert_tree(std::move(files)));
    std::unique_ptr<Receiver> iter, next;
    for (iter = std::move(q_inputs); iter; iter = std::move(next)) {
      next = std::move(iter->next);
      Receiver::receive(queue, std::move(iter), out);
    }
    q_inputs.reset();
  }

  if ((state & STATE_FINISHED) && q_outputs) {
    auto files = db->get_tree(2, job);
    auto out = bad_finish
      ? make_result(false, std::shared_ptr<Value>(bad_finish))
      : make_result(true, convert_tree(std::move(files)));
    std::unique_ptr<Receiver> iter, next;
    for (iter = std::move(q_outputs); iter; iter = std::move(next)) {
      next = std::move(iter->next);
      Receiver::receive(queue, std::move(iter), out);
    }
    q_outputs.reset();
  }

  if ((state & STATE_FINISHED) && q_report) {
    auto out = bad_finish
      ? make_result(false, std::shared_ptr<Value>(bad_finish))
      : make_result(true, make_usage(report));
    std::unique_ptr<Receiver> iter, next;
    for (iter = std::move(q_report); iter; iter = std::move(next)) {
      next = std::move(iter->next);
      Receiver::receive(queue, std::move(iter), out);
    }
    q_report.reset();
  }
}

static PRIMTYPE(type_job_output) {
  TypeVar result;
  Data::typeResult.clone(result);
  result[0].unify(String::typeVar);
  result[1].unify(Data::typeError);
  return args.size() == 2 &&
    args[0]->unify(Job::typeVar) &&
    args[1]->unify(Integer::typeVar) &&
    out->unify(result);
}

static PRIMFN(prim_job_output) {
  EXPECT(2);
  JOB(arg0, 0);
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
    bool stdin_or_stderr = false;
    REQUIRE(stdin_or_stderr);
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
  JOB(arg0, 0);
  INTEGER(arg1, 1);

  if (mpz_cmp_si(arg1->value, 256) < 0 && mpz_cmp_si(arg1->value, 0) > 0) {
    int sig = mpz_get_si(arg1->value);
    if ((arg0->state & STATE_FORKED) && !(arg0->state & STATE_MERGED))
      kill(arg0->pid, sig);
  }

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
  TypeVar result;
  Data::typeResult.clone(result);
  result[0].unify(list);
  result[1].unify(Data::typeError);
  return args.size() == 2 &&
    args[0]->unify(Job::typeVar) &&
    args[1]->unify(Integer::typeVar) &&
    out->unify(result);
}

static PRIMFN(prim_job_tree) {
  EXPECT(2);
  JOB(arg0, 0);
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
    bool stdin_or_stderr = false;
    REQUIRE(stdin_or_stderr);
  }
}

static PRIMTYPE(type_job_id) {
  return args.size() == 1 &&
    args[0]->unify(Job::typeVar) &&
    out->unify(Integer::typeVar);
}

static PRIMFN(prim_job_id) {
  EXPECT(1);
  JOB(arg0, 0);
  auto out = std::make_shared<Integer>(arg0->job);
  RETURN(out);
}

static PRIMTYPE(type_job_desc) {
  return args.size() == 1 &&
    args[0]->unify(Job::typeVar) &&
    out->unify(String::typeVar);
}

static PRIMFN(prim_job_desc) {
  EXPECT(1);
  JOB(arg0, 0);
  auto out = std::make_shared<String>(pretty_cmd(arg0->cmdline));
  RETURN(out);
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
  EXPECT(9);
  JOB(job, 0);
  STRING(inputs, 1);
  STRING(outputs, 2);

  parse_usage(&job->report, args.data()+3, queue, binding);
  job->report.found = true;

  REQUIRE(job->state & STATE_MERGED);
  REQUIRE(!(job->state & STATE_FINISHED));

  bool keep = !job->bad_launch && !job->bad_finish && job->keep && job->report.status == 0;
  job->db->finish_job(job->job, inputs->value, outputs->value, job->code.data[0], keep, job->report);
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
  if (stat(file.c_str(), &sbuf) != 0) return -1;
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

  auto out = std::make_shared<String>(find_in_path(exec->value, path->value));
  RETURN(out);
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
  TypeVar result;
  Data::typeResult.clone(result);
  result[0].unify(pair);
  result[1].unify(Data::typeError);
  return args.size() == 1 &&
    args[0]->unify(Job::typeVar) &&
    out->unify(result);
}

static PRIMFN(prim_job_reality) {
  EXPECT(1);
  JOB(job, 0);
  completion->next = std::move(job->q_reality);
  job->q_reality = std::move(completion);
  job->process(queue);
}

static PRIMTYPE(type_job_report) {
  TypeVar pair;
  usage_type(pair);
  TypeVar result;
  Data::typeResult.clone(result);
  result[0].unify(pair);
  result[1].unify(Data::typeError);
  return args.size() == 1 &&
    args[0]->unify(Job::typeVar) &&
    out->unify(result);
}

static PRIMFN(prim_job_report) {
  EXPECT(1);
  JOB(job, 0);
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
  JOB(job, 0);

  std::vector<std::shared_ptr<Value> > stats;
  if (job->record.found) stats.emplace_back(make_usage(job->record));

  auto out = make_list(std::move(stats));
  RETURN(out);
}

static PRIMTYPE(type_access) {
  return args.size() == 2 &&
    args[0]->unify(String::typeVar) &&
    args[1]->unify(Integer::typeVar) &&
    out->unify(Data::typeBoolean);
}

static PRIMFN(prim_access) {
  EXPECT(2);
  STRING(file, 0);
  INTEGER(kind, 1);
  int mode = R_OK;
  if (mpz_cmp_si(kind->value, 1) == 0) mode = W_OK;
  if (mpz_cmp_si(kind->value, 2) == 0) mode = X_OK;
  auto out = make_bool(access(file->value.c_str(), mode) == 0);
  RETURN(out);
}

void prim_register_job(JobTable *jobtable, PrimMap &pmap) {
  prim_register(pmap, "job_cache",  prim_job_cache,  type_job_cache,   PRIM_SHALLOW, jobtable);
  prim_register(pmap, "job_create", prim_job_create, type_job_create,  PRIM_SHALLOW, jobtable);
  prim_register(pmap, "job_launch", prim_job_launch, type_job_launch,  PRIM_SHALLOW, jobtable);
  prim_register(pmap, "job_virtual",prim_job_virtual,type_job_virtual, PRIM_SHALLOW, jobtable);
  prim_register(pmap, "job_finish", prim_job_finish, type_job_finish,  PRIM_SHALLOW);
  prim_register(pmap, "job_fail_launch", prim_job_fail_launch, type_job_fail, PRIM_SHALLOW);
  prim_register(pmap, "job_fail_finish", prim_job_fail_finish, type_job_fail, PRIM_SHALLOW);
  prim_register(pmap, "job_kill",   prim_job_kill,   type_job_kill,    PRIM_SHALLOW);
  prim_register(pmap, "job_output", prim_job_output, type_job_output,  PRIM_SHALLOW|PRIM_PURE);
  prim_register(pmap, "job_tree",   prim_job_tree,   type_job_tree,    PRIM_SHALLOW|PRIM_PURE);
  prim_register(pmap, "job_id",     prim_job_id,     type_job_id,      PRIM_SHALLOW|PRIM_PURE);
  prim_register(pmap, "job_desc",   prim_job_desc,   type_job_desc,    PRIM_SHALLOW|PRIM_PURE);
  prim_register(pmap, "job_reality",prim_job_reality,type_job_reality, PRIM_SHALLOW|PRIM_PURE);
  prim_register(pmap, "job_report", prim_job_report, type_job_report,  PRIM_SHALLOW|PRIM_PURE);
  prim_register(pmap, "job_record", prim_job_record, type_job_record,  PRIM_SHALLOW|PRIM_PURE);
  prim_register(pmap, "add_hash",   prim_add_hash,   type_add_hash,    PRIM_SHALLOW, jobtable);
  prim_register(pmap, "get_hash",   prim_get_hash,   type_get_hash,    PRIM_SHALLOW, jobtable);
  prim_register(pmap, "get_modtime",prim_get_modtime,type_get_modtime, PRIM_SHALLOW);
  prim_register(pmap, "search_path",prim_search_path,type_search_path, PRIM_SHALLOW);
  prim_register(pmap, "access",     prim_access,     type_access,      PRIM_SHALLOW);
}
