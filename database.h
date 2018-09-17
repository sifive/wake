#ifndef DATABASE_H
#define DATABASE_H

#include <memory>
#include <string>
#include <vector>

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

  bool needs_build(
    int   cache, // 0 -> always need rebuild
    const std::string &directory,
    const std::string &commandline,
    const std::string &environment,
    const std::string &stdin, // "" -> /dev/null
    // ^^^ only these matter to identify the job
    const std::string &visible_files, // null separated
    // ^^^ a rebuild will be necessary if the hashes of inputs or stdin disagree
    const std::string &stack,
    long   *job); // key used for accesses below
  void save_job(
    long job,
    const std::string &inputs,  // null separated
    const std::string &outputs, // null separated
    int status,
    double runtime);
  std::vector<std::string> get_inputs(long job);
  std::vector<std::string> get_outputs(long job);

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
    const std::string &hash);
};

#endif
