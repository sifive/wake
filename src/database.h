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
  std::string stdout;
  std::string stderr;
  int status;
  double runtime;
  std::vector<FileReflection> inputs;
  std::vector<FileReflection> outputs;
};

struct Usage {
  bool found;
  int status; // -signal, +code
  double runtime;
  double cputime;
  uint64_t membytes;
  uint64_t iobytes;
};

struct Database {
  struct detail;
  std::unique_ptr<detail> imp;

  Database(bool debugdb);
  ~Database();

  std::string open(bool wait);
  void close();

  void entropy(uint64_t *key, int words);

  std::vector<std::string> get_targets();
  void add_target(const std::string &target);
  void del_target(const std::string &target);

  void prepare(); // prepare for job execution
  void clean(); // finished execution; sweep stale jobs

  void begin_txn();
  void end_txn();

  Usage reuse_job(
    const std::string &directory,
    const std::string &stdin, // "" -> /dev/null
    const std::string &environment,
    const std::string &commandline,
    const std::string &visible,
    bool check,
    long &job,
    std::vector<FileReflection> &out);
  Usage predict_job(
    uint64_t hashcode);
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
    uint64_t hashcode,
    bool keep,
    Usage reality);
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
    int use,
    bool verbose);
};

#endif
