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
#include "type.h"
#include "value.h"
#include "database.h"
#include "location.h"
#include "execpath.h"
#include "status.h"
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
#include <limits.h>
#include <sstream>
#include <iostream>
#include <list>
#include <vector>
#include <cstring>
#include <algorithm>
#include <limits>

// How many times to SIGTERM a process before SIGKILL
#define TERM_ATTEMPTS 6
// How long between first and second SIGTERM attempt (exponentially increasing)
#define TERM_BASE_GAP_MS 100
// The most file descriptors used by wake for itself (database/stdio/etc)
#define MAX_SELF_FDS	24
// The most children wake will ever allow to run at once
#define MAX_CHILDREN	500

// #define DEBUG_PROGRESS

#define ALMOST_ONE (1.0 - 2*std::numeric_limits<double>::epsilon())

#define STATE_FORKED	1  // in database and running
#define STATE_STDOUT	2  // stdout fully in database
#define STATE_STDERR	4  // stderr fully in database
#define STATE_MERGED	8  // exit status in struct
#define STATE_FINISHED	16 // inputs+outputs+status+runtime in database

#define LOG_STDOUT(x) (x & 3)
#define LOG_STDERR(x) ((x >> 2) & 3)
#define LOG_ECHO(x) (x & 0x10)

// Can be queried at multiple stages of the job's lifetime
struct Job final : public GCObject<Job> {
  typedef GCObject<Job> Parent;

  Database *db;
  HeapPointer<String> cmdline, stdin;
  int state;
  Hash code; // hash(dir, stdin, environ, cmdline)
  pid_t pid;
  long job;
  bool keep;
  int log;
  HeapPointer<HeapObject> bad_launch;
  HeapPointer<HeapObject> bad_finish;
  double pathtime;
  Usage record;  // retrieved from DB (user-facing usage)
  Usage predict; // prediction of Runners given record (used by scheduler)
  Usage reality; // actual measured local usage
  Usage report;  // usage to save into DB + report in Job API

  // There are 4 distinct wait queues for jobs
  HeapPointer<Continuation> q_stdout;  // waken once stdout closed
  HeapPointer<Continuation> q_stderr;  // waken once stderr closed
  HeapPointer<Continuation> q_reality; // waken once job merged (reality available)
  HeapPointer<Continuation> q_inputs;  // waken once job finished (inputs+outputs+report available)
  HeapPointer<Continuation> q_outputs; // waken once job finished (inputs+outputs+report available)
  HeapPointer<Continuation> q_report;  // waken once job finished (inputs+outputs+report available)

  static TypeVar typeVar;
  Job(Database *db_, String *dir, String *stdin, String *environ, String *cmdline, bool keep, int log);

  template <typename T, T (HeapPointerBase::*memberfn)(T x)>
  T recurse(T arg);

  void format(std::ostream &os, FormatState &state) const override;
  Hash hash() const override;

  double threads() const;
};

template <typename T, T (HeapPointerBase::*memberfn)(T x)>
T Job::recurse(T arg) {
  arg = Parent::recurse<T, memberfn>(arg);
  arg = (cmdline.*memberfn)(arg);
  arg = (stdin.*memberfn)(arg);
  arg = (bad_launch.*memberfn)(arg);
  arg = (bad_finish.*memberfn)(arg);
  arg = (q_stdout.*memberfn)(arg);
  arg = (q_stderr.*memberfn)(arg);
  arg = (q_reality.*memberfn)(arg);
  arg = (q_inputs.*memberfn)(arg);
  arg = (q_outputs.*memberfn)(arg);
  arg = (q_report.*memberfn)(arg);
  return arg;
}

template <>
HeapStep Job::recurse<HeapStep, &HeapPointerBase::explore>(HeapStep step) {
  // We don't want to explore the work-queues or bad_finish/launch children
  // Instead, we front-loaded the hash calculation
  return step;
}

// Check if Job can wake up any computation
struct WJob final : public GCObject<WJob, Work> {
  typedef GCObject<WJob, Work> Parent;
  HeapPointer<Job> job;

  WJob(Job *job_) : job(job_) { }

  template <typename T, T (HeapPointerBase::*memberfn)(T x)>
  T recurse(T arg);

  void execute(Runtime &runtime) override;
};

template <typename T, T (HeapPointerBase::*memberfn)(T x)>
T WJob::recurse(T arg) {
  arg = Parent::recurse<T, memberfn>(arg);
  arg = (job.*memberfn)(arg);
  return arg;
}

void Job::format(std::ostream &os, FormatState &state) const {
  if (APP_PRECEDENCE < state.p()) os << "(";
  os << "Job " << job;
  if (APP_PRECEDENCE < state.p()) os << ")";
}

TypeVar Job::typeVar("Job", 0);

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
  RootPointer<Job> job;
  std::string dir;
  std::string stdin;
  std::string environ;
  std::string cmdline;
  Task(RootPointer<Job> &&job_, const std::string &dir_, const std::string &stdin_, const std::string &environ_, const std::string &cmdline_)
  : job(std::move(job_)), dir(dir_), stdin(stdin_), environ(environ_), cmdline(cmdline_) { }
};

static bool operator < (const std::unique_ptr<Task> &x, const std::unique_ptr<Task> &y) {
  // anything with dependants on stderr/stdout is infinity (ie: run first)
  if (x->job->q_stdout || x->job->q_stderr) return false;
  if (y->job->q_stdout || y->job->q_stderr) return true;
  // 0 (unknown runtime) is infinity for this comparison (ie: run first)
  if (x->job->predict.runtime == 0) return false;
  if (y->job->predict.runtime == 0) return true;
  return x->job->pathtime < y->job->pathtime;
}

// A JobEntry is a forked job with pid|stdout|stderr incomplete
struct JobEntry {
  RootPointer<Job> job; // if unset, available for reuse
  pid_t pid;       //  0 if merged
  int pipe_stdout; // -1 if closed
  int pipe_stderr; // -1 if closed
  std::string stdout_buf;
  std::string stderr_buf;
  struct timeval start;
  std::list<Status>::iterator status;
  JobEntry(RootPointer<Job> &&job_) : job(std::move(job_)), pid(0), pipe_stdout(-1), pipe_stderr(-1) { }
  double runtime(struct timeval now);
};

double JobEntry::runtime(struct timeval now) {
  return now.tv_sec - start.tv_sec + (now.tv_usec - start.tv_usec)/1000000.0;
}

struct CriticalJob {
  double pathtime;
  double runtime;
};

// Implementation details for a JobTable
struct JobTable::detail {
  std::list<JobEntry> running;
  std::vector<std::unique_ptr<Task> > pending;
  sigset_t block; // signals that can race with pselect()
  Database *db;
  double active, limit; // CPUs
  long max_children; // hard cap on jobs allowed
  bool verbose;
  bool quiet;
  bool check;
  struct timeval wall;

  CriticalJob critJob(double nexttime) const;
};

CriticalJob JobTable::detail::critJob(double nexttime) const {
  CriticalJob out;
  out.pathtime = nexttime;
  out.runtime = 0;
  for (auto &j : running) {
    if (j.pid && j.job->pathtime > out.pathtime) {
      out.pathtime = j.job->pathtime;
      out.runtime = j.job->record.runtime;
    }
  }
  for (auto &j : pending) {
    if (j->job->pathtime > out.pathtime) {
      out.pathtime = j->job->pathtime;
      out.runtime = j->job->record.runtime;
    }
  }
  return out;
}

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

  // Determine the available file descriptor limits
  struct rlimit limit;
  if (getrlimit(RLIMIT_NOFILE, &limit) != 0) {
    perror("getrlimit(RLIMIT_NOFILE)");
    exit(1);
  }

  // Calculate the maximum number of children to ever run
  imp->max_children = max_jobs * 100; // based on minimum 1% CPU utilization in Job::threads
  if (imp->max_children > MAX_CHILDREN) imp->max_children = MAX_CHILDREN; // wake hard cap
#ifdef CHILD_MAX
  if (imp->max_children > CHILD_MAX/2) imp->max_children = CHILD_MAX/2;   // limits.h
#endif
  long sys_child_max = sysconf(_SC_CHILD_MAX);
  if (sys_child_max != -1 && imp->max_children > sys_child_max/2) imp->max_children = sys_child_max/2;

#ifndef OPEN_MAX
#define OPEN_MAX 99999
#endif

  // We want 2 descriptors (stdout+stderr) per job.
  rlim_t requested = imp->max_children * 2 + MAX_SELF_FDS;
  rlim_t maximum = (limit.rlim_max == RLIM_INFINITY) ? OPEN_MAX : limit.rlim_max;
  if (maximum > OPEN_MAX) maximum = OPEN_MAX;
  if (maximum > FD_SETSIZE) maximum = FD_SETSIZE;

  if (maximum >= requested) {
    limit.rlim_cur = requested;
  } else {
    limit.rlim_cur = maximum;
    std::cerr << "wake wanted a limit of " << imp->max_children;
    imp->max_children = (maximum - MAX_SELF_FDS) / 2;
    std::cerr << " children, but only got " << imp->max_children
      << ", because only " << maximum
      << " file descriptors are available." << std::endl;
  }

/*
  std::cerr << "max children " << imp->max_children << "/" << sys_child_max
    << " and " << limit.rlim_cur << "/" << maximum << std::endl;
*/

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
      status_state.jobs.erase(i.status);
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
  auto &heap = jobtable->imp->pending;
  while (!heap.empty()
      && jobtable->imp->running.size() < (size_t)jobtable->imp->max_children
      && jobtable->imp->active < jobtable->imp->limit) {
    Task &task = *heap.front();
    jobtable->imp->active += task.job->threads();

    jobtable->imp->running.emplace_back(std::move(task.job));
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
    bool indirect = i.job->cmdline->compare(task.cmdline) != 0;
    double predict = i.job->predict.status == 0 ? i.job->predict.runtime : 0;
    std::string pretty = pretty_cmd(i.job->cmdline->as_str());
    i.status = status_state.jobs.emplace(status_state.jobs.end(), pretty, predict, i.start);
    if (LOG_ECHO(i.job->log)) {
      std::stringstream s;
      s << pretty;
      if (i.job->stdin->length != 0) s << " < " << i.job->stdin->c_str();
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

    std::pop_heap(heap.begin(), heap.end());
    heap.resize(heap.size()-1);
  }
}

bool JobTable::wait(Runtime &runtime) {
  char buffer[4096];
  struct timespec nowait;
  memset(&nowait, 0, sizeof(nowait));

  launch(this);

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
          runtime.heap.guarantee(WJob::reserve());
          runtime.schedule(WJob::claim(runtime.heap, i.job.get()));
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
          runtime.heap.guarantee(WJob::reserve());
          runtime.schedule(WJob::claim(runtime.heap, i.job.get()));
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
          runtime.heap.guarantee(WJob::reserve());
          runtime.schedule(WJob::claim(runtime.heap, i.job.get()));

          // If this was the job on the critical path, adjust remain
          if (i.job->pathtime == status_state.remain) {
            auto crit = imp->critJob(ALMOST_ONE * (i.job->pathtime - i.job->record.runtime));
#ifdef DEBUG_PROGRESS
            std::cerr << "RUN DONE CRIT: "
              << status_state.remain << " => " << crit.pathtime << "  /  "
              << status_state.total << std::endl;
#endif
            status_state.remain = crit.pathtime;
            status_state.current = crit.runtime;
            if (crit.runtime == 0) imp->wall = now;
          }
        }
      }
    }

    // In case the expected next critical job is never scheduled, fall back to the next
    double dwall = (now.tv_sec - imp->wall.tv_sec) + (now.tv_usec - imp->wall.tv_usec) / 1000000.0;
    if (status_state.current == 0 && dwall*5 > status_state.remain) {
      auto crit = imp->critJob(0);
      if (crit.runtime != 0) {
        status_state.total = crit.pathtime + (status_state.total - status_state.remain);
        status_state.remain = crit.pathtime;
        status_state.current = crit.runtime;
      }
    }

    if (done > 0) {
      compute = true;
      break;
    }
  }

  return compute;
}

Job::Job(Database *db_, String *dir, String *stdin_, String *environ, String *cmdline_, bool keep_, int log_)
  : db(db_), cmdline(cmdline_), stdin(stdin_), state(0), code(), pid(0), job(-1), keep(keep_), log(log_)
{
  std::vector<uint64_t> codes;
  Hash(dir->c_str(), dir->length).push(codes);
  Hash(stdin->c_str(), stdin->length).push(codes);
  Hash(environ->c_str(), environ->length).push(codes);
  Hash(cmdline->c_str(), cmdline->length).push(codes);
  code = Hash(codes);
}

Hash Job::hash() const {
  return code;
}

#define JOB(arg, i) do { HeapObject *arg = args[i]; REQUIRE(typeid(*arg) == typeid(Job)); } while(0); Job *arg = static_cast<Job*>(args[i]);

static void parse_usage(Usage *usage, HeapObject **args, Runtime &runtime, Tuple *scope) {
  INTEGER_MPZ(status, 0);
  DOUBLE(rtime, 1);
  DOUBLE(ctime, 2);
  INTEGER_MPZ(membytes, 3);
  INTEGER_MPZ(ibytes, 4);
  INTEGER_MPZ(obytes, 5);

  usage->status = mpz_get_si(status);
  usage->runtime = rtime->value;
  usage->cputime = ctime->value;
  usage->membytes = mpz_get_si(membytes);
  usage->ibytes = mpz_get_si(ibytes);
  usage->obytes = mpz_get_si(obytes);
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

  size_t need = reserve_unit() + WJob::reserve();
  runtime.heap.reserve(need);

  job->bad_launch = args[1];
  job->reality.found    = true;
  job->reality.status   = 128;
  job->reality.runtime  = 0;
  job->reality.cputime  = 0;
  job->reality.membytes = 0;
  job->reality.ibytes   = 0;
  job->reality.obytes   = 0;
  job->state = STATE_FORKED|STATE_STDOUT|STATE_STDERR|STATE_MERGED;

  runtime.schedule(WJob::claim(runtime.heap, job));
  RETURN(claim_unit(runtime.heap));
}

static PRIMFN(prim_job_fail_finish) {
  EXPECT(2);
  JOB(job, 0);

  REQUIRE(job->state & STATE_MERGED);
  REQUIRE(!(job->state & STATE_FINISHED));

  size_t need = reserve_unit() + WJob::reserve();
  runtime.heap.reserve(need);

  job->bad_finish = args[1];
  job->report.found    = true;
  job->report.status   = 128;
  job->report.runtime  = 0;
  job->report.cputime  = 0;
  job->report.membytes = 0;
  job->report.ibytes   = 0;
  job->report.obytes   = 0;
  job->state |= STATE_FINISHED;

  runtime.schedule(WJob::claim(runtime.heap, job));
  RETURN(claim_unit(runtime.heap));
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

  runtime.heap.reserve(reserve_unit());
  parse_usage(&job->predict, args+5, runtime, scope);
  job->predict.found = true;

  REQUIRE (job->state == 0);

  auto &heap = jobtable->imp->pending;
  heap.emplace_back(new Task(
    runtime.heap.root(job),
    dir->as_str(),
    stdin->as_str(),
    env->as_str(),
    cmd->as_str()));
  std::push_heap(heap.begin(), heap.end());

  // If a scheduled job claims a longer critical path, we need to adjust the total path time
  if (job->pathtime >= status_state.remain) {
#ifdef DEBUG_PROGRESS
    std::cerr << "RUN RAISE CRIT: "
      << status_state.remain << " => " << job->pathtime << "  /  "
      << status_state.total  << " => " << (job->pathtime + status_state.total - status_state.remain) << std::endl;
#endif
    status_state.total = job->pathtime + (status_state.total - status_state.remain);
    status_state.remain = job->pathtime;
    status_state.current = job->record.runtime;
  }

  RETURN(claim_unit(runtime.heap));
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

  size_t need = reserve_unit() + WJob::reserve();
  runtime.heap.reserve(need);

  parse_usage(&job->predict, args+3, runtime, scope);
  job->predict.found = true;
  job->reality = job->predict;

  if (stdout->length != 0)
    job->db->save_output(job->job, 1, stdout->c_str(), stdout->length, 0);
  if (stderr->length != 0)
    job->db->save_output(job->job, 2, stderr->c_str(), stderr->length, 0);

  REQUIRE (job->state == 0);

  if (LOG_ECHO(job->log)) {
    std::stringstream s;
    s << pretty_cmd(job->cmdline->as_str());
    if (job->stdin->length != 0) s << " < " << job->stdin->c_str();
    s << std::endl;
    std::string out = s.str();
    status_write(1, out.data(), out.size());
  }

  job->state = STATE_FORKED|STATE_STDOUT|STATE_STDERR|STATE_MERGED;

  runtime.schedule(WJob::claim(runtime.heap, job));
  RETURN(claim_unit(runtime.heap));
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
  INTEGER_MPZ(keep, 5);
  INTEGER_MPZ(log, 6);

  Job *out = Job::alloc(
    runtime.heap,
    jobtable->imp->db,
    dir,
    stdin,
    env,
    cmd,
    mpz_cmp_si(keep,0),
    mpz_get_si(log));

  out->record = jobtable->imp->db->predict_job(out->code.data[0], &out->pathtime);

  std::stringstream stack;
  for (auto &i : scope->stack_trace()) stack << i.file() << std::endl;

  out->db->insert_job(
    dir->as_str(),
    stdin->as_str(),
    env->as_str(),
    cmd->as_str(),
    visible->as_str(),
    stack.str(),
    &out->job);

  RETURN(out);
}

static size_t reserve_tree(const std::vector<FileReflection> &files) {
  size_t need = reserve_list(files.size());
  for (auto &i : files)
    need += reserve_tuple2()
            + String::reserve(i.path.size())
            + String::reserve(i.hash.size());
  return need;
}

static HeapObject *claim_tree(Heap &h, const std::vector<FileReflection> &files) {
  std::vector<HeapObject*> vals;
  vals.reserve(files.size());
  for (auto &i : files)
    vals.emplace_back(claim_tuple2(h,
      String::claim(h, i.path),
      String::claim(h, i.hash)));
  return claim_list(h, vals.size(), vals.data());
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

  // This function can be rerun; it's side effect has no impact on re-execution of reuse_job.
  long job;
  double pathtime;
  std::vector<FileReflection> files;
  Usage reuse = jobtable->imp->db->reuse_job(
    dir->as_str(),
    stdin->as_str(),
    env->as_str(),
    cmd->as_str(),
    visible->as_str(),
    jobtable->imp->check,
    job,
    files,
    &pathtime);

  size_t need = reserve_tuple2() + reserve_tree(files) + reserve_list(1) + Job::reserve();
  runtime.heap.reserve(need);

  HeapObject *joblist;
  if (reuse.found && !jobtable->imp->check) {
    Job *jobp = Job::claim(runtime.heap, jobtable->imp->db, dir, stdin, env, cmd, true, 0);
    jobp->state = STATE_FORKED|STATE_STDOUT|STATE_STDERR|STATE_MERGED|STATE_FINISHED;
    jobp->job = job;
    jobp->record = reuse;
    // predict + reality unusued since Job not run
    jobp->report = reuse;
    jobp->reality = reuse;
    jobp->pathtime = pathtime;

    HeapObject *obj = jobp;
    joblist = claim_list(runtime.heap, 1, &obj);

    // Even though this job is not run, it might have been the 'next' job of something that DID run
    if (pathtime >= status_state.remain && pathtime*ALMOST_ONE*ALMOST_ONE <= status_state.remain) {
      auto crit = jobtable->imp->critJob(ALMOST_ONE * (pathtime - reuse.runtime));
#ifdef DEBUG_PROGRESS
      std::cerr << "DECREASE CRIT: "
        << status_state.remain << " => " << crit.pathtime << "  /  "
        << status_state.total  << " => " << (status_state.total - status_state.remain - crit.pathtime) << std::endl;
#endif
      status_state.total = crit.pathtime + (status_state.total - status_state.remain);
      status_state.remain = crit.pathtime;
      status_state.current = crit.runtime;
      if (crit.runtime == 0) gettimeofday(&jobtable->imp->wall, 0);
    }
  } else {
    joblist = claim_list(runtime.heap, 0, nullptr);
  }

  RETURN(claim_tuple2(runtime.heap, joblist, claim_tree(runtime.heap, files)));
}

static size_t reserve_usage(const Usage &usage) {
  MPZ s(usage.status);
  MPZ m(usage.membytes);
  MPZ i(usage.ibytes);
  MPZ o(usage.obytes);
  return Integer::reserve(s)
       + Double::reserve()
       + Double::reserve()
       + Integer::reserve(m)
       + Integer::reserve(i)
       + Integer::reserve(o)
       + reserve_tuple2() * 5;
}

static HeapObject *claim_usage(Heap &h, const Usage &usage) {
  MPZ s(usage.status);
  MPZ m(usage.membytes);
  MPZ i(usage.ibytes);
  MPZ o(usage.obytes);
  return
    claim_tuple2(h,
      claim_tuple2(h,
        Integer::claim(h, s),
        Double::claim(h, usage.runtime)),
      claim_tuple2(h,
        claim_tuple2(h,
          Double::claim(h, usage.cputime),
          Integer::claim(h, m)),
        claim_tuple2(h,
          Integer::claim(h, i),
          Integer::claim(h, o))));
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
  INTEGER_MPZ(arg1, 1);

  if (mpz_cmp_si(arg1, 1) == 0) {
    runtime.schedule(WJob::alloc(runtime.heap, arg0));
    continuation->next = arg0->q_stdout;
    arg0->q_stdout = continuation;
  } else if (mpz_cmp_si(arg1, 2) == 0) {
    runtime.schedule(WJob::alloc(runtime.heap, arg0));
    continuation->next = arg0->q_stderr;
    arg0->q_stderr = continuation;
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
  INTEGER_MPZ(arg1, 1);

  runtime.heap.reserve(reserve_unit());

  if (mpz_cmp_si(arg1, 256) < 0 && mpz_cmp_si(arg1, 0) > 0) {
    int sig = mpz_get_si(arg1);
    if ((arg0->state & STATE_FORKED) && !(arg0->state & STATE_MERGED))
      kill(arg0->pid, sig);
  }

  RETURN(claim_unit(runtime.heap));
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
  INTEGER_MPZ(arg1, 1);

  if (mpz_cmp_si(arg1, 1) == 0) {
    runtime.schedule(WJob::alloc(runtime.heap, arg0));
    continuation->next = std::move(arg0->q_inputs);
    arg0->q_inputs = std::move(continuation);
  } else if (mpz_cmp_si(arg1, 2) == 0) {
    runtime.schedule(WJob::alloc(runtime.heap, arg0));
    continuation->next = std::move(arg0->q_outputs);
    arg0->q_outputs = std::move(continuation);
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
  MPZ out(arg0->job);
  RETURN(Integer::alloc(runtime.heap, out));
}

static PRIMTYPE(type_job_desc) {
  return args.size() == 1 &&
    args[0]->unify(Job::typeVar) &&
    out->unify(String::typeVar);
}

static PRIMFN(prim_job_desc) {
  EXPECT(1);
  JOB(arg0, 0);
  RETURN(String::alloc(runtime.heap, pretty_cmd(arg0->cmdline->as_str())));
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

  REQUIRE(job->state & STATE_MERGED);
  REQUIRE(!(job->state & STATE_FINISHED));

  size_t need = WJob::reserve() + reserve_unit();
  runtime.heap.reserve(need);

  parse_usage(&job->report, args+3, runtime, scope);
  job->report.found = true;

  bool keep = !job->bad_launch && !job->bad_finish && job->keep && job->report.status == 0;
  job->db->finish_job(job->job, inputs->as_str(), outputs->as_str(), job->code.data[0], keep, job->report);
  job->state |= STATE_FINISHED;

  runtime.schedule(WJob::claim(runtime.heap, job));
  RETURN(claim_unit(runtime.heap));
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

static long stat_mod_ns(const char *file) {
  struct stat sbuf;
  if (stat(file, &sbuf) != 0) return -1;
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
  jobtable->imp->db->add_hash(file->as_str(), hash->as_str(), stat_mod_ns(file->c_str()));
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
  std::string hash = jobtable->imp->db->get_hash(file->as_str(), stat_mod_ns(file->c_str()));
  RETURN(String::alloc(runtime.heap, hash));
}

static PRIMTYPE(type_get_modtime) {
  return args.size() == 1 &&
    args[0]->unify(String::typeVar) &&
    out->unify(Integer::typeVar);
}

static PRIMFN(prim_get_modtime) {
  EXPECT(1);
  STRING(file, 0);
  MPZ out(stat_mod_ns(file->c_str()));
  RETURN(Integer::alloc(runtime.heap, out));
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

  auto out = find_in_path(exec->as_str(), path->as_str());
  RETURN(String::alloc(runtime.heap, out));
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
  runtime.schedule(WJob::alloc(runtime.heap, job));
  continuation->next = job->q_reality;
  job->q_reality = continuation;
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
  runtime.schedule(WJob::alloc(runtime.heap, job));
  continuation->next = job->q_report;
  job->q_report = continuation;
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

  size_t need = reserve_usage(job->record) + reserve_list(1);
  runtime.heap.reserve(need);

  if (job->record.found) {
    HeapObject *obj = claim_usage(runtime.heap, job->record);
    RETURN(claim_list(runtime.heap, 1, &obj));
  } else {
    RETURN(claim_list(runtime.heap, 0, nullptr));
  }
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
  INTEGER_MPZ(kind, 1);

  runtime.heap.reserve(reserve_bool());
  int mode = R_OK;
  if (mpz_cmp_si(kind, 1) == 0) mode = W_OK;
  if (mpz_cmp_si(kind, 2) == 0) mode = X_OK;
  RETURN(claim_bool(runtime.heap, access(file->c_str(), mode) == 0));
}

void prim_register_job(JobTable *jobtable, PrimMap &pmap) {
  prim_register(pmap, "job_cache",  prim_job_cache,  type_job_cache,   0, jobtable);
  prim_register(pmap, "job_create", prim_job_create, type_job_create,  0, jobtable);
  prim_register(pmap, "job_launch", prim_job_launch, type_job_launch,  0, jobtable);
  prim_register(pmap, "job_virtual",prim_job_virtual,type_job_virtual, 0, jobtable);
  prim_register(pmap, "job_finish", prim_job_finish, type_job_finish,  0);
  prim_register(pmap, "job_fail_launch", prim_job_fail_launch, type_job_fail, 0);
  prim_register(pmap, "job_fail_finish", prim_job_fail_finish, type_job_fail, 0);
  prim_register(pmap, "job_kill",   prim_job_kill,   type_job_kill,    0);
  prim_register(pmap, "job_output", prim_job_output, type_job_output,  PRIM_PURE);
  prim_register(pmap, "job_tree",   prim_job_tree,   type_job_tree,    PRIM_PURE);
  prim_register(pmap, "job_id",     prim_job_id,     type_job_id,      PRIM_PURE);
  prim_register(pmap, "job_desc",   prim_job_desc,   type_job_desc,    PRIM_PURE);
  prim_register(pmap, "job_reality",prim_job_reality,type_job_reality, PRIM_PURE);
  prim_register(pmap, "job_report", prim_job_report, type_job_report,  PRIM_PURE);
  prim_register(pmap, "job_record", prim_job_record, type_job_record,  PRIM_PURE);
  prim_register(pmap, "add_hash",   prim_add_hash,   type_add_hash,    0, jobtable);
  // These are not pure, because they can't be reordered freely:
  prim_register(pmap, "get_hash",   prim_get_hash,   type_get_hash,    0, jobtable);
  prim_register(pmap, "get_modtime",prim_get_modtime,type_get_modtime, 0);
  prim_register(pmap, "search_path",prim_search_path,type_search_path, 0);
  prim_register(pmap, "access",     prim_access,     type_access,      0);
}

static void wake(Runtime &runtime, HeapPointer<Continuation> &q, HeapObject *value) {
  Continuation *c = q.get();
  while (c->next) {
    c->value = value;
    c = static_cast<Continuation*>(c->next.get());
  }
  c->value = value;
  c->next = runtime.stack;
  runtime.stack = q;
  q.reset();
}

void WJob::execute(Runtime &runtime) {
  // This function is a bit sneaky.
  // We don't reserve memory for all potential wake-up events up-front.
  // Instead, we allocate as we go, even causing side effects.
  // Each side-effect is guarded by an 'if' so it only happens the first time.

  if ((job->state & STATE_STDOUT) && job->q_stdout) {
    HeapObject *what;
    if (job->bad_launch) {
      runtime.heap.reserve(reserve_result());
      what = claim_result(runtime.heap, false, job->bad_launch.get());
    } else {
      std::string out(job->db->get_output(job->job, 1));
      runtime.heap.reserve(reserve_result() + String::reserve(out.size()));
      what = claim_result(runtime.heap, true, String::claim(runtime.heap, out));
    }
    wake(runtime, job->q_stdout, what);
  }

  if ((job->state & STATE_STDERR) && job->q_stderr) {
    HeapObject *what;
    if (job->bad_launch) {
      runtime.heap.reserve(reserve_result());
      what = claim_result(runtime.heap, false, job->bad_launch.get());
    } else {
      std::string out(job->db->get_output(job->job, 2));
      runtime.heap.reserve(reserve_result() + String::reserve(out.size()));
      what = claim_result(runtime.heap, true, String::claim(runtime.heap, out));
    }
    wake(runtime, job->q_stderr, what);
  }

  if ((job->state & STATE_MERGED) && job->q_reality) {
    HeapObject *what;
    if (job->bad_launch) {
      runtime.heap.reserve(reserve_result());
      what = claim_result(runtime.heap, false, job->bad_launch.get());
    } else {
      runtime.heap.reserve(reserve_result() + reserve_usage(job->reality));
      what = claim_result(runtime.heap, true, claim_usage(runtime.heap, job->reality));
    }
    wake(runtime, job->q_reality, what);
  }

  if ((job->state & STATE_FINISHED) && job->q_inputs) {
    HeapObject *what;
    if (job->bad_finish) {
      runtime.heap.reserve(reserve_result());
      what = claim_result(runtime.heap, false, job->bad_finish.get());
    } else {
      auto files = job->db->get_tree(1, job->job);
      runtime.heap.reserve(reserve_result() + reserve_tree(files));
      what = claim_result(runtime.heap, true, claim_tree(runtime.heap, files));
    }
    wake(runtime, job->q_inputs, what);
  }

  if ((job->state & STATE_FINISHED) && job->q_outputs) {
    HeapObject *what;
    if (job->bad_finish) {
      runtime.heap.reserve(reserve_result());
      what = claim_result(runtime.heap, false, job->bad_finish.get());
    } else {
      auto files = job->db->get_tree(2, job->job);
      runtime.heap.reserve(reserve_result() + reserve_tree(files));
      what = claim_result(runtime.heap, true, claim_tree(runtime.heap, files));
    }
    wake(runtime, job->q_outputs, what);
  }

  if ((job->state & STATE_FINISHED) && job->q_report) {
    HeapObject *what;
    if (job->bad_finish) {
      runtime.heap.reserve(reserve_result());
      what = claim_result(runtime.heap, false, job->bad_finish.get());
    } else {
      runtime.heap.reserve(reserve_result() + reserve_usage(job->report));
      what = claim_result(runtime.heap, true, claim_usage(runtime.heap, job->report));
    }
    wake(runtime, job->q_report, what);
  }
}
