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
  static TypeVar typeVar;
  JobResult(Database *db_, const std::string &dir, const std::string &stdin, const std::string &environ, const std::string &cmdline);

  void format(std::ostream &os, int depth) const;
  TypeVar &getType();
  Hash hash() const;

  void process(WorkQueue &queue); // Run commands based on state
};

const char *JobResult::type = "JobResult";

void JobResult::format(std::ostream &os, int p) const {
  if (APP_PRECEDENCE < p) os << "(";
  os << "JobResult " << job;
  if (APP_PRECEDENCE < p) os << ")";
  if (p < 0) os << std::endl;
}

TypeVar JobResult::typeVar("Job", 0);
TypeVar &JobResult::getType() {
  return typeVar;
}

Hash JobResult::hash() const { return code; }

// A Task is a job that is not yet forked
struct Task {
  std::shared_ptr<JobResult> job;
  std::string root;
  std::string dir;
  std::string stdin;
  std::string environ;
  std::string cmdline;
  std::string stack;
  Task(const std::shared_ptr<JobResult> &job_, const std::string &root_, const std::string &dir_, const std::string &stdin_, const std::string &environ_, const std::string &cmdline_, const std::string &stack_)
  : job(job_), root(root_), dir(dir_), stdin(stdin_), environ(environ_), cmdline(cmdline_), stack(stack_) { }
};

// A Job is a forked job not yet merged
struct Job {
  int pool;
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
  std::vector<std::list<Task> > tasks;
  sigset_t sigset;
  Database *db;
  bool verbose;
  bool quiet;
};

static void handle_SIGCHLD(int sig) {
  /* noop -- we just want to interrupt select */
  (void)sig;
}

JobTable::JobTable(Database *db, int max_jobs, bool verbose, bool quiet) : imp(new JobTable::detail) {
  imp->table.resize(max_jobs*POOLS);
  imp->tasks.resize(POOLS);
  imp->verbose = verbose;
  imp->quiet = quiet;
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

static int do_hash_dir() {
  printf("00000000000000000000000000000000\n");
  return 0;
}

static int do_hash(const char *file) {
  struct stat stat;
  int fd;
  uint8_t hash[16];
  void *map;

  fd = open(file, O_RDONLY);
  if (fd == -1) {
    if (errno == EISDIR) return do_hash_dir();
    perror("open");
    return 1;
  }

  if (fstat(fd, &stat) != 0) {
    if (errno == EISDIR) return do_hash_dir();
    perror("fstat");
    return 1;
  }

  if (S_ISDIR(stat.st_mode)) return do_hash_dir();

  if (stat.st_size != 0) {
    map = mmap(0, stat.st_size, PROT_READ, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
      perror("mmap");
      return 1;
    }
  }

  MurmurHash3_x64_128(map, stat.st_size, 42, &hash[0]);
  printf("%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n",
    hash[ 0], hash[ 1], hash[ 2], hash[ 3],
    hash[ 4], hash[ 5], hash[ 6], hash[ 7],
    hash[ 8], hash[ 9], hash[10], hash[11],
    hash[12], hash[13], hash[14], hash[15]);

  return 0;
}

static void launch(JobTable *jobtable) {
  for (auto &i : jobtable->imp->table) {
    if (jobtable->imp->tasks[i.pool].empty()) continue;

    if (i.pid == 0) {
      Task &task = jobtable->imp->tasks[i.pool].front();
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
      std::cout << std::flush;
      std::cerr << std::flush;
      fflush(stdout);
      fflush(stderr);
      i.job->pid = i.pid = fork();
      i.job->state |= STATE_FORKED;
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
        if (task.root != "." && chdir(task.root.c_str())) {
          perror("chdir");
          exit(1);
        }
        if (task.dir != "." && chdir(task.dir.c_str())) {
          perror("chdir");
          exit(1);
        }
        auto cmdline = split_null(task.cmdline);
        auto environ = split_null(task.environ);
        if (!strcmp(cmdline[0], "<hash>")) {
          exit(do_hash(cmdline[1]));
        } else {
          execve(cmdline[0], cmdline, environ);
          perror("execve");
        }
        exit(1);
      }
      close(pipe_stdout[1]);
      close(pipe_stderr[1]);
      for (char &c : task.cmdline) if (c == 0) c = ' ';
      if (!jobtable->imp->quiet && i.pool) std::cerr << task.cmdline << std::endl;
      jobtable->imp->tasks[i.pool].pop_front();
    }
  }
}

bool JobTable::wait(WorkQueue &queue) {
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

    int done = 0;

    if (retval > 0) for (auto &i : imp->table) {
      if (i.pid != 0 && i.pipe_stdout != -1 && FD_ISSET(i.pipe_stdout, &set)) {
        int got = read(i.pipe_stdout, buffer, sizeof(buffer));
        if (got <= 0) {
          close(i.pipe_stdout);
          i.pipe_stdout = -1;
          i.job->state |= STATE_STDOUT;
          i.job->process(queue);
          ++done;
        } else {
          if (imp->verbose) {
            std::cout.write(buffer, got);
          }
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
          ++done;
        } else {
          if (imp->verbose) {
            std::cerr.write(buffer, got);
          }
          i.job->db->save_output(i.job->job, 2, buffer, got, i.runtime(now));
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
          if (imp->verbose && i.pool) std::cerr << i.job->db->get_output(i.job->job, 2);
          i.job->process(queue);
          i.job->runtime = i.runtime(now);
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

static std::unique_ptr<Receiver> cast_jobresult(WorkQueue &queue, std::unique_ptr<Receiver> completion, const std::shared_ptr<Binding> &binding, const std::shared_ptr<Value> &value, JobResult **job) {
  if (value->type != JobResult::type) {
    Receiver::receive(queue, std::move(completion),
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

static PRIMTYPE(type_job_launch) {
  return args.size() == 6 &&
    args[0]->unify(Integer::typeVar) &&
    args[1]->unify(String::typeVar) &&
    args[2]->unify(String::typeVar) &&
    args[3]->unify(String::typeVar) &&
    args[4]->unify(String::typeVar) &&
    args[5]->unify(String::typeVar) &&
    out->unify(JobResult::typeVar);
}

static PRIMFN(prim_job_launch) {
  JobTable *jobtable = reinterpret_cast<JobTable*>(data);
  EXPECT(6);
  INTEGER(pool, 0);
  STRING(root, 1);
  STRING(dir, 2);
  STRING(stdin, 3);
  STRING(env, 4);
  STRING(cmd, 5);

  REQUIRE(mpz_cmp_si(pool->value, 0) >= 0, "Pool must be >= 0");
  REQUIRE(mpz_cmp_si(pool->value, POOLS) < 0, "Pool must be < POOLS");

  std::stringstream stack;
  for (auto &i : binding->stack_trace()) stack << i << std::endl;
  auto out = std::make_shared<JobResult>(
    jobtable->imp->db,
    dir->value,
    stdin->value,
    env->value,
    cmd->value);

  jobtable->imp->tasks[mpz_get_si(pool->value)].emplace_back(
    out,
    root->value,
    dir->value,
    stdin->value,
    env->value,
    cmd->value,
    stack.str());

  launch(jobtable);

  RETURN(out);
}

static PRIMTYPE(type_job_cache) {
  return args.size() == 5 &&
    args[0]->unify(String::typeVar) &&
    args[1]->unify(String::typeVar) &&
    args[2]->unify(String::typeVar) &&
    args[3]->unify(String::typeVar) &&
    args[4]->unify(String::typeVar) &&
    out->unify(JobResult::typeVar);
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

static std::shared_ptr<Value> convert_tree(std::vector<FileReflection> &&files) {
  std::vector<std::shared_ptr<Value> > vals;
  vals.reserve(files.size());
  for (auto &i : files)
    vals.emplace_back(make_tuple(
      std::static_pointer_cast<Value>(std::make_shared<String>(std::move(i.path))),
      std::static_pointer_cast<Value>(std::make_shared<String>(std::move(i.hash)))));
  return make_list(std::move(vals));
}

void JobResult::process(WorkQueue &queue) {
  if ((state & STATE_STDOUT) && q_stdout) {
    auto out = std::make_shared<String>(db->get_output(job, 1));
    std::unique_ptr<Receiver> iter, next;
    for (iter = std::move(q_stdout); iter; iter = std::move(next)) {
      next = std::move(iter->next);
      Receiver::receive(queue, std::move(iter), out);
    }
    q_stdout.reset();
  }

  if ((state & STATE_STDERR) && q_stderr) {
    auto out = std::make_shared<String>(db->get_output(job, 2));
    std::unique_ptr<Receiver> iter, next;
    for (iter = std::move(q_stderr); iter; iter = std::move(next)) {
      next = std::move(iter->next);
      Receiver::receive(queue, std::move(iter), out);
    }
    q_stderr.reset();
  }

  if ((state & STATE_MERGED) && q_merge) {
    auto out = std::make_shared<Integer>(status);
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
    args[0]->unify(JobResult::typeVar) &&
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
    args[0]->unify(JobResult::typeVar) &&
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
    args[0]->unify(JobResult::typeVar) &&
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
    args[0]->unify(JobResult::typeVar) &&
    args[1]->unify(String::typeVar) &&
    args[2]->unify(String::typeVar) &&
    out->unify(Data::typeBoolean);
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

static PRIMTYPE(type_add_hash) {
  TypeVar pair;
  Data::typePair.clone(pair);
  pair[0].unify(String::typeVar);
  pair[1].unify(String::typeVar);
  return args.size() == 2 &&
    args[0]->unify(String::typeVar) &&
    args[1]->unify(String::typeVar) &&
    out->unify(pair);
}

static PRIMFN(prim_add_hash) {
  JobTable *jobtable = reinterpret_cast<JobTable*>(data);
  EXPECT(2);
  STRING(file, 0);
  STRING(hash, 1);
  jobtable->imp->db->add_hash(file->value, hash->value);
  auto out = make_tuple(
    std::shared_ptr<Value>(args[0]),
    std::shared_ptr<Value>(args[1]));
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
  pmap.emplace("job_launch", PrimDesc(prim_job_launch, type_job_launch, jobtable));
  pmap.emplace("job_cache",  PrimDesc(prim_job_cache,  type_job_cache,  jobtable));
  pmap.emplace("job_output", PrimDesc(prim_job_output, type_job_output));
  pmap.emplace("job_kill",   PrimDesc(prim_job_kill,   type_job_kill));
  pmap.emplace("job_tree",   PrimDesc(prim_job_tree,   type_job_tree));
  pmap.emplace("job_finish", PrimDesc(prim_job_finish, type_job_finish));
  pmap.emplace("add_hash",   PrimDesc(prim_add_hash,   type_add_hash,   jobtable));
  pmap.emplace("search_path",PrimDesc(prim_search_path,type_search_path));
}
