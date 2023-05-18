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
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#include <util/execpath.h>
#include <util/mkdir_parents.h>
#include <util/poll.h>
#include <util/term.h>
#include <wcl/defer.h>
#include <wcl/filepath.h>
#include <wcl/trie.h>
#include <wcl/unique_fd.h>
#include <wcl/xoshiro_256.h>

#include <algorithm>
#include <future>
#include <iostream>
#include <map>
#include <random>
#include <thread>
#include <unordered_map>

#include "db_helpers.h"
#include "eviction_command.h"
#include "eviction_policy.h"
#include "job_cache.h"
#include "job_cache_impl_common.h"
#include "logging.h"
#include "message_parser.h"

namespace {

using namespace job_cache;

// Helper that only returns successful file opens.
static int open_fd(const char *str, int flags, mode_t mode) {
  int fd = open(str, flags, mode);
  if (fd == -1) {
    log_fatal("open(%s): %s", str, strerror(errno));
  }
  return fd;
}

static void lock_file(const char *lock_path) {
  // We throw out the lock_fd because we don't want to release
  // the lock until we exit the process
  int lock_fd = open_fd(lock_path, O_CREAT | O_RDWR, 0644);
  struct flock fl;
  memset(&fl, 0, sizeof(fl));
  fl.l_type = F_WRLCK;
  fl.l_whence = SEEK_SET;
  fl.l_start = 0;
  fl.l_len = 0;

  // We captured the lock, we are the daemon
  if (fcntl(lock_fd, F_SETLK, &fl) == 0) {
    return;
  }

  // Some other process has the lock, they are the daemon
  if (errno == EAGAIN || errno == EACCES) {
    int pid = getpid();
    log_exit("fcntl(F_SETLK, %s): %s -- assuming another daemon exists, closing pid=%d", lock_path,
             strerror(errno), pid);
    return;
  }

  // Something went wrong trying to grab the lock
  log_fatal("fcntl(F_SETLK, %s): %s", lock_path, strerror(errno));
}

static void create_file(const char *tmp_path, const char *final_path, const char *data,
                        size_t size) {
  // This method of creating a file is slightly more hygenic because it means
  // that the file does not exist at the target location until it has been fully created.

  {
    auto create_fd = wcl::unique_fd::open(tmp_path, O_CREAT | O_RDWR, 0644);
    if (!create_fd) {
      log_fatal("open(%s): %s", tmp_path, strerror(create_fd.error()));
    }

    if (write(create_fd->get(), data, size) == -1) {
      log_fatal("write(%s): %s", tmp_path, strerror(errno));
    }
  }

  if (rename(tmp_path, final_path) == -1) {
    log_fatal("rename(%s, %s): %s", tmp_path, final_path, strerror(errno));
  }
}

// Create a *blocking* domain socket. We intend on using
// epoll later so we don't need anything to be non-blocking.
static int open_abstract_domain_socket(const std::string &key) {
  // Now we need to:
  //   1) Create a socket
  //   3) Bind the socket to "abstract" socket address
  //   4) Mark this socket as a "listen" socket
  //   5) Later some other code can accept in a loop (with epoll lets say)
  int socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (socket_fd == -1) {
    log_fatal("socket(AF_UNIX, SOCK_STREAM, 0): %s", strerror(errno));
  }

  // By adding a null character to the start of this socket address we're creating
  // an "abstract" socket.
  sockaddr_un addr = {0};
  addr.sun_family = AF_UNIX;
  addr.sun_path[0] = '\0';
  memcpy(addr.sun_path + 1, key.data(), key.size());

  // The length needs to cover the whole path field so we add 1 to size of the key.
  if (bind(socket_fd, reinterpret_cast<const sockaddr *>(&addr), key.size() + 1)) {
    log_fatal("bind(key = %s): %s", key.c_str(), strerror(errno));
  }
  log_info("Successfully bound abstract socket = %s", key.c_str());

  // Now we just need to set this socket to listen and we're good!
  // TODO: Decide what the backlog should actually be
  if (listen(socket_fd, 256) == -1) {
    log_fatal("listen(%s): %s", key, strerror(errno));
  }
  log_info("Successfully set abstract socket %s to listen", key.c_str());

  return socket_fd;
}

static std::pair<int, std::string> create_cache_socket(const std::string &dir) {
  // Aquire a write lock so we know we're the only cache owner.
  // While this successfully stops multiple daemons from running,
  // it has another issue in that just because the lock is aquired,
  // doesn't mean that the service has started. I don't see a strong
  // way around this however so I think the clients will just have
  // keep retrying the connection. Worse yet the old key may
  // still exist so users will have to keep re-*reading* the key
  // while retrying with expoential backoff.
  std::string lock_path = dir + "/.lock";
  lock_file(lock_path.c_str());

  // Not critical but more hygenic to unlink the old key now
  std::string key_path = dir + "/.key";
  unlink_no_fail(key_path.c_str());

  // Get some random bits to name our domain socket with
  wcl::xoshiro_256 rng(wcl::xoshiro_256::get_rng_seed());
  std::string key = rng.unique_name();
  log_info("key = %s", key.c_str());

  // Create the key file that clients can read the domain
  // socket name from.
  // TODO: Move this step to as late as possible to be more
  // hygenic. The client
  std::string gen_path = dir + "/" + key;
  create_file(gen_path.c_str(), key_path.c_str(), key.data(), key.size());

  // Create the socket to listen on. A
  int socket_fd = open_abstract_domain_socket(key);
  return std::make_pair(socket_fd, std::move(key));
}

// Database classes
class InputFiles {
 private:
  PreparedStatement add_input_file;

 public:
  static constexpr const char *insert_query =
      "insert into input_files (path, hash, job) values (?, ?, ?)";

  InputFiles(std::shared_ptr<job_cache::Database> db) : add_input_file(db, insert_query) {
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

  InputDirs(std::shared_ptr<job_cache::Database> db) : add_input_dir(db, insert_query) {
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

  OutputFiles(std::shared_ptr<job_cache::Database> db) : add_output_file(db, insert_query) {
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

class OutputDirs {
 private:
  PreparedStatement add_output_dir;

 public:
  static constexpr const char *insert_query =
      "insert into output_dirs (path, mode, job) values (?, ?, ?)";

  OutputDirs(std::shared_ptr<job_cache::Database> db) : add_output_dir(db, insert_query) {
    add_output_dir.set_why("Could not insert output dir");
  }

  void insert(const std::string &path, mode_t mode, int64_t job_id) {
    add_output_dir.bind_string(1, path);
    add_output_dir.bind_integer(2, mode);
    add_output_dir.bind_integer(3, job_id);
    add_output_dir.step();
    add_output_dir.reset();
  }
};

class OutputSymlinks {
 private:
  PreparedStatement add_output_symlink;

 public:
  static constexpr const char *insert_query =
      "insert into output_symlinks (path, value, job) values (?, ?, ?)";

  OutputSymlinks(std::shared_ptr<job_cache::Database> db) : add_output_symlink(db, insert_query) {
    add_output_symlink.set_why("Could not insert output file");
  }

  void insert(const std::string &path, const std::string &value, int64_t job_id) {
    add_output_symlink.bind_string(1, path);
    add_output_symlink.bind_string(2, value);
    add_output_symlink.bind_integer(3, job_id);
    add_output_symlink.step();
    add_output_symlink.reset();
  }
};

class JobTable {
 private:
  std::shared_ptr<job_cache::Database> db;
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

  JobTable(std::shared_ptr<job_cache::Database> db)
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
                          const std::string &stderr_str, int status, double runtime, double cputime,
                          int64_t mem, int64_t ibytes, int64_t obytes) {
    add_output_info.bind_integer(1, job_id);
    add_output_info.bind_string(2, stdout_str);
    add_output_info.bind_string(3, stderr_str);
    add_output_info.bind_integer(4, status);
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

class SelectMatchingJobs {
 private:
  PreparedStatement find_jobs;
  PreparedStatement find_files;
  PreparedStatement find_dirs;
  PreparedStatement find_outputs;
  PreparedStatement find_output_dirs;
  PreparedStatement find_output_symlinks;
  PreparedStatement find_job_output_info;

  static wcl::optional<std::vector<std::string>> all_match(PreparedStatement &find, int64_t job_id,
                                                           const FindJobRequest &find_job_request) {
    auto defer_reset = wcl::make_defer([&find]() { find.reset(); });
    find.bind_integer(1, job_id);
    std::vector<std::string> out;
    while (find.step() == SQLITE_ROW) {
      std::string path = find.read_string(1);
      Hash256 hash = Hash256::from_hex(find.read_string(2));
      auto iter = find_job_request.visible.find(path);
      if (iter == find_job_request.visible.end() || hash != iter->second) {
        return {};
      }
      out.emplace_back(std::move(path));
    }
    return {wcl::in_place_t{}, std::move(out)};
  }

  std::vector<CachedOutputFile> read_outputs(int64_t job_id) {
    auto defer_reset = wcl::make_defer([this]() { find_outputs.reset(); });
    find_outputs.bind_integer(1, job_id);
    std::vector<CachedOutputFile> out;
    while (find_outputs.step() == SQLITE_ROW) {
      CachedOutputFile file;
      file.path = find_outputs.read_string(1);
      file.hash = Hash256::from_hex(find_outputs.read_string(2));
      file.mode = find_outputs.read_integer(3);
      out.emplace_back(std::move(file));
    }
    return out;
  }

  std::vector<CachedOutputDir> read_output_dirs(int64_t job_id) {
    auto defer_reset = wcl::make_defer([this]() { find_output_dirs.reset(); });
    find_output_dirs.bind_integer(1, job_id);
    std::vector<CachedOutputDir> out;
    while (find_output_dirs.step() == SQLITE_ROW) {
      CachedOutputDir dir;
      dir.path = find_output_dirs.read_string(1);
      dir.mode = find_output_dirs.read_integer(2);
      out.emplace_back(std::move(dir));
    }
    // Sort them so that parents always proceed their children
    std::sort(out.begin(), out.end(),
              [](const auto &a, const auto &b) { return a.path.size() < b.path.size(); });
    return out;
  }

  std::vector<CachedOutputSymlink> read_output_symlinks(int64_t job_id) {
    auto defer_reset = wcl::make_defer([this]() { find_output_symlinks.reset(); });
    find_output_symlinks.bind_integer(1, job_id);
    std::vector<CachedOutputSymlink> out;
    while (find_output_symlinks.step() == SQLITE_ROW) {
      CachedOutputSymlink sym;
      sym.path = find_output_symlinks.read_string(1);
      sym.value = find_output_symlinks.read_string(2);
      out.emplace_back(std::move(sym));
    }
    return out;
  }

  wcl::optional<JobOutputInfo> read_output_info(int64_t job_id) {
    auto defer_reset = wcl::make_defer([this]() { find_job_output_info.reset(); });
    find_job_output_info.bind_integer(1, job_id);
    if (find_job_output_info.step() != SQLITE_ROW) {
      return {};
    }
    JobOutputInfo out;
    out.stdout_str = find_job_output_info.read_string(0);
    out.stderr_str = find_job_output_info.read_string(1);
    out.status = find_job_output_info.read_integer(2);
    out.runtime = find_job_output_info.read_double(3);
    out.cputime = find_job_output_info.read_double(4);
    out.mem = find_job_output_info.read_integer(5);
    out.ibytes = find_job_output_info.read_integer(6);
    out.obytes = find_job_output_info.read_integer(7);

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

  // Lastly if we find a job we need to read all of its outputs
  static constexpr const char *sql_output_files = "select * from output_files where job = ?";
  static constexpr const char *sql_output_dirs = "select * from output_dirs where job = ?";
  static constexpr const char *sql_output_symlinks = "select * from output_symlinks where job = ?";

  // When a matching job is found we need to read its other output info too
  static constexpr const char *sql_job_output_info =
      "select stdout, stderr, ret, runtime, cputime, mem, ibytes, obytes from job_output_info "
      "where job = ?";

  SelectMatchingJobs(std::shared_ptr<job_cache::Database> db)
      : find_jobs(db, sql_find_jobs),
        find_files(db, sql_find_files),
        find_dirs(db, sql_input_dirs),
        find_outputs(db, sql_output_files),
        find_output_dirs(db, sql_output_dirs),
        find_output_symlinks(db, sql_output_symlinks),
        find_job_output_info(db, sql_job_output_info) {
    find_jobs.set_why("Could not find matching jobs");
    find_files.set_why("Could not find files of the given job");
    find_dirs.set_why("Could not find dirs of the given job");
  }

  // NOTE: It is assumed that this is already running inside of a transaction
  wcl::optional<std::pair<int, MatchingJob>> find(const FindJobRequest &find_job_request) {
    wcl::optional<std::pair<int, MatchingJob>> out;

    // These parts must match exactly
    auto defer_reset = wcl::make_defer([this]() { find_jobs.reset(); });
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
      result.output_files = read_outputs(job_id);
      result.output_dirs = read_output_dirs(job_id);
      result.output_symlinks = read_output_symlinks(job_id);
      result.output_info = std::move(*output_info);
      result.input_files = std::move(*found_input_files);
      result.input_dirs = std::move(*found_input_dirs);

      return wcl::make_some<std::pair<int, MatchingJob>>(job_id, std::move(result));
    }

    return {};
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

}  // namespace

namespace job_cache {

struct CacheDbImpl {
 private:
  std::shared_ptr<job_cache::Database> db;

 public:
  JobTable jobs;
  InputFiles input_files;
  InputDirs input_dirs;
  OutputFiles output_files;
  OutputDirs output_dirs;
  OutputSymlinks output_symlinks;
  Transaction transact;
  SelectMatchingJobs matching_jobs;

  CacheDbImpl(const std::string &_dir)
      : db(std::make_unique<job_cache::Database>(_dir)),
        jobs(db),
        input_files(db),
        input_dirs(db),
        output_files(db),
        output_dirs(db),
        output_symlinks(db),
        transact(db),
        matching_jobs(db) {}
};

AddJobRequest AddJobRequest::from_implicit(const JAST &json) {
  AddJobRequest req;
  req.cwd = json.get("cwd").value;
  req.command_line = json.get("command_line").value;
  req.envrionment = json.get("envrionment").value;
  req.stdin_str = json.get("stdin").value;
  req.stdout_str = json.get("stdout").value;
  req.stderr_str = json.get("stderr").value;
  req.status = std::stoi(json.get("status").value);
  req.runtime = std::stod(json.get("runtime").value);
  req.cputime = std::stod(json.get("cputime").value);
  req.mem = std::stoull(json.get("mem").value);
  req.ibytes = std::stoull(json.get("ibytes").value);
  req.obytes = std::stoull(json.get("obytes").value);

  // Read the input files
  for (const auto &input_file : json.get("input_files").children) {
    InputFile input(input_file.second);
    req.bloom.add_hash(input.hash);
    req.inputs.emplace_back(std::move(input));
  }

  // Read the input dirs
  for (const auto &input_dir : json.get("input_dirs").children) {
    InputDir input(input_dir.second);
    req.bloom.add_hash(input.hash);
    req.directories.emplace_back(std::move(input));
  }

  // TODO: I hate this loop but its the fastest path to a demo.
  //       we need to figure out a path add things to the cache
  //       only after the files have been hashed so we don't
  //       need this loop. Since this job was just run, wake
  //       will eventually hash all these files so the fact
  //       that we have to re-hash them here is a shame.
  // TODO: Use aio_read and do these hashes online and interleaveed
  //       so that the IO can be in parallel despite the hashing being
  //       serial.s. It would also be nice to figure out how to do
  //       the hashing in parallel if we can't avoid it completely.
  // Read the output files which requires kicking off a hash
  for (const auto &output_file : json.get("output_files").children) {
    struct stat buf = {};
    const std::string &src = output_file.second.get("src").value;
    if (lstat(src.c_str(), &buf) < 0) {
      log_fatal("lstat(%s): %s", src.c_str(), strerror(errno));
    }

    // Handle output directory
    if (S_ISDIR(buf.st_mode)) {
      OutputDirectory dir;
      dir.mode = buf.st_mode;
      dir.path = output_file.second.get("path").value;
      req.output_dirs.emplace_back(std::move(dir));
      continue;
    }

    // Handle symlink
    if (S_ISLNK(buf.st_mode)) {
      OutputSymlink sym;
      static thread_local char link[4097];
      int size = readlink(src.c_str(), link, sizeof(link));
      if (size == -1) {
        log_fatal("readlink(%s): %s", src.c_str(), strerror(errno));
      }
      sym.path = output_file.second.get("path").value;
      sym.value = std::string(link, link + size);
      req.output_symlinks.emplace_back(std::move(sym));
      continue;
    }

    // Handle regular files but ignore everything else.
    if (!S_ISREG(buf.st_mode)) continue;
    OutputFile output;
    output.source = output_file.second.get("src").value;
    output.path = output_file.second.get("path").value;
    auto fd = wcl::unique_fd::open(output.source.c_str(), O_RDONLY);
    if (!fd) {
      log_fatal("open(%s): %s", output.source.c_str(), strerror(fd.error()));
    }
    output.hash = do_hash_file(output.source.c_str(), fd->get());
    output.mode = buf.st_mode;
    req.outputs.emplace_back(std::move(output));
  }

  return req;
}

AddJobRequest::AddJobRequest(const JAST &json) {
  cwd = json.get("cwd").value;
  command_line = json.get("command_line").value;
  envrionment = json.get("envrionment").value;
  stdin_str = json.get("stdin").value;
  stdout_str = json.get("stdout").value;
  stderr_str = json.get("stderr").value;
  status = std::stoi(json.get("status").value);
  runtime = std::stod(json.get("runtime").value);
  cputime = std::stod(json.get("cputime").value);
  mem = std::stoull(json.get("mem").value);
  ibytes = std::stoull(json.get("ibytes").value);
  obytes = std::stoull(json.get("obytes").value);

  // Read the input files
  for (const auto &input_file : json.get("input_files").children) {
    InputFile input(input_file.second);
    bloom.add_hash(input.hash);
    inputs.emplace_back(std::move(input));
  }

  // Read the input dirs
  for (const auto &input_dir : json.get("input_dirs").children) {
    InputDir input(input_dir.second);
    bloom.add_hash(input.hash);
    directories.emplace_back(std::move(input));
  }

  for (const auto &output_file : json.get("output_files").children) {
    OutputFile output(output_file.second);
    outputs.emplace_back(std::move(output));
  }

  for (const auto &output_directory : json.get("output_dirs").children) {
    OutputDirectory dir(output_directory.second);
    output_dirs.emplace_back(std::move(dir));
  }

  for (const auto &output_symlink : json.get("output_symlinks").children) {
    OutputSymlink symlink(output_symlink.second);
    output_symlinks.emplace_back(std::move(symlink));
  }
}

JAST AddJobRequest::to_json() const {
  JAST json(JSON_OBJECT);
  json.add("cwd", cwd);
  json.add("command_line", command_line);
  json.add("envrionment", envrionment);
  json.add("stdin", stdin_str);
  json.add("stdout", stdout_str);
  json.add("stderr", stderr_str);
  json.add("status", status);
  json.add("runtime", runtime);
  json.add("cputime", cputime);
  json.add("mem", int64_t(mem));
  json.add("ibytes", int64_t(ibytes));
  json.add("obytes", int64_t(obytes));

  JAST input_files_json(JSON_ARRAY);
  for (const auto &input_file : inputs) {
    input_files_json.add("", input_file.to_json());
  }
  json.add("input_files", std::move(input_files_json));

  JAST input_dirs_json(JSON_ARRAY);
  for (const auto &input_dir : directories) {
    input_dirs_json.add("", input_dir.to_json());
  }
  json.add("input_dirs", std::move(input_dirs_json));

  JAST output_files_json(JSON_ARRAY);
  for (const auto &output_file : outputs) {
    output_files_json.add("", output_file.to_json());
  }
  json.add("output_files", std::move(output_files_json));

  JAST output_directories_json(JSON_ARRAY);
  for (const auto &output_directory : output_dirs) {
    output_directories_json.add("", output_directory.to_json());
  }
  json.add("output_dirs", std::move(output_directories_json));

  JAST output_symlinks_json(JSON_ARRAY);
  for (const auto &output_symlink : output_symlinks) {
    output_symlinks_json.add("", output_symlink.to_json());
  }
  json.add("output_symlinks", std::move(output_symlinks_json));

  return json;
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

  // Now actually perform those hashes
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

JAST FindJobRequest::to_json() const {
  JAST json(JSON_OBJECT);
  json.add("cwd", cwd);
  json.add("command_line", command_line);
  json.add("envrionment", envrionment);
  json.add("stdin", stdin_str);

  JAST input_files(JSON_ARRAY);
  for (const auto &input_file : visible) {
    JAST input_entry(JSON_OBJECT);
    input_entry.add("path", input_file.first);
    input_entry.add("hash", input_file.second.to_hex());
    input_files.add("", std::move(input_entry));
  }
  json.add("input_files", std::move(input_files));

  JAST dir_redirects_json(JSON_OBJECT);
  dir_redirects.for_each(
      [&dir_redirects_json](const std::vector<std::string> &prefix, const std::string &value) {
        std::string path = join('/', prefix.begin(), prefix.end());
        dir_redirects_json.add(path, value);
      });

  json.add("dir_redirects", std::move(dir_redirects_json));

  return json;
}

DaemonCache::DaemonCache(std::string _dir, uint64_t max, uint64_t low)
    : dir(std::move(_dir)),
      rng(wcl::xoshiro_256::get_rng_seed()),
      max_cache_size(max),
      low_cache_size(low) {
  mkdir_no_fail(dir.c_str());
  impl = std::make_unique<CacheDbImpl>(dir);
  std::tie(listen_socket_fd, key) = create_cache_socket(dir);
  launch_evict_loop();
}

int DaemonCache::run() {
  auto cleanup = wcl::make_defer([dir = this->dir]() {
    std::string key = dir + "/.key";
    unlink_no_fail(key.c_str());
  });

  poll.add(listen_socket_fd);
  while (!exit_now) {
    // recieve and process messages

    struct timespec wait_until;
    wait_until.tv_sec = 60 * 10;
    wait_until.tv_nsec = 0;

    auto fds = poll.wait(&wait_until, nullptr);

    if (fds.empty() && message_parsers.empty()) {
      log_info("No initial connection for 10 mins, exiting.");
      return 0;
    }

    for (int fd : fds) {
      if (fd == listen_socket_fd) {
        handle_new_client();
      } else {
        handle_msg(fd);
      }
    }
  }

  return 0;
}

wcl::optional<MatchingJob> DaemonCache::read(const FindJobRequest &find_request) {
  wcl::optional<std::pair<int, MatchingJob>> matching_job;

  // We want to hold the database lock for as little time as possible
  impl->transact.run([this, &find_request, &matching_job]() {
    matching_job = impl->matching_jobs.find(find_request);
  });

  // Return early if there was no match.
  if (!matching_job) return {};

  int job_id = matching_job->first;
  MatchingJob &result = matching_job->second;

  // We need a tmp directory to put these outputs into
  std::string tmp_job_dir = wcl::join_paths(dir, "tmp_outputs_" + rng.unique_name());
  mkdir_no_fail(tmp_job_dir.c_str());

  // We also need to know what directory we're reading out of
  uint8_t group_id = job_id & 0xFF;
  std::string job_dir = wcl::join_paths(dir, wcl::to_hex(&group_id), std::to_string(job_id));

  // We then hard link each file to a new location atomically.
  // If any of these hard links fail then we fail this read
  // and clean up. This allows job cleanup to occur during
  // a read. That would be an unfortunate situation but its
  // very unlikely to occur so its better to commit the
  // transaction early and suffer the consequences of unlinking
  // one of the files just before we need it.
  std::vector<std::tuple<std::string, std::string, mode_t>> to_copy;
  bool success = true;
  for (const auto &output_file : result.output_files) {
    std::string hash_name = output_file.hash.to_hex();
    std::string cur_file = wcl::join_paths(job_dir, hash_name);
    std::string tmp_file = wcl::join_paths(tmp_job_dir, hash_name);
    int ret = link(cur_file.c_str(), tmp_file.c_str());
    if (ret < 0 && errno != EEXIST) {
      success = false;
      break;
    }
    to_copy.emplace_back(std::make_tuple(std::move(tmp_file), output_file.path, output_file.mode));
  }

  auto rewite_path = [&find_request](const std::string &sandbox_destination) {
    std::vector<std::string> path_vec = split_path(sandbox_destination);
    // So the file that the sandbox wrote to `sandbox_destination` currently
    // lives at `tmp_file` and is safe from interference. The sandbox location
    // needs to be redirected to some other output location however.
    auto pair = find_request.dir_redirects.find_max(path_vec.begin(), path_vec.end());
    if (pair.first == nullptr) {
      std::string output_path = wcl::join_paths(".", sandbox_destination);
      return std::make_pair<std::string, std::vector<std::string>>(std::move(output_path),
                                                                   std::move(path_vec));
    }
    const auto &output_dir = *pair.first;
    const auto &rel_path = join('/', pair.second, path_vec.end());
    std::string output_path = wcl::join_paths(output_dir, rel_path);
    std::vector<std::string> output_path_vec = split_path(output_path);
    return std::make_pair<std::string, std::vector<std::string>>(std::move(output_path),
                                                                 std::move(output_path_vec));
  };

  if (success) {
    // First output all the directories (assumed to be sorted by length).
    // This ensures that all directories are already made with the
    // expected mode.
    for (const auto &output_dir : result.output_dirs) {
      // Rewrite the path based on the available rewrites
      auto pair = rewite_path(output_dir.path);

      // First make all the needed directories
      mkdir(pair.first.c_str(), output_dir.mode);
    }

    // Now copy/reflink all output files into their final place
    for (const auto &to_copy : to_copy) {
      const auto &tmp_file = std::get<0>(to_copy);
      const auto &sandbox_destination = std::get<1>(to_copy);
      mode_t mode = std::get<2>(to_copy);

      // Rewrite the path based on the available rewrites
      auto pair = rewite_path(sandbox_destination);

      // First make all the needed directories in case the output
      // directories are missing. The mode of creation is assumed
      // in this case.
      mkdir_all(pair.second.begin(), pair.second.end());

      // Finally copy the file (as efficently as we can) to
      // the destination.
      std::string tmp_dst = pair.first + "." + rng.unique_name();
      copy_or_reflink(tmp_file.c_str(), tmp_dst.c_str(), mode, O_EXCL);
      rename_no_fail(tmp_dst.c_str(), pair.first.c_str());
    }

    // Now create all the symlinks
    for (const auto &output_symlink : result.output_symlinks) {
      // Rewrite the path based on the available rewrites
      auto pair = rewite_path(output_symlink.path);

      // First make all the needed directories in case the output
      // directories are missing. The mode of creation is assumed
      // in this case.
      mkdir_all(pair.second.begin(), pair.second.end());

      // Lastly make the symlink
      std::string tmp_link = pair.first + "." + rng.unique_name();
      symlink_no_fail(output_symlink.value.c_str(), tmp_link.c_str());
      rename_no_fail(tmp_link.c_str(), pair.first.c_str());
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
  // here we redirect the output files.
  auto redirect_path = [&find_request](std::string &path) {
    std::vector<std::string> output_path_vec = split_path(path);
    auto pair = find_request.dir_redirects.find_max(output_path_vec.begin(), output_path_vec.end());
    if (!pair.first) return;
    std::string rel_path = join('/', pair.second, output_path_vec.end());
    path = wcl::join_paths(*pair.first, rel_path);
  };

  for (auto &output_file : result.output_files) {
    redirect_path(output_file.path);
  }

  for (auto &output_dir : result.output_dirs) {
    redirect_path(output_dir.path);
  }

  for (auto &output_symlink : result.output_symlinks) {
    redirect_path(output_symlink.path);
  }

  for (auto &input_file : result.input_files) {
    redirect_path(input_file);
  }

  for (auto &input_dir : result.input_dirs) {
    redirect_path(input_dir);
  }

  EvictionCommand cmd(EvictionCommandType::Read, job_id);

  std::string msg = cmd.serialize();
  msg += '\0';

  if (write(evict_stdin, msg.data(), msg.size()) == -1) {
    log_warning("Failed to send eviction update: %s", strerror(errno));
  }

  // TODO: We should really return a different thing here
  //       that mentions the *output* locations but for
  //       now this is good enough and we can assume
  //       workspace relative paths everywhere.
  return wcl::make_some<MatchingJob>(std::move(result));
}

void DaemonCache::add(const AddJobRequest &add_request) {
  // Create a unique name for the job dir (will rename later to correct name)
  std::string tmp_job_dir = wcl::join_paths(dir, "/tmp_" + rng.unique_name());
  mkdir_no_fail(tmp_job_dir.c_str());

  // Copy the output files into the temp dir
  for (const auto &output_file : add_request.outputs) {
    // TODO(jake): See if this file already exists in another job to
    //             avoid the copy by using a link and also save disk
    //             space.
    std::string blob_path = wcl::join_paths(tmp_job_dir, output_file.hash.to_hex());
    copy_or_reflink(output_file.source.c_str(), blob_path.c_str());
  }

  // Start a transaction so that a job is never without its files.
  int64_t job_id;
  {
    impl->transact.run([this, &add_request, &job_id]() {
      job_id = impl->jobs.insert(add_request.cwd, add_request.command_line, add_request.envrionment,
                                 add_request.stdin_str, add_request.bloom);

      // Add additional info
      impl->jobs.insert_output_info(job_id, add_request.stdout_str, add_request.stderr_str,
                                    add_request.status, add_request.runtime, add_request.cputime,
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
        impl->output_files.insert(output_file.path, output_file.hash, output_file.mode, job_id);
      }

      // Output Dirs
      for (const auto &output_dir : add_request.output_dirs) {
        impl->output_dirs.insert(output_dir.path, output_dir.mode, job_id);
      }

      // Output Symlinks
      for (const auto &output_symlink : add_request.output_symlinks) {
        impl->output_symlinks.insert(output_symlink.path, output_symlink.value, job_id);
      }

      // We commit the database without having moved the job directory.
      // On *read* you have to be aware that the database can be in
      // this kind of faulty state where the database is populated but
      // file system is *not* populated. In such a case we interpret that
      // as if it wasn't in the database and so it doesn't get used and
      // will eventully be deleted.
    });
  }

  // Finally we make sure the group directory exits and then
  // atomically rename the temp job into place which completes
  // the insertion. At that point reads should suceed.
  uint8_t job_group = job_id & 0xFF;
  std::string job_group_dir = wcl::join_paths(dir, wcl::to_hex<uint8_t>(&job_group));
  mkdir_no_fail(job_group_dir.c_str());
  std::string job_dir = wcl::join_paths(job_group_dir, std::to_string(job_id));
  rename_no_fail(tmp_job_dir.c_str(), job_dir.c_str());

  EvictionCommand cmd(EvictionCommandType::Write, job_id);

  std::string msg = cmd.serialize();
  msg += '\0';

  if (write(evict_stdin, msg.data(), msg.size()) == -1) {
    log_warning("Failed to send eviction update: %s", strerror(errno));
  }
}

void DaemonCache::launch_evict_loop() {
  const size_t read_side = 0;
  const size_t write_side = 1;

  int stdinPipe[2];
  int stdoutPipe[2];

  if (pipe(stdinPipe) < 0) {
    log_fatal("Failed to allocate eviction pipe: %s", strerror(errno));
  }

  if (pipe(stdoutPipe) < 0) {
    log_fatal("Failed to allocate eviction pipe: %s", strerror(errno));
  }

  int pid = fork();

  // error forking
  if (pid < 0) {
    log_fatal("Failed to fork eviction process: %s", strerror(errno));
  }

  // child
  if (pid == 0) {
    if (dup2(stdinPipe[read_side], STDIN_FILENO) == -1) {
      log_fatal("Failed to dup2 stdin pipe for eviction process: %s", strerror(errno));
    }

    if (dup2(stdoutPipe[write_side], STDOUT_FILENO) == -1) {
      log_fatal("Failed to dup2 stdin pipe for eviction process: %s", strerror(errno));
    }

    close(stdinPipe[read_side]);
    close(stdinPipe[write_side]);
    close(stdoutPipe[read_side]);
    close(stdoutPipe[write_side]);

    // Finally enter the eviction loop, if it exits cleanly
    // go ahead and exit with its result.
    int result =
        eviction_loop(dir, std::make_unique<LRUEvictionPolicy>(max_cache_size, low_cache_size));
    exit(result);
  }

  // parent
  if (pid > 0) {
    close(stdinPipe[read_side]);
    close(stdoutPipe[write_side]);

    evict_pid = pid;
    evict_stdin = stdinPipe[write_side];
    evict_stdout = stdoutPipe[read_side];
  }
}

void DaemonCache::reap_evict_loop() {
  close(evict_stdin);
  close(evict_stdout);
  waitpid(evict_pid, nullptr, 0);
}

// This has to be here because the destructor code for std::unique_ptr<CacheDbImpl>
// requires that the CacheDbImpl be complete.
DaemonCache::~DaemonCache() { reap_evict_loop(); }

void DaemonCache::handle_new_client() {
  // Accept the new client socket. Because of needing to perform
  // multiple read's per client IO event, we have to make the client
  // socket use non-blocking reads. This allows us to consume multiple
  // read calls without blocking the other clients if one of them waits.
  int accept_fd = accept4(listen_socket_fd, nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);
  if (accept_fd == -1) {
    log_fatal("accept(%s): %s", key.c_str(), strerror(errno));
  }

  poll.add(accept_fd);
  message_parsers.insert({accept_fd, MessageParser(accept_fd)});
  log_info("new client connected: %d", accept_fd);
}

void DaemonCache::handle_msg(int client_fd) {
  // In case multiple read events have been enqueued since the
  // last epoll_wait, we have to perform all the reads that
  // have been enqueued.
  MessageParserState state;
  std::vector<std::string> msgs;

  auto it = message_parsers.find(client_fd);
  if (it == message_parsers.end()) {
    log_fatal("unreachable: message_parsers out of sync with poll. client_fd = %d", client_fd);
  }

  state = it->second.read_messages(msgs);

  for (const auto &msg : msgs) {
    // TODO: parse and dispatch message event
    log_info("msg: %s", msg.c_str());

    JAST json;
    std::stringstream parseErrors;
    if (!JAST::parse(msg, parseErrors, json)) {
      log_fatal("DaemonCache::handle_msg(): failed to parse client request");
    }

    if (json.get("method").value == "cache/read") {
      FindJobRequest req(json.get("params"));
      auto match_opt = read(req);

      JAST res(JSON_OBJECT);
      res.add("method", "cache/read");

      if (match_opt) {
        res.add("params", match_opt->to_json());
      } else {
        res.add("params", JSON_NULLVAL);
      }

      send_json_message(client_fd, res);
    }

    if (json.get("method").value == "cache/add") {
      AddJobRequest req(json.get("params"));
      add(req);
    }
  }

  // If the file was closed, remove from epoll and close it.
  if (state == MessageParserState::StopSuccess) {
    log_info("closing client fd = %d", client_fd);
    poll.remove(client_fd);
    close(client_fd);
    message_parsers.erase(client_fd);
    if (message_parsers.empty()) {
      exit_now = true;
    }
    return;
  }

  // If there's an error, consider which one it is or fail
  if (state == MessageParserState::StopFail) {
    // These sockets are non-blocking because we have to try reading
    // them multiple times. This means that often we'll wind up
    // attempting N+1 reads and one of them will fail like this
    // without closing.
    if (errno == EAGAIN || errno == EWOULDBLOCK) return;
    // Otherwise handle the error
    log_fatal("read(%d), key = %s:", strerror(errno));
  }
}

}  // namespace job_cache
