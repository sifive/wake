#ifndef JOB_H
#define JOB_H

#include <memory>

struct Database;
struct WorkQueue;

struct JobTable {
  struct detail;
  std::unique_ptr<detail> imp;

  JobTable(Database *db, int max_jobs, bool verbose, bool quiet, bool check);
  ~JobTable();

  // Wait for a job to complete; false -> no more active jobs
  bool wait(WorkQueue &queue);
};

#endif
