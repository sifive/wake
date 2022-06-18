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
#include "xoshiro256.h"

// Helper that only returns successful file opens and exits
// otherwise.
static int open_fd(const char *str, int flags) {
  int fd = open(str, flags);
  if (fd == -1) {
    log_fatal("open(%s): %s", str, strerror(errno));
  }
  return fd;
}

// Helper that only returns successful file opens and exits
// otherwise.
static int open_fd(const char *str, int flags, mode_t mode) {
  int fd = open(str, flags, mode);
  if (fd == -1) {
    log_fatal("open(%s): %s", str, strerror(errno));
  }
  return fd;
}

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

// Like close but crashes if an error occurs
static void close_fd(int fd) {
  if (close(fd) == -1) {
    log_fatal("close: %s", strerror(errno));
  }
}

// This function first attempts to reflink but if that isn't
// supported by the filesystem it copies instead.
#ifdef __APPLE__

static void copy_or_reflink(const char *src, const char *dst) {
  // TODO: Actully make this work. APFS supports reflinking so
  //       it should be possible to do this correctly
}

#else

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

static void copy_or_reflink(const char *src, const char *dst) {
  int src_fd = open_fd(src, O_RDONLY);
  int dst_fd = open_fd(dst, O_WRONLY | O_CREAT, 0644);
  if (ioctl(dst_fd, FICLONE, src_fd) < 0) {
    if (errno != EINVAL && errno != EOPNOTSUPP) {
      log_fatal("ioctl(%s, FICLONE, %d): %s", dst, src, strerror(errno));
    }
    copy(src_fd, dst_fd);
  }
  close_fd(dst_fd);
  close_fd(src_fd);
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

static void finish_stmt(const char *why, sqlite3_stmt *stmt) {
  int ret;

  ret = sqlite3_reset(stmt);
  if (ret != SQLITE_OK) {
    log_fatal("error: %s; sqlite3_reset: %s", why, sqlite3_errmsg(sqlite3_db_handle(stmt)));
  }

  ret = sqlite3_clear_bindings(stmt);
  if (ret != SQLITE_OK) {
    log_fatal("error: %s; sqlite3_clear_bindings: %s", why,
              sqlite3_errmsg(sqlite3_db_handle(stmt)));
  }
}

static void single_step(const char *why, sqlite3_stmt *stmt) {
  int ret;

  ret = sqlite3_step(stmt);
  if (ret != SQLITE_DONE) {
    log_fatal("error: %s; sqlite3_step: %s", why, sqlite3_errmsg(sqlite3_db_handle(stmt)));
  }

  finish_stmt(why, stmt);
}

static void bind_string(const char *why, sqlite3_stmt *stmt, int index, const char *str,
                        size_t len) {
  int ret;
  ret = sqlite3_bind_text(stmt, index, str, len, SQLITE_STATIC);
  if (ret != SQLITE_OK) {
    log_fatal("%s: sqlite3_bind_text(%d, %s): %s", why, index, str,
              sqlite3_errmsg(sqlite3_db_handle(stmt)));
  }
}

static void bind_integer(const char *why, sqlite3_stmt *stmt, int index, long x) {
  int ret;
  ret = sqlite3_bind_int64(stmt, index, x);
  if (ret != SQLITE_OK) {
    log_fatal("%s: sqlite3_bind_int64(%d, %d): %s", why, index, x,
              sqlite3_errmsg(sqlite3_db_handle(stmt)));
  }
}

#define FINALIZE(member)                                                  \
  if (member) {                                                           \
    int ret = sqlite3_finalize(member);                                   \
    if (ret != SQLITE_OK) {                                               \
      log_fatal("sqlite3_finalize(%s): %s", #member, sqlite3_errmsg(db)); \
    }                                                                     \
    member = nullptr;                                                     \
  }

#define PREPARE(sql_str, query_stmt)                                                   \
  if (sqlite3_prepare_v2(db, sql_str.c_str(), sql_str.size(), &query_stmt, nullptr) != \
      SQLITE_OK) {                                                                     \
    log_fatal("error: failed to prepare statement: %s", sqlite3_errmsg(db));           \
  }

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

// Use /dev/urandom to get a good seed
static std::tuple<uint64_t, uint64_t, uint64_t, uint64_t> get_rng_seed() {
  int rng_fd = open_fd("/dev/urandom", O_RDONLY, 0644);
  uint8_t seed_data[32] = {0};
  if (read(rng_fd, seed_data, sizeof(seed_data)) < 0) {
    log_fatal("read(/dev/urandom): %s", strerror(errno));
  }
  close_fd(rng_fd);
  uint64_t *data = reinterpret_cast<uint64_t *>(seed_data);
  return std::make_tuple(data[0], data[1], data[2], data[3]);
}

class Cache {
  sqlite3 *db = nullptr;
  sqlite3_stmt *add_job = nullptr;
  sqlite3_stmt *add_input_file = nullptr;
  sqlite3_stmt *add_input_dir = nullptr;
  sqlite3_stmt *add_output_file = nullptr;
  sqlite3_stmt *begin_txn_query = nullptr;
  sqlite3_stmt *commit_txn_query = nullptr;
  std::string dir;
  Xoshiro256 rng;

  void init_sql() {
    std::string cache_schema =
        "pragma auto_vacuum=incremental;"
        "pragma journal_mode=wal;"
        "pragma synchronous=0;"
        "pragma locking_mode=exclusive;"
        "pragma foreign_keys=on;"
        // In order to look up compaitable jobs quickly we need
        // a special table of some kind. We use a bloom filter
        // table. We have an index on (directory, commandline, environment,
        // stdin) and from there we do a scan over our bloom_filters. Any
        // remaining matching jobs can be checked against the the input_files,
        // and input_dirs tables.
        "create table if not exists jobs("
        "  job_id       integer primary key autoincrement,"
        "  directory    text    not null,"
        "  commandline  blob    not null,"
        "  environment  blob    not null,"
        "  stdin        text    not null,"
        "  bloom_filter integer);"  // TODO: Use a larger bloom filter
        "create index if not exists job on jobs(directory, commandline, environment, stdin);"
        // We only record the input hashes, and not all visible files.
        // The input file blobs are not stored on disk. Only their hash
        // is stored
        "create table if not exists input_files("
        "  input_file_id integer primary key autoincrement,"
        "  path          text    not null,"
        "  hash          text    not null,"
        "  job           job_id  not null references jobs(job_id) on delete cascade);"
        "create index if not exists input_file on input_files(path, hash);"
        // We don't record where a wake job writes an output file
        // only where the file is placed within the sandbox. Each
        // seperate sandbox will provide a distinct remapping of
        // these items. The blobs of these hashes will also
        // be stored on disk.
        // TODO(jake): Add mode
        "create table if not exists output_files("
        "  output_file_id integer primary key autoincrement,"
        "  path           text    not null,"
        "  hash           text    not null,"
        "  job            job_id  not null references jobs(job_id) on delete "
        "cascade);"
        "create index if not exists output_file on output_files(path, hash);"
        "create index if not exists find_file on output_files(hash);"
        // We also need to know about directories that have been read
        // in some way. For instance if a file fails to be read in
        // a directory or if a readdir is performed. We only
        // store a hash of a subset of the dirent information for
        // a directory. Namely the name of each entry and its d_type.
        // This hash is crptographic so we do not intend on seeing
        // collisions.
        "create table if not exists input_dirs("
        "  input_dir_id integer primary key autoincrement,"
        "  path         text    not null,"
        "  hash         text    not null,"
        "  job          job_id  not null references jobs(job_id) on delete "
        "cascade);"
        "create index if not exists input_dir on input_dirs(path, hash);";

    std::string db_path = dir + "/cache.db";
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
    if (sqlite3_exec(db, cache_schema.c_str(), nullptr, nullptr, &fail) != SQLITE_OK) {
      log_fatal("error: failed init stmt: %s: %s", fail, sqlite3_errmsg(db));
    }
  }

  void prepare_stmts() {
    std::string sql_add_job =
        "insert into jobs (directory, commandline, environment, stdin, bloom_filter) values (?, ?, "
        "?, ?, ?)";
    std::string sql_add_input_file = "insert into input_files (path, hash, job) values (?, ?, ?)";
    std::string sql_add_input_dir = "insert into input_files (path, hash, job) values (?, ?, ?)";
    std::string sql_add_output_file = "insert into output_files (path, hash, job) values (?, ?, ?)";
    std::string sql_begin_txn = "begin transaction";
    std::string sql_commit_txn = "commit transaction";

    PREPARE(sql_add_job, add_job)
    PREPARE(sql_add_input_file, add_input_file)
    PREPARE(sql_add_input_dir, add_input_dir)
    PREPARE(sql_add_output_file, add_output_file)
    PREPARE(sql_begin_txn, begin_txn_query)
    PREPARE(sql_commit_txn, commit_txn_query)
  }

  void begin_txn() { single_step("Could not begin a transaction", begin_txn_query); }

  void end_txn() { single_step("Could not commit a transaction", commit_txn_query); }

  int64_t insert_job(const std::string &cwd, const std::string &cmd, const std::string &env,
                     const std::string &stdin_str, BloomFilter bloom) {
    const char *why = "Could not insert job";
    int64_t bloom_integer = *reinterpret_cast<const int64_t *>(bloom.data());
    bind_string(why, add_job, 1, cwd.c_str(), cwd.size());
    bind_string(why, add_job, 2, cmd.c_str(), cmd.size());
    bind_string(why, add_job, 3, env.c_str(), env.size());
    bind_string(why, add_job, 4, stdin_str.c_str(), stdin_str.size());
    bind_integer(why, add_job, 5, bloom_integer);
    single_step(why, add_job);
    int64_t job_id = sqlite3_last_insert_rowid(db);
    finish_stmt(why, add_job);
    return job_id;
  }

  void insert_input_file(const std::string &path, const std::string &hash, int64_t job_id) {
    const char *why = "Could not insert input file";
    bind_string(why, add_input_file, 1, path.c_str(), path.size());
    bind_string(why, add_input_file, 2, hash.c_str(), hash.size());
    bind_integer(why, add_input_file, 3, job_id);
    single_step(why, add_input_file);
    finish_stmt(why, add_input_file);
  }

  void insert_input_dir(const std::string &path, const std::string &hash, int64_t job_id) {
    const char *why = "Could not insert input directory";
    bind_string(why, add_input_dir, 1, path.c_str(), path.size());
    bind_string(why, add_input_dir, 2, hash.c_str(), hash.size());
    bind_integer(why, add_input_dir, 3, job_id);
    single_step(why, add_input_dir);
    finish_stmt(why, add_input_dir);
  }

  void insert_output_file(const std::string &path, const std::string &hash, int64_t job_id) {
    const char *why = "Could not insert output file";
    bind_string(why, add_output_file, 1, path.c_str(), path.size());
    bind_string(why, add_output_file, 2, hash.c_str(), hash.size());
    bind_integer(why, add_output_file, 3, job_id);
    single_step(why, add_output_file);
    finish_stmt(why, add_output_file);
  }

 public:
  ~Cache() {
    FINALIZE(add_job)
    FINALIZE(add_input_file)
    FINALIZE(add_input_dir)
    FINALIZE(add_output_file)
    FINALIZE(begin_txn_query)
    FINALIZE(commit_txn_query)

    if (sqlite3_close(db) != SQLITE_OK) {
      std::cerr << "Could not close wake.db: " << sqlite3_errmsg(db) << std::endl;
      return;
    }
  }

  Cache() = delete;
  Cache(const Cache &) = delete;
  Cache(std::string _dir) : dir(_dir), rng(get_rng_seed()) {
    mkdir_no_fail(_dir.c_str());
    init_sql();
    prepare_stmts();
  }

  void add(const JAST &job_result_json) {
    // Create a unique name for the job dir (will rename later to correct name)
    std::string tmp_job_dir = dir + "/tmp_" + rng.unique_name();
    mkdir_no_fail(tmp_job_dir.c_str());

    // Parse the json
    AddJobRequest add_request(job_result_json);

    // Copy the output files into the temp dir
    for (const auto &output_file : add_request.outputs) {
      std::string blob_path = tmp_job_dir + "/" + output_file.hash;
      copy_or_reflink(output_file.source.c_str(), blob_path.c_str());
    }

    // Start a transaction so that a job is never without its files.
    begin_txn();
    uint64_t job_id = insert_job(add_request.cwd, add_request.command_line, add_request.envrionment,
                                 add_request.stdin_str, add_request.bloom);

    // Input Files
    for (const auto &input_file : add_request.inputs) {
      insert_input_file(input_file.path, input_file.hash, job_id);
    }

    // Input Dirs
    for (const auto &input_dir : add_request.directories) {
      insert_input_dir(input_dir.path, input_dir.hash, job_id);
    }

    // Output Files
    for (const auto &output_file : add_request.outputs) {
      insert_input_dir(output_file.path, output_file.hash, job_id);
    }

    // Commit the database without having moved the job directory.
    // On *read* you have to be aware tha the database can be in
    // this kind of faulty state where the database is populated but
    // file system is *not* populated. In such a case we interpret that
    // as if it wasn't in the database and so it doesn't get used and
    // will eventully be deleted.
    end_txn();

    // TODO: Clean up temp directories
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
      cache.add(job_result);
    }
  }
}
