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

  bool needs_build(
    int   cache, // 0 -> always need rebuild
    const std::string &directory,
    const std::string &commandline,
    const std::string &environment,
    // ^^^ only these matter to identify the job
    const std::string &stdin, // "" -> /dev/null
    const std::string &visible_files, // null separated
    // ^^^ a rebuild will be necessary if the hashes of inputs disagree
    const std::string &stack,
    int   *job); // key used for accesses below
  void save_output( // call only if needs_build -> true
    int job,
    int descriptor,
    const char *buffer,
    int size);
  void save_job(
    int job,
    const std::string &inputs,   // null separated
    const std::string &outputs); // null separated
  std::string get_output(
    int job,
    int descriptor);
  std::vector<std::string> get_inputs(int job);
  std::vector<std::string> get_outputs(int job);
};

#endif
