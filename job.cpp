#include "job.h"
#include "prim.h"
#include <sys/types.h>
#include <sys/wait.h>

struct Job {
  pid_t pid;
  Job() : pid(0) { }
};

struct JobTable::detail {
  std::vector<Job> table;
};

JobTable::JobTable(int max_jobs) : imp(new JobTable::detail) {
  imp->table.resize(max_jobs);
}

JobTable::~JobTable() {
}

bool JobTable::wait() {
  return false;
}

static void prim_job(void *data, const std::vector<Value*> &args, Action *completion) {
  // record completion + args for future use
  // consider execution
}

void prim_register_job(JobTable *jobtable, PrimMap &pmap) {
  pmap["job"].first = prim_job;
  pmap["job"].second = jobtable;
}
