#ifndef DATABASE_H
#define DATABASE_H

#include <memory>
#include <string>
#include <vector>

struct FileReflection {
  std::string path;
  std::string hash;
  FileReflection(std::string &&path_, std::string &&hash_) : path(std::move(path_)), hash(std::move(hash_)) { }
};

struct JobReflection {
  long job;
  std::string directory;
  std::vector<std::string> commandline;
  std::vector<std::string> environment;
  std::string stack;
  std::string stdin;
  std::string time;
  int status;
  double runtime;
  std::vector<FileReflection> inputs;
  std::vector<FileReflection> outputs;
};

struct Database {
  struct detail;
  std::unique_ptr<detail> imp;

  Database();
  ~Database();

  std::string open();
  void close();

  std::vector<std::string> get_targets();
  void add_target(const std::string &target);
  void del_target(const std::string &target);

  void prepare(); // prepare for job execution
  void clean(bool verbose); // finished execution; sweep stale files

  void begin_txn();
  void end_txn();

  bool reuse_job(
    const std::string &directory,
    const std::string &stdin, // "" -> /dev/null
    const std::string &environment,
    const std::string &commandline,
    const std::string &visible,
    long *job);
  void insert_job( // also wipes out any old runs
    const std::string &directory,
    const std::string &stdin, // "" -> /dev/null
    const std::string &environment,
    const std::string &commandline,
    // ^^^ only these matter to identify the job
    const std::string &stack,
    long   *job); // key used for accesses below
  void finish_job(
    long job,
    const std::string &inputs,  // null separated
    const std::string &outputs, // null separated
    int status,
    double runtime);
  std::vector<FileReflection> get_tree(int kind, long job);

  void save_output( // call only if needs_build -> true
    long job,
    int descriptor,
    const char *buffer,
    int size,
    double runtime);
  std::string get_output(
    long job,
    int descriptor);

  void add_hash(
    const std::string &file,
    const std::string &hash,
    long modified);

  std::string get_hash(
    const std::string &file,
    long modified);

  std::vector<JobReflection> explain(
    const std::string &file,
    int use);
};

#endif
