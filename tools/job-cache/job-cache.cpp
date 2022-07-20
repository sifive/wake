/*
 * Copyright 2022 SiFive, Inc.
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

// Open Group Base Specifications Issue 7
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <json/json5.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <util/mkdir_parents.h>
#include <wcl/filepath.h>
#include <wcl/trie.h>
#include <wcl/xoshiro_256.h>

#include <algorithm>
#include <iostream>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "bloom.h"
#include "logging.h"
#include "unique_fd.h"

// moves the file or directory, crashes on error
static void rename_no_fail(const char *old_path, const char *new_path) {
  if (rename(old_path, new_path) < 0) {
    log_fatal("rename(%s, %s): %s", old_path, new_path, strerror(errno));
  }
}

// Ensures the the given directory has been created
static void mkdir_no_fail(const char *dir) {
  if (mkdir(dir, 0777) < 0 && errno != EEXIST) {
    log_fatal("mkdir(%s): %s", dir, strerror(errno));
  }
}

// Ensures the given file has been deleted
static void unlink_no_fail(const char *file) {
  if (unlink(file) < 0) {
    log_fatal("unlink(%s): %s", file, strerror(errno));
  }
}

// Ensures the the given directory no longer exists
static void rmdir_no_fail(const char *dir) {
  if (rmdir(dir) < 0 && errno != ENOENT) {
    log_fatal("rmdir(%s): %s", dir, strerror(errno));
  }
}

// This function first attempts to reflink but if that isn't
// supported by the filesystem it copies instead.
#ifdef __APPLE__

static void copy(int src_fd, int dst_fd) {
  // TODO: Actully make this work. APFS supports reflinking so
  //       it should be possible to do this correctly
}

#elif __GLIBC__ >= 2 && __GLIBC_MINOR__ >= 27

#include <linux/fs.h>

// This function just uses `copy_file_range` to make
// an efficent copy
static void copy(int src_fd, int dst_fd) {
  struct stat buf;
  if (fstat(src_fd, &buf) < 0) {
    log_fatal("fstat(src_fd = %d): %s", src_fd, strerror(errno));
  }
  if (copy_file_range(src_fd, nullptr, dst_fd, nullptr, buf.st_size, 0) < 0) {
    log_fatal("copy_file_range(src_fd = %d, NULL, dst_fd = %d, size = %d, 0): %s", src_fd, dst_fd,
              buf.st_size, strerror(errno));
  }
}

#else
static void copy(int src_fd, int dst_fd) {
  // TODO: Write a fallback. This should only be needed for centos7.6
}
#endif

#ifdef FICLONE

static void copy_or_reflink(const char *src, const char *dst) {
  auto src_fd = UniqueFd::open(src, O_RDONLY);
  auto dst_fd = UniqueFd::open(dst, O_WRONLY | O_CREAT, 0644);

  if (ioctl(dst_fd.get(), FICLONE, src_fd.get()) < 0) {
    if (errno != EINVAL && errno != EOPNOTSUPP) {
      log_fatal("ioctl(%s, FICLONE, %d): %s", dst, src, strerror(errno));
    }
    copy(src_fd.get(), dst_fd.get());
  }
}

#else

static void copy_or_reflink(const char *src, const char *dst) {
  auto src_fd = UniqueFd::open(src, O_RDONLY);
  auto dst_fd = UniqueFd::open(dst, O_WRONLY | O_CREAT, 0644);

  copy(src_fd.get(), dst_fd.get());
}

#endif

class Database {
 private:
  sqlite3 *db = nullptr;

 public:
  Database(const Database &) = delete;
  Database(Database &&other) {
    db = other.db;
    other.db = nullptr;
  }
  Database() = delete;
  ~Database() {
    if (sqlite3_close(db) != SQLITE_OK) {
      log_fatal("Could not close database: %s", sqlite3_errmsg(db));
    }
  }
  Database(const std::string &cache_dir) {
    // We want to keep a sql file that has proper syntax highlighting
    // around instead of embeding the schema. In order to acomplish this
    // we use C++11 raw strings and the preprocessor. Unfortuently
    // since starting a sql file with `R("` causes it to all highlight
    // as a string we need to work around that. In sql `--` is a comment
    // starter but in C++ its decrement. We take advantage of this by
    // adding `--dummy, R("` to the start of the sql file which allows
    // it to be valid and have no effect in both languages. Thus
    // this dummy variable is needed at the import site. Additionally
    // because the comma is lower precedence than the '=' operator we
    // have to put parens around the include to get this trick to work.
    // clang-format off
    int dummy = 0;
    const char* cache_schema = (
        #include "schema.sql"
    );
    // clang-format on

    // Make sure the cache directory exists
    mkdir_no_fail(cache_dir.c_str());

    std::string db_path = cache_dir + "/cache.db";
    if (sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                        nullptr) != SQLITE_OK) {
      log_fatal("error: %s", sqlite3_errmsg(db));
    }

    // If we happen to open the db as read only we need to fail.
    // TODO: Why would this happen?
    if (sqlite3_db_readonly(db, 0)) {
      log_fatal("error: cache.db is read-only");
    }

    char *fail = nullptr;
    if (sqlite3_exec(db, cache_schema, nullptr, nullptr, &fail) != SQLITE_OK) {
      log_fatal("error: failed init stmt: %s: %s", fail, sqlite3_errmsg(db));
    }
  }

  sqlite3 *get() const { return db; }
};

class PreparedStatement {
 private:
  std::shared_ptr<Database> db = nullptr;
  sqlite3_stmt *query_stmt = nullptr;
  std::string why = "";

 public:
  PreparedStatement() = delete;
  PreparedStatement(const PreparedStatement &) = delete;
  PreparedStatement(PreparedStatement &&pstmt) {
    db = pstmt.db;
    query_stmt = pstmt.query_stmt;
    pstmt.db = nullptr;
    query_stmt = nullptr;
  }
  PreparedStatement &operator=(PreparedStatement &&pstmt) {
    db = pstmt.db;
    query_stmt = pstmt.query_stmt;
    pstmt.db = nullptr;
    query_stmt = nullptr;
    return *this;
  }

  PreparedStatement(std::shared_ptr<Database> db, const std::string &sql_str) : db(db) {
    if (sqlite3_prepare_v2(db->get(), sql_str.c_str(), sql_str.size(), &query_stmt, nullptr) !=
        SQLITE_OK) {
      log_fatal("error: failed to prepare statement: %s", sqlite3_errmsg(db->get()));
    }
  }

  ~PreparedStatement() {
    if (query_stmt) {
      int ret = sqlite3_finalize(query_stmt);
      if (ret != SQLITE_OK) {
        log_fatal("sqlite3_finalize: %s", sqlite3_errmsg(db->get()));
      }
      query_stmt = nullptr;
    }
  }

  void set_why(std::string why) { this->why = std::move(why); }

  void bind_integer(int64_t index, int64_t value) {
    int ret = sqlite3_bind_int64(query_stmt, index, value);
    if (ret != SQLITE_OK) {
      log_fatal("%s: sqlite3_bind_int64(%d, %d): %s", why.c_str(), index, value,
                sqlite3_errmsg(db->get()));
    }
  }

  void bind_string(int64_t index, const std::string &value) {
    int ret = sqlite3_bind_text(query_stmt, index, value.c_str(), value.size(), SQLITE_TRANSIENT);
    if (ret != SQLITE_OK) {
      log_fatal("%s: sqlite3_bind_text(%d, %s): %s", why.c_str(), index, value.c_str(),
                sqlite3_errmsg(sqlite3_db_handle(query_stmt)));
    }
  }

  int64_t read_integer(int64_t index) { return sqlite3_column_int64(query_stmt, index); }

  std::string read_string(int64_t index) {
    const char *str = reinterpret_cast<const char *>(sqlite3_column_text(query_stmt, index));
    size_t size = sqlite3_column_bytes(query_stmt, index);
    return std::string(str, size);
  }

  void reset() {
    if (sqlite3_reset(query_stmt) != SQLITE_OK) {
      log_fatal("error: %s; sqlite3_reset: %s", why.c_str(), sqlite3_errmsg(db->get()));
    }

    if (sqlite3_clear_bindings(query_stmt) != SQLITE_OK) {
      log_fatal("error: %s; sqlite3_clear_bindings: %s", why.c_str(), sqlite3_errmsg(db->get()));
    }
  }

  int step() {
    int ret = sqlite3_step(query_stmt);
    if (ret == SQLITE_MISUSE || ret == SQLITE_ERROR) {
      log_fatal("error: %s; sqlite3_step: %s", why.c_str(), sqlite3_errmsg(db->get()));
    }
    return ret;
  }
};

// Database and JSON classes
class InputFiles {
 private:
  PreparedStatement add_input_file;

 public:
  static constexpr const char *insert_query =
      "insert into input_files (path, hash, job) values (?, ?, ?)";

  InputFiles(std::shared_ptr<Database> db) : add_input_file(db, insert_query) {
    add_input_file.set_why("Could not insert input file");
  }

  void insert(const std::string &path, Hash256 hash, int64_t job_id) {
    add_input_file.bind_string(1, path);
    add_input_file.bind_string(2, hash.to_hex());
    add_input_file.bind_integer(3, job_id);
    add_input_file.step();
    add_input_file.reset();
  }
};

class InputDirs {
 private:
  PreparedStatement add_input_dir;

 public:
  static constexpr const char *insert_query =
      "insert into input_dirs (path, hash, job) values (?, ?, ?)";

  InputDirs(std::shared_ptr<Database> db) : add_input_dir(db, insert_query) {
    add_input_dir.set_why("Could not insert input directory");
  }

  void insert(const std::string &path, Hash256 hash, int64_t job_id) {
    add_input_dir.bind_string(1, path);
    add_input_dir.bind_string(2, hash.to_hex());
    add_input_dir.bind_integer(3, job_id);
    add_input_dir.step();
    add_input_dir.reset();
  }
};

class OutputFiles {
 private:
  PreparedStatement add_output_file;

 public:
  static constexpr const char *insert_query =
      "insert into output_files (path, hash, job) values (?, ?, ?)";

  OutputFiles(std::shared_ptr<Database> db) : add_output_file(db, insert_query) {
    add_output_file.set_why("Could not insert output file");
  }

  void insert(const std::string &path, Hash256 hash, int64_t job_id) {
    add_output_file.bind_string(1, path);
    add_output_file.bind_string(2, hash.to_hex());
    add_output_file.bind_integer(3, job_id);
    add_output_file.step();
    add_output_file.reset();
  }
};

class JobTable {
 private:
  std::shared_ptr<Database> db;
  PreparedStatement add_job;

 public:
  static constexpr const char *insert_query =
      "insert into jobs (directory, commandline, environment, stdin, bloom_filter)"
      "values (?, ?, ?, ?, ?)";

  JobTable(std::shared_ptr<Database> db) : db(db), add_job(db, insert_query) {
    add_job.set_why("Could not insert job");
  }

  int64_t insert(const std::string &cwd, const std::string &cmd, const std::string &env,
                 const std::string &stdin_str, BloomFilter bloom) {
    int64_t bloom_integer = *reinterpret_cast<const int64_t *>(bloom.data());
    add_job.bind_string(1, cwd);
    add_job.bind_string(2, cmd);
    add_job.bind_string(3, env);
    add_job.bind_string(4, stdin_str);
    add_job.bind_integer(5, bloom_integer);
    add_job.step();
    int64_t job_id = sqlite3_last_insert_rowid(db->get());
    add_job.reset();
    return job_id;
  }
};

struct CachedOutputFile {
  std::string path;
  Hash256 hash;
};

struct MatchingJob {
  int64_t job_id;
  std::vector<CachedOutputFile> output_files;
};

// Returns the end of the parent directory in the path.
wcl::optional<std::pair<std::string, std::string>> parent_and_base(const std::string &str) {
  // traverse backwards but using a normal iterator instead of a reverse
  // iterator.
  auto rbegin = str.end() - 1;
  auto rend = str.begin();
  for (; rbegin >= rend; --rbegin) {
    if (*rbegin == '/') {
      // Advance to the character past the slash
      rbegin++;
      // Now return the two strings
      return {wcl::in_place_t{}, std::string(rend, rbegin), std::string(rbegin, str.end())};
    }
  }

  return {};
}

struct FindJobRequest {
 public:
  std::string cwd;
  std::string command_line;
  std::string envrionment;
  std::string stdin_str;
  wcl::trie<std::string, std::string> dir_redirects;
  BloomFilter bloom;
  // Using an ordered map is a neat trick here. It
  // gives us repeatable hashes on directories
  // later.
  std::map<std::string, Hash256> visible;
  std::unordered_map<std::string, Hash256> dir_hashes;

  FindJobRequest() = delete;
  FindJobRequest(const FindJobRequest &) = default;
  FindJobRequest(FindJobRequest &&) = default;

  explicit FindJobRequest(const JAST &find_job_json) {
    cwd = find_job_json.get("cwd").value;
    command_line = find_job_json.get("command_line").value;
    envrionment = find_job_json.get("envrionment").value;
    stdin_str = find_job_json.get("stdin").value;

    // Read the input files, and compute the directory hashes as we go.
    for (const auto &input_file : find_job_json.get("input_files").children) {
      std::string path = input_file.second.get("path").value;
      Hash256 hash = Hash256::from_hex(input_file.second.get("hash").value);
      bloom.add_hash(hash);
      visible[std::move(path)] = hash;
    }

    // Now accumulate the hashables in the directory.
    std::unordered_map<std::string, std::string> dirs;
    // NOTE: `visible` is already sorted because its an std::map.
    // this means that we'll accumulate directories correctly.
    for (const auto &input : visible) {
      auto pair = parent_and_base(input.first);
      if (!pair) continue;
      std::string parent = std::move(pair->first);
      std::string base = std::move(pair->second);
      dirs[parent] += base;
      dirs[parent] += ":";
    }

    // Now actully perform those hashes
    for (auto dir : dirs) {
      dir_hashes[dir.first] = Hash256::blake2b(dir.second);
    }

    // When outputting files we need to map sandbox dirs to output dirs.
    // Collect those redirects here.
    for (const auto &dir_redirect : find_job_json.get("dir_redirects").children) {
      auto dir_range = wcl::make_filepath_range(dir_redirect.first);
      dir_redirects.move_emplace(dir_range.begin(), dir_range.end(), dir_redirect.second.value);
    }
  }
};

class Transaction {
 private:
  PreparedStatement begin_txn_query;
  PreparedStatement commit_txn_query;

 public:
  static constexpr const char *sql_begin_txn = "begin transaction";
  static constexpr const char *sql_commit_txn = "commit transaction";

  Transaction(std::shared_ptr<Database> db)
      : begin_txn_query(db, sql_begin_txn), commit_txn_query(db, sql_commit_txn) {
    begin_txn_query.set_why("Could not begin a transaction");
    commit_txn_query.set_why("Could not commit a transaction");
  }

  template <class F>
  void run(F f) {
    begin_txn_query.step();
    f();
    commit_txn_query.step();
  }
};

class SelectMatchingJobs {
 private:
  PreparedStatement find_jobs;
  PreparedStatement find_files;
  PreparedStatement find_dirs;
  PreparedStatement find_outputs;

  static bool all_match(PreparedStatement &find, int64_t job_id,
                        const FindJobRequest &find_job_request) {
    find.reset();
    find.bind_integer(1, job_id);
    while (find.step() == SQLITE_ROW) {
      std::string path = find.read_string(1);
      Hash256 hash = Hash256::from_hex(find.read_string(2));
      auto iter = find_job_request.visible.find(path);
      if (iter == find_job_request.visible.end() || hash != iter->second) {
        find.reset();
        return false;
      }
    }
    return true;
  }

  std::vector<CachedOutputFile> read_outputs(int64_t job_id) {
    find_outputs.bind_integer(1, job_id);
    std::vector<CachedOutputFile> out;
    while (find_outputs.step() == SQLITE_ROW) {
      CachedOutputFile file;
      file.path = find_outputs.read_string(1);
      file.hash = Hash256::from_hex(find_outputs.read_string(2));
      out.emplace_back(std::move(file));
    }
    find_outputs.reset();
    return out;
  }

 public:
  // First we manually read everything in and we do additional
  // processing on match.
  static constexpr const char *sql_find_jobs =
      "select job_id from jobs"
      "  where directory = ?"
      "  and   commandline = ?"
      "  and   environment = ?"
      "  and   stdin = ?"
      "  and   bloom_filter & ~? = 0";

  // When we find a match we check all of its input files and input directories
  static constexpr const char *sql_find_files = "select * from input_files where job = ?";
  static constexpr const char *sql_input_dirs = "select * from input_dirs where job = ?";

  // Lastly if we find a job we need to read all of its output files
  static constexpr const char *sql_output_files = "select * from output_files where job = ?";

  SelectMatchingJobs(std::shared_ptr<Database> db)
      : find_jobs(db, sql_find_jobs),
        find_files(db, sql_find_files),
        find_dirs(db, sql_input_dirs),
        find_outputs(db, sql_output_files) {
    find_jobs.set_why("Could not find matching jobs");
    find_files.set_why("Could not find files of the given job");
    find_dirs.set_why("Could not find dirs of the given job");
  }

  // NOTE: It is assumed that this is already running inside of a transaction
  wcl::optional<MatchingJob> find(const FindJobRequest &find_job_request) {
    wcl::optional<MatchingJob> out;

    // These parts must match exactly
    find_jobs.bind_string(1, find_job_request.cwd);
    find_jobs.bind_string(2, find_job_request.command_line);
    find_jobs.bind_string(3, find_job_request.envrionment);
    find_jobs.bind_string(4, find_job_request.stdin_str);

    // The bloom filter of a matching job has to be a subset of this one
    uint64_t bloom_integer = *reinterpret_cast<const uint64_t *>(find_job_request.bloom.data());
    find_jobs.bind_integer(5, bloom_integer);

    // Loop over all matching jobs
    while (find_jobs.step() == SQLITE_ROW) {
      // Having found a matching job we need to check all the files
      // and directories have matching hashes.
      int64_t job_id = find_jobs.read_integer(0);
      if (!all_match(find_files, job_id, find_job_request)) continue;
      if (!all_match(find_dirs, job_id, find_job_request)) continue;

      // Ok this is the job, it matches *exactly* so we should
      // expect running it to produce exaxtly the same result.
      MatchingJob result;
      result.job_id = job_id;
      result.output_files = read_outputs(job_id);
      out = {wcl::in_place_t{}, std::move(result)};
      break;
    }

    // Reset find jobs for the next such transaction
    find_jobs.reset();

    // Hopefully we found something
    return out;
  }
};

// JSON parsing stuff
struct InputFile {
  std::string path;
  Hash256 hash;
};

struct InputDir {
  std::string path;
  Hash256 hash;
};

struct OutputFile {
  std::string source;
  std::string path;
  Hash256 hash;
};

struct AddJobRequest {
 public:
  std::string cwd;
  std::string command_line;
  std::string envrionment;
  std::string stdin_str;
  BloomFilter bloom;
  std::vector<InputFile> inputs;
  std::vector<InputDir> directories;
  std::vector<OutputFile> outputs;

  AddJobRequest() = delete;
  AddJobRequest(const AddJobRequest &) = default;
  AddJobRequest(AddJobRequest &&) = default;

  explicit AddJobRequest(const JAST &job_result_json) {
    cwd = job_result_json.get("cwd").value;
    command_line = job_result_json.get("command_line").value;
    envrionment = job_result_json.get("envrionment").value;
    stdin_str = job_result_json.get("stdin").value;

    // Read the input files
    for (const auto &input_file : job_result_json.get("input_files").children) {
      InputFile input;
      input.path = input_file.second.get("path").value;
      input.hash = Hash256::from_hex(input_file.second.get("hash").value);
      bloom.add_hash(input.hash);
      inputs.emplace_back(std::move(input));
    }

    // Read the input dirs
    for (const auto &input_dir : job_result_json.get("input_dirs").children) {
      InputDir input;
      input.path = input_dir.second.get("path").value;
      input.hash = Hash256::from_hex(input_dir.second.get("hash").value);
      bloom.add_hash(input.hash);
      directories.emplace_back(std::move(input));
    }

    // Read the output files
    for (const auto &output_file : job_result_json.get("output_files").children) {
      OutputFile output;
      output.source = output_file.second.get("src").value;
      output.path = output_file.second.get("path").value;
      output.hash = Hash256::from_hex(output_file.second.get("hash").value);
      outputs.emplace_back(std::move(output));
    }
  }
};

// join takes a sequence of strings and concats that
// sequence with some seperator between it. It's like
// python's join method on strings. So ", ".join(seq)
// in python joins a list of strings with a comma. This
// function is a C++ equivlent.
template <class Iter>
static std::string join(char sep, Iter begin, Iter end) {
  std::string out;
  for (; begin != end; ++begin) {
    out += *begin;
    if (begin + 1 != end) out += sep;
  }
  return out;
}

static std::vector<std::string> split_path(const std::string &path) {
  std::vector<std::string> path_vec;
  for (std::string node : wcl::make_filepath_range_ref(path)) {
    path_vec.emplace_back(std::move(node));
  }

  return path_vec;
}

// The very last value is assumed to be a file and
// is not created
template <class Iter>
static void mkdir_all(Iter begin, Iter end) {
  std::string acc;
  for (; begin + 1 != end; ++begin) {
    acc += *begin + "/";
    mkdir_no_fail(acc.c_str());
  }
}

// the `Cache` class provides the full interface
// the the underlying complete cache directory.
// This requires interplay between the file system and
// the database and must be carefully orchestrated. This
// class handles all those details and provides a simple
// interface.
class Cache {
 private:
  std::shared_ptr<Database> db;
  JobTable jobs;
  InputFiles input_files;
  InputDirs input_dirs;
  OutputFiles output_files;
  Transaction transact;
  SelectMatchingJobs matching_jobs;
  std::string dir;
  wcl::xoshiro_256 rng;

 public:
  ~Cache() {}

  Cache() = delete;
  Cache(const Cache &) = delete;

  Cache(std::string _dir)
      : db(std::make_unique<Database>(_dir)),
        jobs(db),
        input_files(db),
        input_dirs(db),
        output_files(db),
        transact(db),
        matching_jobs(db),
        dir(std::move(_dir)),
        rng(wcl::xoshiro_256::get_rng_seed()) {}

  // TODO: Unlike reading, we need to account for
  // directory remappings here.
  wcl::optional<MatchingJob> read(const FindJobRequest &find_request) {
    wcl::optional<MatchingJob> result;

    // We run the matching job in a transaction. This ensures
    // that we get the *complete* set of output files. This
    // allows us to know if one of the job files was tampered
    // with while we're copying files into place.
    transact.run([this, &find_request, &result]() { result = matching_jobs.find(find_request); });

    // Return early if there was no match.
    if (!result) return {};

    // We need a tmp directory to put these outputs into
    std::string tmp_job_dir = dir + "/tmp_outputs_" + rng.unique_name();
    mkdir_no_fail(tmp_job_dir.c_str());

    // We also need to know what directory we're reading out of
    uint8_t group_id = result->job_id & 0xFF;
    std::string job_dir =
        dir + "/" + wcl::to_hex(&group_id) + "/" + std::to_string(result->job_id) + "/";

    // We then hard link each file to a new location atomically.
    // If any of these hard links fail then we fail this read
    // and clean up. This allows job cleanup to occur during
    // a read. That would be an unfortunate situation but its
    // very unlikely to occur so its better to commit the
    // transaction early and suffer the consequences of unlinking
    // one of the files just before we need it.
    std::vector<std::pair<std::string, std::string>> to_copy;
    bool success = true;
    for (const auto &output_file : result->output_files) {
      std::string hash_name = output_file.hash.to_hex();
      std::string cur_file = job_dir + hash_name;
      std::string tmp_file = tmp_job_dir + "/" + hash_name;
      int ret = link(cur_file.c_str(), tmp_file.c_str());
      if (ret < 0) {
        success = false;
        break;
      }
      to_copy.emplace_back(std::make_pair(std::move(tmp_file), output_file.path));
    }

    if (success) {
      // Now copy/reflink all files into their final place
      for (const auto &to_copy : to_copy) {
        const auto &tmp_file = to_copy.first;
        const auto &sandbox_destination = to_copy.second;
        std::vector<std::string> path_vec = split_path(sandbox_destination);

        // So the file that the sandbox wrote to `sandbox_destination` currently
        // lives at `tmp_file` and is safe from interference. The sandbox location
        // needs to be redirected to some other output location however.
        auto pair = find_request.dir_redirects.find_max(path_vec.begin(), path_vec.end());

        // If there is no redirect what so ever, just copy and assume the sandbox
        // had an accurate picture of the current system.
        if (pair.first == nullptr) {
          std::string output_path = "." + sandbox_destination;  // TODO: We need join here
          std::vector<std::string> output_path_vec = split_path(output_path);
          mkdir_all(path_vec.begin(), path_vec.end());
          copy_or_reflink(tmp_file.c_str(), output_path.c_str());
        } else {
          const auto &output_dir = *pair.first;
          const auto &rel_path = join('/', pair.second, path_vec.end());
          std::string output_path = "./" + output_dir + rel_path;  // TODO: We need join here
          std::vector<std::string> output_path_vec = split_path(output_path);
          mkdir_all(output_path_vec.begin(), output_path_vec.end());
          copy_or_reflink(tmp_file.c_str(), output_path.c_str());
        }
      }
    }

    // Now clean up those files in the tempdir
    for (const auto &to_copy : to_copy) {
      unlink_no_fail(to_copy.first.c_str());
    }

    // Lastly clean up the tmp dir itself
    rmdir_no_fail(tmp_job_dir.c_str());

    // If we didn't link all the files over we need to return a failure.
    if (!success) return {};

    // TODO: We should really return a different thing here
    //       that mentions the *output* locations but for
    //       now this is good enough and we can assume
    //       workspace relative paths everywhere.
    return result;
  }

  void add(const AddJobRequest &add_request) {
    // Create a unique name for the job dir (will rename later to correct name)
    std::string tmp_job_dir = dir + "/tmp_" + rng.unique_name();
    mkdir_no_fail(tmp_job_dir.c_str());

    // Copy the output files into the temp dir
    for (const auto &output_file : add_request.outputs) {
      // TODO(jake): See if this file already exists somewhere
      std::string blob_path = tmp_job_dir + "/" + output_file.hash.to_hex();
      copy_or_reflink(output_file.source.c_str(), blob_path.c_str());
    }

    // Start a transaction so that a job is never without its files.
    int64_t job_id;
    transact.run([this, &add_request, &job_id]() {
      job_id = jobs.insert(add_request.cwd, add_request.command_line, add_request.envrionment,
                           add_request.stdin_str, add_request.bloom);

      // Input Files
      for (const auto &input_file : add_request.inputs) {
        input_files.insert(input_file.path, input_file.hash, job_id);
      }

      // Input Dirs
      for (const auto &input_dir : add_request.directories) {
        input_dirs.insert(input_dir.path, input_dir.hash, job_id);
      }

      // Output Files
      for (const auto &output_file : add_request.outputs) {
        output_files.insert(output_file.path, output_file.hash, job_id);
      }

      // We commit the database without having moved the job directory.
      // On *read* you have to be aware tha the database can be in
      // this kind of faulty state where the database is populated but
      // file system is *not* populated. In such a case we interpret that
      // as if it wasn't in the database and so it doesn't get used and
      // will eventully be deleted.
    });

    // Finally we make sure the group directory exits and then
    // atomically rename the temp job into place which completes
    // the insertion. At that point reads should suceed.
    uint8_t job_group = job_id & 0xFF;
    std::string job_group_dir = dir + "/" + wcl::to_hex<uint8_t>(&job_group);
    mkdir_no_fail(job_group_dir.c_str());
    std::string job_dir = job_group_dir + "/" + std::to_string(job_id);
    rename_no_fail(tmp_job_dir.c_str(), job_dir.c_str());
  }
};

int main(int argc, char **argv) {
  // TODO: Add a better CLI
  if (argc < 2) return 1;

  Cache cache(argv[1]);

  if (argc >= 4) {
    if (std::string(argv[2]) == "add") {
      JAST job_result;
      JAST::parse(argv[3], std::cerr, job_result);
      AddJobRequest add_request(job_result);
      cache.add(add_request);
      return 0;
    }

    if (std::string(argv[2]) == "read") {
      JAST job_plan;
      JAST::parse(argv[3], std::cerr, job_plan);
      FindJobRequest find_request(job_plan);
      auto result = cache.read(find_request);
      if (result) {
        std::cout << "Job Found" << std::endl;
      } else {
        std::cout << "Job Not Found" << std::endl;
      }
    }
  }
}
