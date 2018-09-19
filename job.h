#ifndef JOB_H
#define JOB_H

#include <memory>

struct Database;
struct ThunkQueue;

struct JobTable {
  struct detail;
  std::unique_ptr<detail> imp;

  JobTable(Database *db, int max_jobs, bool verbose);
  ~JobTable();

  // Wait for a job to complete; false -> no more active jobs
  bool wait(ThunkQueue &queue);
};

#endif
