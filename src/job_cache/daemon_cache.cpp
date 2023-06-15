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

#include "daemon_cache.h"

#include <sqlite3.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <wcl/defer.h>
#include <wcl/filepath.h>
#include <wcl/optional.h>
#include <wcl/tracing.h>
#include <wcl/unique_fd.h>
#include <wcl/xoshiro_256.h>

#include "db_helpers.h"
#include "eviction_command.h"
#include "eviction_policy.h"
#include "job_cache_impl_common.h"
#include "message_parser.h"
#include "types.h"

namespace {

using namespace job_cache;

// Helper that only returns successful file opens.
static int open_fd(const char *str, int flags, mode_t mode) {
  int fd = open(str, flags, mode);
  if (fd == -1) {
    wcl::log::error("open(%s): %s", str, strerror(errno)).urgent()();
    exit(1);
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
    wcl::log::info("fcntl(F_SETLK, %s): %s -- assuming another daemon exists, closing", lock_path,
                   strerror(errno))();
    exit(0);
  }

  // Something went wrong trying to grab the lock
  wcl::log::error("fcntl(F_SETLK, %s): %s", lock_path, strerror(errno)).urgent()();
  exit(1);
}

static void create_file(const char *tmp_path, const char *final_path, const char *data,
                        size_t size) {
  // This method of creating a file is slightly more hygenic because it means
  // that the file does not exist at the target location until it has been fully created.

  {
    auto create_fd = wcl::unique_fd::open(tmp_path, O_CREAT | O_RDWR, 0644);
    if (!create_fd) {
      wcl::log::error("open(%s): %s", tmp_path, strerror(create_fd.error())).urgent()();
      exit(1);
    }

    if (write(create_fd->get(), data, size) == -1) {
      wcl::log::error("write(%s): %s", tmp_path, strerror(errno)).urgent()();
      exit(1);
    }
  }

  if (rename(tmp_path, final_path) == -1) {
    wcl::log::error("rename(%s, %s): %s", tmp_path, final_path, strerror(errno)).urgent()();
    exit(1);
  }
}

// Create a *blocking* domain socket
static int open_abstract_domain_socket(const std::string &key) {
  // Now we need to:
  //   1) Create a socket
  //   3) Bind the socket to "abstract" socket address
  //   4) Mark this socket as a "listen" socket
  //   5) Later some other code can accept in a loop (with epoll lets say)
  int socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (socket_fd == -1) {
    wcl::log::error("socket(AF_UNIX, SOCK_STREAM, 0): %s", strerror(errno)).urgent()();
    exit(1);
  }

  // By adding a null character to the start of this socket address we're creating
  // an "abstract" socket.
  sockaddr_un addr = {0};
  addr.sun_family = AF_UNIX;
  addr.sun_path[0] = '\0';
  memcpy(addr.sun_path + 1, key.data(), key.size());

  // The length needs to cover the whole path field so we add 1 to size of the key.
  if (bind(socket_fd, reinterpret_cast<const sockaddr *>(&addr), key.size() + 1)) {
    wcl::log::error("bind(key = %s): %s", key.c_str(), strerror(errno)).urgent()();
    exit(1);
  }
  wcl::log::info("Successfully bound abstract socket = %s", key.c_str())();

  // Now we just need to set this socket to listen and we're good!
  // TODO: Decide what the backlog should actually be
  if (listen(socket_fd, 256) == -1) {
    wcl::log::error("listen(%s): %s", key.c_str(), strerror(errno)).urgent()();
    exit(1);
  }
  wcl::log::info("Successfully set abstract socket %s to listen", key.c_str())();

  return socket_fd;
}

int create_cache_socket(const std::string &dir, const std::string &key) {
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

  // Create the key file that clients can read the domain
  // socket name from.
  wcl::log::info("key = %s", key.c_str())();
  std::string gen_path = dir + "/" + key;
  create_file(gen_path.c_str(), key_path.c_str(), key.data(), key.size());

  // Create the socket to listen on. A
  int socket_fd = open_abstract_domain_socket(key);
  return socket_fd;
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
  PreparedStatement remove_job;

 public:
  static constexpr const char *insert_query =
      "insert into jobs (directory, commandline, environment, stdin, bloom_filter)"
      "values (?, ?, ?, ?, ?)";

  static constexpr const char *add_output_info_query =
      "insert into job_output_info"
      "(job, stdout, stderr, ret, runtime, cputime, mem, ibytes, obytes)"
      "values (?, ?, ?, ?, ?, ?, ?, ?, ?)";

  static constexpr const char *remove_job_query = "delete from jobs where job_id = ?";

  JobTable(std::shared_ptr<job_cache::Database> db)
      : db(db),
        add_job(db, insert_query),
        add_output_info(db, add_output_info_query),
        remove_job(db, remove_job_query) {
    add_job.set_why("Could not insert job");
    add_output_info.set_why("Could not add output info");
    remove_job.set_why("Could not remove job");
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

  void remove(int64_t job_id) {
    remove_job.bind_integer(1, job_id);
    remove_job.step();
    remove_job.reset();
  }
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
      result.client_cwd = find_job_request.client_cwd;
      result.output_files = read_outputs(job_id);             // paths are sandbox-absolute here
      result.output_dirs = read_output_dirs(job_id);          // paths are sandbox-absolute here
      result.output_symlinks = read_output_symlinks(job_id);  // paths are sandbox-absolute here
      result.output_info = std::move(*output_info);
      result.input_files = std::move(*found_input_files);
      result.input_dirs = std::move(*found_input_dirs);

      return wcl::make_some<std::pair<int, MatchingJob>>(job_id, std::move(result));
    }

    return {};
  }
};

// The very last value is assumed to be a file and
// is not created.
// This function assumes Iter is an absolute path.
template <class Iter>
static void mkdir_all(Iter begin, Iter end) {
  std::string acc = "/";
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

DaemonCache::DaemonCache(std::string dir, uint64_t max, uint64_t low)
    : rng(wcl::xoshiro_256::get_rng_seed()), max_cache_size(max), low_cache_size(low) {
  wcl::log::info("Launching DaemonCache. dir = %s, max = %lu, low = %lu", dir.c_str(),
                 max_cache_size, low_cache_size)();
  mkdir_no_fail(dir.c_str());
  chdir_no_fail(dir.c_str());

  impl = std::make_unique<CacheDbImpl>(".");

  // Get some random bits to name our domain socket with
  key = rng.unique_name();
  listen_socket_fd = create_cache_socket(".", key);

  launch_evict_loop();
}

int DaemonCache::run() {
  auto cleanup = wcl::make_defer([]() {
    unlink_no_fail(".key");
    wcl::log::info("Exiting run loop.")();
  });

  poll.add(listen_socket_fd);
  while (!exit_now) {
    // recieve and process messages
    struct timespec wait_until;
    wait_until.tv_sec = 60 * 10;
    wait_until.tv_nsec = 0;

    auto fds = poll.wait(&wait_until, nullptr);

    if (fds.empty() && message_parsers.empty()) {
      wcl::log::info("No initial connection for 10 mins, exiting.")();
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

void DaemonCache::remove_corrupt_job(int64_t job_id) {
  // First remove this job from the database so that we don't get hung up on it anymore
  impl->jobs.remove(job_id);

  // Find this job directory so we can remove all the file
  uint8_t group_id = job_id & 0xFF;
  std::string job_dir = wcl::join_paths(wcl::to_hex(&group_id), std::to_string(job_id));

  // Iterate over these files collecting the paths to delete
  std::vector<std::string> to_delete;
  auto dir_res = wcl::directory_range::open(job_dir);
  if (!dir_res) {
    // We can keep going even with this failure but we need to at least log it
    wcl::log::error("cleaning corrupt job: wcl::directory_range::open(%s): %s", job_dir.c_str(),
                    strerror(dir_res.error()))();
    return;
  }

  // Find all the entries to remove
  for (const auto &entry : *dir_res) {
    if (!entry) {
      // It isn't critical that we remove this so just log the error and move on
      wcl::log::error("cleaning corrupt job: bad entry in %s: %s", job_dir.c_str(),
                      strerror(entry.error()))();
      return;
    }
    to_delete.emplace_back(wcl::join_paths(job_dir, entry->name));
  }

  // Unlink them all
  for (const auto &file : to_delete) {
    unlink_no_fail(file.c_str());
  }

  // Remove the files
  rmdir_no_fail(job_dir.c_str());
}

FindJobResponse DaemonCache::read(const FindJobRequest &find_request) {
  wcl::optional<std::pair<int, MatchingJob>> matching_job;

  // We want to hold the database lock for as little time as possible
  impl->transact.run([this, &find_request, &matching_job]() {
    matching_job = impl->matching_jobs.find(find_request);
  });

  // Return early if there was no match.
  if (!matching_job) return FindJobResponse(wcl::optional<MatchingJob>{});

  int job_id = matching_job->first;
  MatchingJob &result = matching_job->second;

  // We need a tmp directory to put these outputs into
  std::string tmp_job_dir = "tmp_outputs_" + rng.unique_name();
  mkdir_no_fail(tmp_job_dir.c_str());

  // We also need to know what directory we're reading out of
  uint8_t group_id = job_id & 0xFF;
  std::string job_dir = wcl::join_paths(wcl::to_hex(&group_id), std::to_string(job_id));

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
      remove_corrupt_job(job_id);
      break;
    }
    to_copy.emplace_back(std::make_tuple(std::move(tmp_file), output_file.path, output_file.mode));
  }

  auto rewite_path = [&find_request](const std::string &sandbox_destination) {
    std::vector<std::string> path_vec = wcl::split_path(sandbox_destination);
    // So the file that the sandbox wrote to `sandbox_destination` currently
    // lives at `tmp_file` and is safe from interference. The sandbox location
    // needs to be redirected to some other output location however.
    auto pair = find_request.dir_redirects.find_max(path_vec.begin(), path_vec.end());
    if (pair.first == nullptr) {
      std::string output_path = wcl::join_paths(find_request.client_cwd, sandbox_destination);
      return std::make_pair<std::string, std::vector<std::string>>(std::move(output_path),
                                                                   std::move(path_vec));
    }
    const auto &output_dir = *pair.first;
    const auto &rel_path = wcl::join('/', pair.second, path_vec.end());
    std::string output_path = wcl::join_paths(output_dir, rel_path);

    if (wcl::is_relative(output_path)) {
      output_path = wcl::join_paths(find_request.client_cwd, output_path);
    }

    std::vector<std::string> output_path_vec = wcl::split_path(output_path);
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

      if (wcl::is_relative(pair.first)) {
        wcl::log::error("'%s' must be an absolute path.", pair.first.c_str()).urgent()();
        exit(1);
      }

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

      if (wcl::is_relative(pair.first)) {
        wcl::log::error("'%s' must be an absolute path.", pair.first.c_str()).urgent()();
        exit(1);
      }

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
  if (!success) return FindJobResponse(wcl::optional<MatchingJob>{});

  // The MatchingJob is currently using sandbox-absolute paths.
  // We need to redirect those sandbox-absolute paths to client-absolute
  // paths. After that, and in order to keep Wake code hygenic and simple,
  // we convert those client-absolute paths to client-relative paths.
  auto redirect_path = [&find_request](std::string &path) {
    // First we convert sandbox-absolute paths to client-absolute paths
    std::vector<std::string> output_path_vec = wcl::split_path(path);
    auto pair = find_request.dir_redirects.find_max(output_path_vec.begin(), output_path_vec.end());
    if (!pair.first) return;
    std::string rel_path = wcl::join('/', pair.second, output_path_vec.end());
    path = wcl::join_paths(*pair.first, rel_path);
    // Then we convert client-absolute paths to client-relative paths
    if (wcl::is_absolute(path)) {
      path = wcl::relative_to(find_request.client_cwd, path);
    }
  };

  /***********************************************************************
   * Now we convert all sandbox-absolute paths to client-relative paths. *
   ***********************************************************************/
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
    wcl::log::warning("Failed to send eviction update: %s", strerror(errno))();
  }

  return FindJobResponse(wcl::make_some<MatchingJob>(std::move(result)));
}

void DaemonCache::add(const AddJobRequest &add_request) {
  // Create a unique name for the job dir (will rename later to correct name)
  std::string tmp_job_dir = "tmp_" + rng.unique_name();
  mkdir_no_fail(tmp_job_dir.c_str());

  // Copy the output files into the temp dir
  for (auto output_file : add_request.outputs) {
    // TODO(jake): See if this file already exists in another job to
    //             avoid the copy by using a link and also save disk
    //             space.
    std::string blob_path = wcl::join_paths(tmp_job_dir, output_file.hash.to_hex());

    if (wcl::is_relative(output_file.source)) {
      output_file.source = wcl::join_paths(add_request.client_cwd, output_file.source);
    }

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
  // the insertion. At that point reads should succeed.
  uint8_t job_group = job_id & 0xFF;
  std::string job_group_dir = wcl::to_hex<uint8_t>(&job_group);
  mkdir_no_fail(job_group_dir.c_str());
  std::string job_dir = wcl::join_paths(job_group_dir, std::to_string(job_id));
  rename_no_fail(tmp_job_dir.c_str(), job_dir.c_str());

  EvictionCommand cmd(EvictionCommandType::Write, job_id);

  std::string msg = cmd.serialize();
  msg += '\0';

  if (write(evict_stdin, msg.data(), msg.size()) == -1) {
    wcl::log::warning("Failed to send eviction update: %s", strerror(errno))();
  }
}

void DaemonCache::launch_evict_loop() {
  const size_t read_side = 0;
  const size_t write_side = 1;

  int stdinPipe[2];
  int stdoutPipe[2];

  if (pipe(stdinPipe) < 0) {
    wcl::log::error("Failed to allocate eviction pipe: %s", strerror(errno)).urgent()();
    exit(1);
  }

  if (pipe(stdoutPipe) < 0) {
    wcl::log::error("Failed to allocate eviction pipe: %s", strerror(errno)).urgent()();
    exit(1);
  }

  int pid = fork();

  // error forking
  if (pid < 0) {
    wcl::log::error("Failed to fork eviction process: %s", strerror(errno)).urgent()();
    exit(1);
  }

  // child
  if (pid == 0) {
    if (dup2(stdinPipe[read_side], STDIN_FILENO) == -1) {
      wcl::log::error("Failed to dup2 stdin pipe for eviction process: %s", strerror(errno))
          .urgent()();
      exit(1);
    }

    if (dup2(stdoutPipe[write_side], STDOUT_FILENO) == -1) {
      wcl::log::error("Failed to dup2 stdin pipe for eviction process: %s", strerror(errno))
          .urgent()();
      exit(1);
    }

    close(stdinPipe[read_side]);
    close(stdinPipe[write_side]);
    close(stdoutPipe[read_side]);
    close(stdoutPipe[write_side]);

    // Finally enter the eviction loop, if it exits cleanly
    // go ahead and exit with its result.
    int result =
        eviction_loop(".", std::make_unique<LRUEvictionPolicy>(max_cache_size, low_cache_size));
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

DaemonCache::~DaemonCache() { reap_evict_loop(); }

void DaemonCache::handle_new_client() {
  // Accept the new client socket.
  int accept_fd = accept4(listen_socket_fd, nullptr, nullptr, SOCK_CLOEXEC);
  if (accept_fd == -1) {
    wcl::log::error("accept(%s): %s", key.c_str(), strerror(errno)).urgent()();
    exit(1);
  }

  poll.add(accept_fd);
  message_parsers.insert({accept_fd, MessageParser(accept_fd)});
  wcl::log::info("new client connected: %d", accept_fd)();
}

void DaemonCache::handle_msg(int client_fd) {
  // In case multiple read events have been enqueued since the
  // last epoll_wait, we have to perform all the reads that
  // have been enqueued.
  MessageParserState state;
  std::vector<std::string> msgs;

  auto it = message_parsers.find(client_fd);
  if (it == message_parsers.end()) {
    wcl::log::error("unreachable: message_parsers out of sync with poll. client_fd = %d", client_fd)
        .urgent()();
    exit(1);
  }

  state = it->second.read_messages(msgs);

  for (const auto &msg : msgs) {
    wcl::log::info("msg: %s", msg.c_str())();

    JAST json;
    std::stringstream parseErrors;
    if (!JAST::parse(msg, parseErrors, json)) {
      wcl::log::error("DaemonCache::handle_msg(): failed to parse client request").urgent()();
      exit(1);
    }

    if (json.get("method").value == "cache/read") {
      FindJobRequest req(json.get("params"));
      FindJobResponse res = read(req);
      send_json_message(client_fd, res.to_json());
    }

    if (json.get("method").value == "cache/add") {
      AddJobRequest req(json.get("params"));
      add(req);
    }
  }

  // If the file was closed, remove from epoll and close it.
  if (state == MessageParserState::StopSuccess) {
    wcl::log::info("closing client fd = %d", client_fd)();
    poll.remove(client_fd);
    close(client_fd);
    message_parsers.erase(client_fd);
    if (message_parsers.empty()) {
      exit_now = true;
      wcl::log::info("All clients disconnected, exiting.")();
    }
    return;
  }

  // If there's an error just fail.
  if (state == MessageParserState::StopFail) {
    wcl::log::error("read(%d), key = %s:", client_fd, strerror(errno)).urgent()();
    exit(1);
  }
}

}  // namespace job_cache
