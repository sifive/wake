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

#include "job-cache.h"

#include "unique_fd.h"

namespace {

using namespace job_cache;

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
  // There's a race here between the fstat and the copy_file_range
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

static mode_t copy_or_reflink(const char *src, const char *dst, mode_t mode = 0644) {
  struct stat buf;
  auto src_fd = UniqueFd::open(src, O_RDONLY);
  // There's a race here between the fstat and the ioctl/copy_file_range.
  // User's of the cache will have to ensure that no one is touching these
  // files while they're being added to the cache for now.
  // I don't see a way to resolve this issue frankly.
  if (fstat(src_fd.get(), &buf) < 0) {
    log_fatal("fstat(%s): %s", src, strerror(errno));
  }
  auto dst_fd = UniqueFd::open(dst, O_WRONLY | O_CREAT, mode);

  static wcl::xoshiro_256 rng(wcl::xoshiro_256::get_rng_seed());
  std::string name = dst;
  name += ".";
  name += rng.unique_name();

  if (ioctl(dst_fd.get(), FICLONE, src_fd.get()) < 0) {
    if (errno != EINVAL && errno != EOPNOTSUPP) {
      log_fatal("ioctl(%s, FICLONE, %d): %s", dst, src, strerror(errno));
    }
    copy(src_fd.get(), dst_fd.get());
  }

  return buf.st_mode;
}

#else

static void copy_or_reflink(const char *src, const char *dst) {
  auto src_fd = UniqueFd::open(src, O_RDONLY);
  auto dst_fd = UniqueFd::open(dst, O_WRONLY | O_CREAT, 0644);

  copy(src_fd.get(), dst_fd.get());
}

#endif

// This is fairly critical to getting strong concurency.
// Specifically adding in exponetial back off plus randomization.
static int wait_handle(void *, int retries) {
  // We don't ever want to wait more than ~4 seconds.
  // If we wait more than ~4 seconds we fail.
  constexpr int start_pow_2 = 6;
  constexpr int end_pow_2 = 22;
  if (retries > end_pow_2 - start_pow_2) return 0;

  useconds_t base_wait = 1 << start_pow_2;
  std::random_device rd;
  // Wait exponetially longer the more times
  // we've had to retry.
  useconds_t wait = base_wait << retries;
  // Randomize so we don't all retry at the same time.
  wait += rd() & (wait - 1);
  // Finally sleep, without waking up exactly when we're done.
  // but we should never sleep longer than twice as long as we
  // had to this way.
  usleep(wait);

  // Tell sqlite to retry
  return 1;
}

class Database {
 private:
  sqlite3 *db = nullptr;

 public:
  Database(const Database &) = delete;
  Database(Database &&) = delete;
  Database() = delete;
  ~Database() {
    static int dystroy_counter = 0;
    assert(++dystroy_counter == 1);
    if (sqlite3_close(db) != SQLITE_OK) {
      log_fatal("Could not close database: %s", sqlite3_errmsg(db));
    }
  }
  Database(const std::string &cache_dir) {
    static int construct_counter = 0;
    assert(++construct_counter == 1);
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

    std::string db_path = wcl::join_paths(cache_dir, "/cache.db");
    if (sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                        nullptr) != SQLITE_OK) {
      log_fatal("error: %s", sqlite3_errmsg(db));
    }

    // std::cerr << "Got here" << std::endl;

    // If we happen to open the db as read only we need to fail.
    // TODO: Why would this happen?
    // if (sqlite3_db_readonly(db, 0)) {
    // log_fatal("error: cache.db is read-only");
    //}

    if (sqlite3_busy_handler(db, wait_handle, nullptr)) {
      log_fatal("error: failed to set sqlite3_busy_handler: %s", sqlite3_errmsg(db));
    }

    char *fail = nullptr;
    // uint64_t wait = 128;
    // std::random_device rd;
    // do {
    int ret = sqlite3_exec(db, cache_schema, nullptr, nullptr, &fail);
    /*if (ret == SQLITE_BUSY) {
      // Sleep with exponetial backoff and add in a random offset
      // to avoid syncing
      usleep(wait + (rd() & (wait - 1)));
      wait <<= 1;
    }*/
    // std::cerr << "busy: " << fail << std::endl;
    //} while (ret == SQLITE_BUSY);
    // std::cerr << "finished!" << std::endl;
    if (ret == SQLITE_BUSY) {
      log_info(
          "warning: It appears another process is holding the database open, check `ps` for "
          "suspended job-cache instances");
    }
    if (ret != SQLITE_OK) {
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

  void bind_double(int64_t index, double value) {
    int ret = sqlite3_bind_double(query_stmt, index, value);
    if (ret != SQLITE_OK) {
      log_fatal("%s: sqlite3_bind_double(%d, %d): %s", why.c_str(), index, value,
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

  double read_double(int64_t index) { return sqlite3_column_double(query_stmt, index); }

  std::string read_string(int64_t index) {
    const char *str = reinterpret_cast<const char *>(sqlite3_column_text(query_stmt, index));
    size_t size = sqlite3_column_bytes(query_stmt, index);
    return std::string(str, size);
  }

  void reset() {
    int ret;
    // do {
    ret = sqlite3_reset(query_stmt);
    if (ret == SQLITE_LOCKED) {
      log_fatal("error: it seems we're hitting this return code somehow");
    }
    //} while (ret == SQLITE_BUSY);
    if (ret != SQLITE_OK) {
      log_fatal("error: %s; sqlite3_reset: %s", why.c_str(), sqlite3_errmsg(db->get()));
    }

    if (sqlite3_clear_bindings(query_stmt) != SQLITE_OK) {
      log_fatal("error: %s; sqlite3_clear_bindings: %s", why.c_str(), sqlite3_errmsg(db->get()));
    }
  }

  int step() {
    int ret;
    // do {
    ret = sqlite3_step(query_stmt);
    //} while (ret == SQLITE_BUSY);
    if (ret != SQLITE_DONE && ret != SQLITE_ROW) {
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
      "insert into output_files (path, hash, mode, job) values (?, ?, ?, ?)";

  OutputFiles(std::shared_ptr<Database> db) : add_output_file(db, insert_query) {
    add_output_file.set_why("Could not insert output file");
  }

  void insert(const std::string &path, Hash256 hash, mode_t mode, int64_t job_id) {
    add_output_file.bind_string(1, path);
    add_output_file.bind_string(2, hash.to_hex());
    add_output_file.bind_integer(3, mode);
    add_output_file.bind_integer(4, job_id);
    add_output_file.step();
    add_output_file.reset();
  }
};

class JobTable {
 private:
  std::shared_ptr<Database> db;
  PreparedStatement add_job;
  PreparedStatement add_output_info;

 public:
  static constexpr const char *insert_query =
      "insert into jobs (directory, commandline, environment, stdin, bloom_filter)"
      "values (?, ?, ?, ?, ?)";

  static constexpr const char *add_output_info_query =
      "insert into job_output_info"
      "(job, stdout, stderr, ret, runtime, cputime, mem, ibytes, obytes)"
      "values (?, ?, ?, ?, ?, ?, ?, ?, ?)";

  JobTable(std::shared_ptr<Database> db)
      : db(db), add_job(db, insert_query), add_output_info(db, add_output_info_query) {
    add_job.set_why("Could not insert job");
    add_output_info.set_why("Could not add output info");
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

  void insert_output_info(int64_t job_id, const std::string &stdout_str,
                          const std::string &stderr_str, int ret_code, double runtime,
                          double cputime, int64_t mem, int64_t ibytes, int64_t obytes) {
    add_output_info.bind_integer(1, job_id);
    add_output_info.bind_string(2, stdout_str);
    add_output_info.bind_string(3, stderr_str);
    add_output_info.bind_integer(4, ret_code);
    add_output_info.bind_double(5, runtime);
    add_output_info.bind_double(6, cputime);
    add_output_info.bind_integer(7, mem);
    add_output_info.bind_integer(8, ibytes);
    add_output_info.bind_integer(9, obytes);
    add_output_info.step();
    add_output_info.reset();
  }
};

// Returns the end of the parent directory in the path.
static wcl::optional<std::pair<std::string, std::string>> parent_and_base(const std::string &str) {
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

struct RequestedJobFile {
  std::string src;
  std::string path;
};

struct JobRequestResult {
  std::vector<RequestedJobFile> requested_files;
};

class Transaction {
 private:
  PreparedStatement begin_txn_query;
  PreparedStatement commit_txn_query;

 public:
  static constexpr const char *sql_begin_txn = "begin immediate transaction";
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
  PreparedStatement find_job_output_info;

  static wcl::optional<std::vector<std::string>> all_match(PreparedStatement &find, int64_t job_id,
                                                           const FindJobRequest &find_job_request) {
    find.reset();
    find.bind_integer(1, job_id);
    std::vector<std::string> out;
    while (find.step() == SQLITE_ROW) {
      std::string path = find.read_string(1);
      Hash256 hash = Hash256::from_hex(find.read_string(2));
      auto iter = find_job_request.visible.find(path);
      if (iter == find_job_request.visible.end() || hash != iter->second) {
        find.reset();  // TODO: This should use wcl::defer
        return {};
      }
      out.emplace_back(std::move(path));
    }
    return {wcl::in_place_t{}, std::move(out)};
  }

  std::vector<CachedOutputFile> read_outputs(int64_t job_id) {
    find_outputs.bind_integer(1, job_id);
    std::vector<CachedOutputFile> out;
    while (find_outputs.step() == SQLITE_ROW) {
      CachedOutputFile file;
      file.path = find_outputs.read_string(1);
      file.hash = Hash256::from_hex(find_outputs.read_string(2));
      file.mode = find_outputs.read_integer(3);
      out.emplace_back(std::move(file));
    }
    find_outputs.reset();  // TODO: This should use wcl::defer
    return out;
  }

  wcl::optional<JobOutputInfo> read_output_info(int64_t job_id) {
    find_job_output_info.bind_integer(1, job_id);
    if (find_job_output_info.step() != SQLITE_ROW) {
      find_job_output_info.reset();  // TODO: This should use wcl::defer
      return {};
    }
    JobOutputInfo out;
    out.stdout_str = find_job_output_info.read_string(0);
    out.stderr_str = find_job_output_info.read_string(1);
    out.ret_code = find_job_output_info.read_integer(2);
    out.runtime = find_job_output_info.read_double(3);
    out.cputime = find_job_output_info.read_double(4);
    out.mem = find_job_output_info.read_integer(5);
    out.ibytes = find_job_output_info.read_integer(6);
    out.obytes = find_job_output_info.read_integer(7);

    find_job_output_info.reset();
    return {wcl::in_place_t{}, std::move(out)};
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

  // When a matching job is found we need to read its other output info too
  static constexpr const char *sql_job_output_info =
      "select stdout, stderr, ret, runtime, cputime, mem, ibytes, obytes from job_output_info "
      "where job = ?";

  SelectMatchingJobs(std::shared_ptr<Database> db)
      : find_jobs(db, sql_find_jobs),
        find_files(db, sql_find_files),
        find_dirs(db, sql_input_dirs),
        find_outputs(db, sql_output_files),
        find_job_output_info(db, sql_job_output_info) {
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

      // We need to find the extra output info as well but this should
      // always work if the database added things correctly.
      auto output_info = read_output_info(job_id);
      if (!output_info) continue;

      auto found_input_files = all_match(find_files, job_id, find_job_request);
      if (!found_input_files) continue;
      auto found_input_dirs = all_match(find_dirs, job_id, find_job_request);
      if (!found_input_dirs) continue;

      // Ok this is the job, it matches *exactly* so we should
      // expect running it to produce exaxtly the same result.
      MatchingJob result;
      result.job_id = job_id;
      result.output_files = read_outputs(job_id);
      result.output_info = std::move(*output_info);
      result.input_files = std::move(*found_input_files);
      result.input_dirs = std::move(*found_input_dirs);

      out = {wcl::in_place_t{}, std::move(result)};
      break;
    }

    // Reset find jobs for the next such transaction
    find_jobs.reset();  // TODO: This should use wcl::defer

    // Hopefully we found something
    return out;
  }
};

static Hash256 do_hash_file(const char *file, int fd) {
  blake2b_state S;
  uint8_t hash[32];
  static thread_local uint8_t buffer[8192];
  ssize_t got;

  blake2b_init(&S, sizeof(hash));
  while ((got = read(fd, &buffer[0], sizeof(buffer))) > 0) blake2b_update(&S, &buffer[0], got);
  blake2b_final(&S, &hash[0], sizeof(hash));

  if (got < 0) {
    log_fatal("job-cache hash read(%s): %s", file, strerror(errno));
  }

  return Hash256::from_hash(&hash);
}

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

// TODO: Do all file movement in parallel

}  // namespace

namespace job_cache {

struct CacheDbImpl {
 private:
  std::shared_ptr<Database> db;

 public:
  JobTable jobs;
  InputFiles input_files;
  InputDirs input_dirs;
  OutputFiles output_files;
  Transaction transact;
  SelectMatchingJobs matching_jobs;

  CacheDbImpl(const std::string &_dir)
      : db(std::make_unique<Database>(_dir)),
        jobs(db),
        input_files(db),
        input_dirs(db),
        output_files(db),
        transact(db),
        matching_jobs(db) {}
};

AddJobRequest::AddJobRequest(const JAST &job_result_json) {
  cwd = job_result_json.get("cwd").value;
  command_line = job_result_json.get("command_line").value;
  envrionment = job_result_json.get("envrionment").value;
  stdin_str = job_result_json.get("stdin").value;
  stdout_str = job_result_json.get("stdout").value;
  stderr_str = job_result_json.get("stderr").value;
  ret_code = std::stoi(job_result_json.get("status").value);
  runtime = std::stod(job_result_json.get("runtime").value);
  cputime = std::stod(job_result_json.get("cputime").value);
  mem = std::stoull(job_result_json.get("mem").value);
  ibytes = std::stoull(job_result_json.get("ibytes").value);
  obytes = std::stoull(job_result_json.get("obytes").value);

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

  // TODO: I hate this loop but its the fastest path to a demo.
  //       we need to figure out a path add things to the cache
  //       only after the files have been hashed so we don't
  //       need this loop.
  std::vector<std::future<OutputFile>> future_outputs;

  // Read the output files which requires kicking off a hash
  for (const auto &output_file : job_result_json.get("output_files").children) {
    struct stat buf;
    const std::string &src = output_file.second.get("src").value;
    if (stat(src.c_str(), &buf) < 0) {
      log_fatal("stat(%s): %s", src.c_str(), strerror(errno));
    }
    if (buf.st_mode & S_IFDIR) continue;
    future_outputs.emplace_back(std::async([&]() {
      OutputFile output;
      output.source = output_file.second.get("src").value;
      output.path = output_file.second.get("path").value;
      auto fd = UniqueFd::open(output.source.c_str(), O_RDONLY | O_NOFOLLOW);
      output.hash = do_hash_file(output.source.c_str(), fd.get());
      return output;
    }));
  }

  // Now wait on all the futures
  for (auto &fut : future_outputs) {
    outputs.emplace_back(fut.get());
  }
}

FindJobRequest::FindJobRequest(const JAST &find_job_json) {
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

Cache::Cache(std::string _dir) : dir(std::move(_dir)), rng(wcl::xoshiro_256::get_rng_seed()) {
  mkdir_no_fail(dir.c_str());
  impl = std::make_unique<CacheDbImpl>(dir);
}

wcl::optional<MatchingJob> Cache::read(const FindJobRequest &find_request) {
  wcl::optional<MatchingJob> result;

  // We want to hold the database lock for as little time as possible
  impl->transact.run(
      [this, &find_request, &result]() { result = impl->matching_jobs.find(find_request); });

  // Return early if there was no match.
  if (!result) return {};

  // We need a tmp directory to put these outputs into
  std::string tmp_job_dir = wcl::join_paths(dir, "tmp_outputs_" + rng.unique_name());
  mkdir_no_fail(tmp_job_dir.c_str());

  // We also need to know what directory we're reading out of
  uint8_t group_id = result->job_id & 0xFF;
  std::string job_dir =
      wcl::join_paths(dir, wcl::to_hex(&group_id), std::to_string(result->job_id));

  // We then hard link each file to a new location atomically.
  // If any of these hard links fail then we fail this read
  // and clean up. This allows job cleanup to occur during
  // a read. That would be an unfortunate situation but its
  // very unlikely to occur so its better to commit the
  // transaction early and suffer the consequences of unlinking
  // one of the files just before we need it.
  std::vector<std::tuple<std::string, std::string, mode_t>> to_copy;
  bool success = true;
  for (const auto &output_file : result->output_files) {
    std::string hash_name = output_file.hash.to_hex();
    std::string cur_file = wcl::join_paths(job_dir, hash_name);
    std::string tmp_file = wcl::join_paths(tmp_job_dir, hash_name);
    int ret = link(cur_file.c_str(), tmp_file.c_str());
    if (ret < 0) {
      success = false;
      break;
    }
    to_copy.emplace_back(std::make_tuple(std::move(tmp_file), output_file.path, output_file.mode));
  }

  if (success) {
    // Now copy/reflink all files into their final place
    for (const auto &to_copy : to_copy) {
      const auto &tmp_file = std::get<0>(to_copy);
      const auto &sandbox_destination = std::get<1>(to_copy);
      mode_t mode = std::get<2>(to_copy);
      std::vector<std::string> path_vec = split_path(sandbox_destination);

      // So the file that the sandbox wrote to `sandbox_destination` currently
      // lives at `tmp_file` and is safe from interference. The sandbox location
      // needs to be redirected to some other output location however.
      auto pair = find_request.dir_redirects.find_max(path_vec.begin(), path_vec.end());

      // If there is no redirect what so ever, just copy and assume the sandbox
      // had an accurate picture of the current system.
      // TODO: Make these copies async
      if (pair.first == nullptr) {
        std::string output_path = wcl::join_paths(".", sandbox_destination);
        std::vector<std::string> output_path_vec = split_path(output_path);
        mkdir_all(path_vec.begin(), path_vec.end());
        copy_or_reflink(tmp_file.c_str(), output_path.c_str(), mode);
      } else {
        const auto &output_dir = *pair.first;
        const auto &rel_path = join('/', pair.second, path_vec.end());
        std::string output_path = wcl::join_paths(output_dir, rel_path);
        std::vector<std::string> output_path_vec = split_path(output_path);
        mkdir_all(output_path_vec.begin(), output_path_vec.end());
        copy_or_reflink(tmp_file.c_str(), output_path.c_str(), mode);
      }
    }
  }

  // Now clean up those files in the tempdir
  for (const auto &to_copy : to_copy) {
    unlink_no_fail(std::get<0>(to_copy).c_str());
  }

  // Lastly clean up the tmp dir itself
  rmdir_no_fail(tmp_job_dir.c_str());

  // If we didn't link all the files over we need to return a failure.
  if (!success) return {};

  // The MatchingJob is currently using sandbox paths.
  // We need to redirect those sandbox paths to non-sandbox paths
  for (auto &output_file : result->output_files) {
    std::vector<std::string> output_path_vec = split_path(output_file.path);
    auto pair = find_request.dir_redirects.find_max(output_path_vec.begin(), output_path_vec.end());
    if (!pair.first) continue;
    std::string rel_path = join('/', pair.second, output_path_vec.end());
    output_file.path = wcl::join_paths(*pair.first, rel_path);
  }

  for (auto &input_file : result->input_files) {
    std::vector<std::string> path_vec = split_path(input_file);
    auto pair = find_request.dir_redirects.find_max(path_vec.begin(), path_vec.end());
    if (!pair.first) continue;
    std::string rel_path = join('/', pair.second, path_vec.end());
    input_file = wcl::join_paths(*pair.first, rel_path);
  }

  for (auto &input_dir : result->input_files) {
    std::vector<std::string> path_vec = split_path(input_dir);
    auto pair = find_request.dir_redirects.find_max(path_vec.begin(), path_vec.end());
    if (!pair.first) continue;
    std::string rel_path = join('/', pair.second, path_vec.end());
    input_dir = wcl::join_paths(*pair.first, rel_path);
  }

  // TODO: We should really return a different thing here
  //       that mentions the *output* locations but for
  //       now this is good enough and we can assume
  //       workspace relative paths everywhere.
  return result;
}

void Cache::add(const AddJobRequest &add_request) {
  // Create a unique name for the job dir (will rename later to correct name)
  std::string tmp_job_dir = wcl::join_paths(dir, "/tmp_" + rng.unique_name());
  mkdir_no_fail(tmp_job_dir.c_str());

  // Copy the output files into the temp dir
  // TOOD: Make this async
  std::unordered_map<void *, mode_t> modes;
  for (const auto &output_file : add_request.outputs) {
    // TODO(jake): See if this file already exists somewhere
    std::string blob_path = wcl::join_paths(tmp_job_dir, output_file.hash.to_hex());
    mode_t src_mode = copy_or_reflink(output_file.source.c_str(), blob_path.c_str());
    modes[(void *)&output_file] = src_mode;
  }

  // Start a transaction so that a job is never without its files.
  int64_t job_id;
  {
    // auto lock = lock_file(wcl::join_paths(dir, "lock"), LOCK_EX);
    // TOOD: Right now we open the database here, we should just leave the connection to the
    // database open in the Cache object instead.
    // CacheDbImpl impl(dir);
    impl->transact.run([this, &modes, &add_request, &job_id]() {
      job_id = impl->jobs.insert(add_request.cwd, add_request.command_line, add_request.envrionment,
                                 add_request.stdin_str, add_request.bloom);

      // Add additional info
      impl->jobs.insert_output_info(job_id, add_request.stdout_str, add_request.stderr_str,
                                    add_request.ret_code, add_request.runtime, add_request.cputime,
                                    add_request.mem, add_request.ibytes, add_request.obytes);

      // Input Files
      for (const auto &input_file : add_request.inputs) {
        impl->input_files.insert(input_file.path, input_file.hash, job_id);
      }

      // Input Dirs
      for (const auto &input_dir : add_request.directories) {
        impl->input_dirs.insert(input_dir.path, input_dir.hash, job_id);
      }

      // Output Files
      for (const auto &output_file : add_request.outputs) {
        mode_t mode = modes[(void *)&output_file];
        if (!mode) mode = 0644;
        impl->output_files.insert(output_file.path, output_file.hash, mode, job_id);
      }

      // We commit the database without having moved the job directory.
      // On *read* you have to be aware tha the database can be in
      // this kind of faulty state where the database is populated but
      // file system is *not* populated. In such a case we interpret that
      // as if it wasn't in the database and so it doesn't get used and
      // will eventully be deleted.
    });
  }
  // TODO: release the lock here

  // Finally we make sure the group directory exits and then
  // atomically rename the temp job into place which completes
  // the insertion. At that point reads should suceed.
  uint8_t job_group = job_id & 0xFF;
  std::string job_group_dir = wcl::join_paths(dir, wcl::to_hex<uint8_t>(&job_group));
  mkdir_no_fail(job_group_dir.c_str());
  std::string job_dir = wcl::join_paths(job_group_dir, std::to_string(job_id));
  rename_no_fail(tmp_job_dir.c_str(), job_dir.c_str());
}

// This has to be here because the destructor code for std::unique_ptr<CacheDbImpl>
// requires that the CacheDbImpl be complete.
Cache::~Cache() {}

}  // namespace job_cache
