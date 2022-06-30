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

#include <iostream>
#include <string>
#include <vector>

#include "bloom.h"
#include "logging.h"
#include "unique_fd.h"
#include "xoshiro256.h"

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

  int status = 0;
  status = ioctl(dst_fd.get(), FICLONE, src_fd.get());
  if (status < 0) {
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

static inline uint8_t hex_to_nibble(char c) {
  if (c <= '0' && c >= '9') return (c - '0') & 0xF;
  if (c >= 'a' && c <= 'z') return (c - 'a' + 10) & 0xF;
  return (c - 'A' + 10) & 0xF;
}

template <size_t size>
static void get_hex_data(const std::string &s, uint8_t (*data)[size]) {
  uint8_t *start = *data;
  const uint8_t *end = start + size;
  for (size_t i = 0; start < end && i < s.size(); i += 2) {
    start[0] = hex_to_nibble(s[i]);
    if (i + 1 < s.size()) start[0] |= hex_to_nibble(s[i + 1]) << 4;
    ++start;
  }
}

// Use /dev/urandom to get a good seed
static std::tuple<uint64_t, uint64_t, uint64_t, uint64_t> get_rng_seed() {
  auto rng_fd = UniqueFd::open("/dev/urandom", O_RDONLY, 0644);
  uint8_t seed_data[32] = {0};
  if (read(rng_fd.get(), seed_data, sizeof(seed_data)) < 0) {
    log_fatal("read(/dev/urandom): %s", strerror(errno));
  }
  uint64_t *data = reinterpret_cast<uint64_t *>(seed_data);
  return std::make_tuple(data[0], data[1], data[2], data[3]);
}

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
    int ret = sqlite3_bind_text(query_stmt, index, value.c_str(), value.size(), SQLITE_STATIC);
    if (ret != SQLITE_OK) {
      log_fatal("%s: sqlite3_bind_text(%d, %s): %s", why.c_str(), index, value.c_str(),
                sqlite3_errmsg(sqlite3_db_handle(query_stmt)));
    }
  }

  void reset() {
    if (sqlite3_reset(query_stmt) != SQLITE_OK) {
      log_fatal("error: %s; sqlite3_reset: %s", why.c_str(), sqlite3_errmsg(db->get()));
    }

    if (sqlite3_clear_bindings(query_stmt) != SQLITE_OK) {
      log_fatal("error: %s; sqlite3_clear_bindings: %s", why.c_str(), sqlite3_errmsg(db->get()));
    }
  }

  void step() {
    if (sqlite3_step(query_stmt) != SQLITE_DONE) {
      log_fatal("error: %s; sqlite3_step: %s", why.c_str(), sqlite3_errmsg(db->get()));
    }
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

  void insert(const std::string &path, const std::string &hash, int64_t job_id) {
    add_input_file.bind_string(1, path);
    add_input_file.bind_string(2, hash);
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

  void insert(const std::string &path, const std::string &hash, int64_t job_id) {
    add_input_dir.bind_string(1, path);
    add_input_dir.bind_string(2, hash);
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

  void insert(const std::string &path, const std::string &hash, int64_t job_id) {
    add_output_file.bind_string(1, path);
    add_output_file.bind_string(2, hash);
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

// JSON parsing stuff
struct InputFile {
  std::string path;
  std::string hash;
};

struct InputDir {
  std::string path;
  std::string hash;
};

struct OutputFile {
  std::string source;
  std::string path;
  std::string hash;
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

    uint8_t hash_data[64];

    // Read the input files
    for (const auto &input_file : job_result_json.get("input_files").children) {
      InputFile input;
      input.path = input_file.second.get("path").value;
      input.hash = input_file.second.get("hash").value;
      get_hex_data(input.hash, &hash_data);
      bloom.add_hash(reinterpret_cast<const uint8_t *>(hash_data), input.hash.size() / 2);
      inputs.emplace_back(std::move(input));
    }

    // Read the input dirs
    for (const auto &input_dir : job_result_json.get("input_dirs").children) {
      InputDir input;
      input.path = input_dir.second.get("path").value;
      input.hash = input_dir.second.get("hash").value;
      get_hex_data(input.hash, &hash_data);
      bloom.add_hash(reinterpret_cast<const uint8_t *>(hash_data), input.hash.size() / 2);
      directories.emplace_back(std::move(input));
    }

    // Read the output files
    for (const auto &output_file : job_result_json.get("output_files").children) {
      OutputFile output;
      output.source = output_file.second.get("src").value;
      output.path = output_file.second.get("path").value;
      output.hash = output_file.second.get("hash").value;
      outputs.emplace_back(std::move(output));
    }
  }
};

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
  std::string dir;
  Xoshiro256 rng;

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
        dir(std::move(_dir)),
        rng(get_rng_seed()) {}

  void add(const AddJobRequest &add_request) {
    // Create a unique name for the job dir (will rename later to correct name)
    std::string tmp_job_dir = dir + "/tmp_" + rng.unique_name();
    mkdir_no_fail(tmp_job_dir.c_str());

    // Copy the output files into the temp dir
    for (const auto &output_file : add_request.outputs) {
      std::string blob_path = tmp_job_dir + "/" + output_file.hash;
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
    std::string job_group_dir = dir + "/" + to_hex<uint8_t>(&job_group);
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
    }
  }
}
