/*
 * Copyright 2023 SiFive, Inc.
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

#include "eviction_policy.h"

#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <wcl/defer.h>
#include <wcl/xoshiro_256.h>

#include <algorithm>
#include <future>
#include <iostream>
#include <memory>
#include <thread>
#include <unordered_set>
#include <vector>

#include "db_helpers.h"
#include "eviction_command.h"
#include "job_cache_impl_common.h"
#include "message_parser.h"

namespace job_cache {

struct LRUEvictionPolicyImpl {
  std::string cache_dir;
  job_cache::PreparedStatement update_size;
  job_cache::PreparedStatement reset_size;
  job_cache::PreparedStatement get_size;
  job_cache::PreparedStatement insert_last_use;
  job_cache::PreparedStatement set_last_use;
  job_cache::PreparedStatement get_last_use;
  job_cache::PreparedStatement does_job_exist;
  job_cache::PreparedStatement find_least_recently_used;
  job_cache::PreparedStatement remove_least_recently_used;
  job_cache::Transaction transact;
  std::thread cleaning_thread;

  // TODO: Right now we're using `obytes` as a proxy for the size of a job
  //       but this is an over aproximation of the storage cost really.

  // Update size to account for new job. Also return the size
  // so we can know if we should trigger a collection or not
  static constexpr const char* update_size_query =
      "update total_size set size = size + (select sum(o.obytes) "
      "from jobs j, job_output_info o "
      "where j.job_id = ? and j.job_id = o.job)";

  static constexpr const char* reset_size_query = "update total_size set size = ?";

  // Unconditionally returns the current total_size
  static constexpr const char* get_size_query = "select size from total_size";

  // We need 3 qurries to do an upset because sqlite only added support in 2018
  // and we still test against very old CI bots.
  static constexpr const char* get_last_use_query = "select * from lru_stats where job_id = ?";
  static constexpr const char* insert_last_use_query =
      "insert into lru_stats (job_id, last_use) values (?, ?)";
  static constexpr const char* set_last_use_query =
      "update lru_stats set last_use = ? where job_id = ?";
  static constexpr const char* does_job_exist_query = "select * from jobs where job_id = ?";

  // This is meant to be used as part of a transaction with remove_least_recently_used.
  // It simply returns the job_ids in last use order.
  static constexpr const char* find_least_recently_used_query =
      "select l.last_use, o.obytes, j.job_id, j.commandline "
      "from lru_stats l, jobs j, job_output_info o "
      "where l.job_id = j.job_id and o.job = j.job_id "
      "order by l.last_use";

  // This query is meant to be used as part of a transaction with find_least_recently_used.
  // It removes all jobs that have a last use time less than a certain amount.
  static constexpr const char* remove_least_recently_used_query =
      "delete from jobs where job_id in (select job_id from lru_stats where last_use <= ?)";

  LRUEvictionPolicyImpl(std::string dir, std::shared_ptr<job_cache::Database> db)
      : cache_dir(dir),
        update_size(db, update_size_query),
        reset_size(db, reset_size_query),
        get_size(db, get_size_query),
        insert_last_use(db, insert_last_use_query),
        set_last_use(db, set_last_use_query),
        get_last_use(db, get_last_use_query),
        does_job_exist(db, does_job_exist_query),
        find_least_recently_used(db, find_least_recently_used_query),
        remove_least_recently_used(db, remove_least_recently_used_query),
        transact(db) {
    update_size.set_why("Could not update total size");
    get_size.set_why("Could not get total size");
    insert_last_use.set_why("Could not insert new last use");
    set_last_use.set_why("Could not update last use");
    get_last_use.set_why("Could not get last use");
    find_least_recently_used.set_why("Could not find least recently used");
    remove_least_recently_used.set_why("Could not remove least recently used");
    reset_size.set_why("Could not reset size");
  }

  uint64_t add_job_size(uint64_t job_id) {
    uint64_t out = 0;
    transact.run([this, &out, job_id]() {
      update_size.bind_integer(1, job_id);
      update_size.step();
      update_size.reset();
      get_size.step();
      out = get_size.read_integer(0);
      get_size.reset();
    });
    return out;
  }

  void mark_new_use(uint64_t job_id) {
    timespec tp;
    clock_gettime(CLOCK_REALTIME, &tp);
    int64_t time = 1000000ll * int64_t(tp.tv_sec) + tp.tv_nsec / 1000;
    // Older versions of sqlite don't have upserts so
    // we have to do this junk.
    transact.run([this, job_id, &time]() {
      does_job_exist.bind_integer(1, job_id);
      auto exists = does_job_exist.step();
      does_job_exist.reset();
      if (exists != SQLITE_ROW) return;
      set_last_use.bind_integer(1, time);
      set_last_use.bind_integer(2, job_id);
      set_last_use.step();
      set_last_use.reset();
      get_last_use.bind_integer(1, job_id);
      auto result = get_last_use.step();
      get_last_use.reset();
      // If there was already a result, we can safely assume it was set and return
      if (result == SQLITE_ROW) return;

      // Unless result is a row something awful has occured.
      if (result != SQLITE_DONE) {
        wcl::log::error("get_last_use result was unexpected: %d", result).urgent()();
        exit(1);
      }

      insert_last_use.bind_integer(1, job_id);
      insert_last_use.bind_integer(2, time);
      insert_last_use.step();
      insert_last_use.reset();
    });
  }

  void cleanup(uint64_t current_size, uint64_t bytes_to_remove) {
    std::vector<std::pair<int64_t, std::string>> jobs_to_remove;
    transact.run([this, &jobs_to_remove, current_size, bytes_to_remove]() {
      uint64_t last_use = 0;
      uint64_t to_remove = bytes_to_remove;
      uint64_t removed_so_far = 0;

      {
        auto reset1 = wcl::make_defer([this]() { find_least_recently_used.reset(); });

        // First find the use time that we want to remove from
        while (find_least_recently_used.step() == SQLITE_ROW) {
          // First see how many bytes removing this would net us
          uint64_t obytes = find_least_recently_used.read_integer(1);

          last_use = find_least_recently_used.read_integer(0);

          int64_t job_id = find_least_recently_used.read_integer(2);
          std::string cmd = find_least_recently_used.read_string(3);

          // The cmd uses null bytes to seperate parts of a command so, we
          // replace them with spaces to make it readable.
          for (auto& ch : cmd) {
            if (ch == '\0') ch = ' ';
          }

          jobs_to_remove.emplace_back(job_id, std::move(cmd));

          removed_so_far += obytes;

          // If obytes is more than to_remove then we can break because
          // this will put us over our bytes_to_remove.
          if (obytes > to_remove) {
            break;
          }

          // Otherwise mark these bytes and keep working.
          to_remove -= obytes;
        }
      }

      // Next actully remove those jobs from the database
      // even though the files haven't been deleted yet.
      // Note that just because we delete jobs from the database
      // before we delete the files backing them doesn't mean that
      // we can't see a race. It's perfectly valid for a read to
      // occur before we do this transaction but for the reads of
      // the backing files to occur *after* this transaction completes.
      // Still, doing things in this order reduces the chance of a
      // failed read occuring.
      auto reset2 = wcl::make_defer([this]() {
        remove_least_recently_used.reset();
        reset_size.reset();
      });
      remove_least_recently_used.bind_integer(1, last_use);
      remove_least_recently_used.step();
      reset_size.bind_integer(1, current_size - removed_so_far);
      reset_size.step();
    });

    // Now we remove the threads in the background.
    // NOTE: We don't wait for this to finish
    // There might be an already running thread finishing up some cleaning.
    // Instead of killing it we should join it before starting a new one.
    if (cleaning_thread.joinable()) cleaning_thread.join();
    // Launch the cleaning thread
    cleaning_thread = std::thread(remove_backing_files, cache_dir, jobs_to_remove,
                                  4 * std::thread::hardware_concurrency());
  }
};

static void garbage_collect_job(std::string job_dir) {
  wcl::log::info("found orphaned job folder: %s", job_dir.c_str())();
  auto dir_res = wcl::directory_range::open(job_dir);
  if (!dir_res) {
    // It's not an error if this directory doesn't exist
    if (dir_res.error() == ENOENT) {
      return;
    }

    // We can keep going even with this failure but we need to at least log it
    wcl::log::error("garbage collecting orphaned folders: wcl::directory_range::open(%s): %s",
                    job_dir.c_str(), strerror(dir_res.error()))();
    return;
  }

  // Find all the entries to remove
  for (const auto& entry : *dir_res) {
    if (!entry) {
      // If one entry has a failure we can just keep going to try and remove more entries
      wcl::log::error("cleaning corrupt job: bad entry in %s: %s", job_dir.c_str(),
                      strerror(entry.error()))();
      continue;
    }
    std::string file = wcl::join_paths(job_dir, entry->name);

    // unlink, if we fail we just ignore the error so that we don't fail
    unlink(file.c_str());

    // This isn't a super important task so we want to wait a bit between iterations
    usleep(200);
  }

  // Finally clean up the file so we don't try to clean it up again later
  rmdir(job_dir.c_str());
}

static void garbage_collect_group(const std::unordered_set<int64_t> jobs, int64_t max_job,
                                  group_id_t group_id) {
  auto group_dir = wcl::to_hex(&group_id);
  auto dir_res = wcl::directory_range::open(group_dir);
  if (!dir_res) {
    // We can keep going even with this failure but we need to at least log it
    wcl::log::error("garbage collecting orphaned folders: wcl::directory_range::open(%s): %s",
                    group_dir.c_str(), strerror(dir_res.error()))();
    return;
  }

  // Find all the entries to remove
  std::vector<int64_t> jobs_to_check;
  for (const auto& entry : *dir_res) {
    if (!entry) {
      // It isn't critical that we remove this so just log the error and move on
      wcl::log::error("cleaning corrupt job: bad entry in %s: %s", group_dir.c_str(),
                      strerror(entry.error()))();
      continue;
    }
    if (entry->name == "." || entry->name == "..") {
      continue;
    }
    int64_t job_id = std::stoll(entry->name);
    // Jobs might be added that aren't in the jobs list. They will
    // all have a job_id greater than the largest we found at startup so
    // we ignore them.
    if (job_id > max_job) continue;

    // Otherwise add to the list to check later
    jobs_to_check.push_back(job_id);
  }

  // Collect the jobs in a second loop so that we don't mutate the list
  // while we're traversing it.
  for (auto job_id : jobs_to_check) {
    if (jobs.count(job_id)) {
      continue;
    }
    garbage_collect_job(wcl::join_paths(group_dir, std::to_string(job_id)));
  }
}

static void garbage_collect_orphan_folders(std::shared_ptr<job_cache::Database> db) {
  constexpr const char* all_jobs_q = "select job_id from jobs";
  PreparedStatement all_jobs(db, all_jobs_q);
  Transaction transact(db);
  std::unordered_set<int64_t> jobs;
  int64_t max_job = -1;

  // First we run a very large query to find all job ids
  transact.run([&all_jobs, &max_job, &jobs]() {
    // Loop over every single job
    while (all_jobs.step() == SQLITE_ROW) {
      int64_t job_id = all_jobs.read_integer(0);
      max_job = std::max(max_job, job_id);
      jobs.insert(job_id);
    }
  });

  // Next we slowly loop over job cache looking for orphaned folders
  // Note we have to use a larger type than group_id_t to know when
  // to exit this loop correctly.
  for (int group_id = 0; group_id <= 0xFF; ++group_id) {
    garbage_collect_group(jobs, max_job, uint8_t(group_id));
  }
}

void LRUEvictionPolicy::init(const std::string& cache_dir) {
  std::shared_ptr<job_cache::Database> db = std::make_unique<job_cache::Database>(cache_dir);
  impl = std::make_unique<LRUEvictionPolicyImpl>(cache_dir, db);

  // To keep this thread alive, we assign it to a static thread object.
  // This starts the collection but if the programs ends so too will this thread.
  gc_thread = std::thread(garbage_collect_orphan_folders, db);
}

void LRUEvictionPolicy::read(int job_id) { impl->mark_new_use(job_id); }

void LRUEvictionPolicy::write(int job_id) {
  impl->mark_new_use(job_id);
  uint64_t size = impl->add_job_size(job_id);
  if (size > max_cache_size) {
    // TODO: Techically this is racy because the size
    //       can get out of sync. We should probably put
    //       these in a transaction.
    impl->cleanup(size, max_cache_size - low_cache_size);
  }
}

LRUEvictionPolicy::LRUEvictionPolicy(uint64_t max, uint64_t low)
    : max_cache_size(max), low_cache_size(low) {}

LRUEvictionPolicy::~LRUEvictionPolicy() {}

int eviction_loop(const std::string& cache_dir, std::unique_ptr<EvictionPolicy> policy) {
  policy->init(cache_dir);

  MessageParser msg_parser(STDIN_FILENO);
  MessageParserState state;
  do {
    std::vector<std::string> msgs;
    state = msg_parser.read_messages(msgs);

    for (const auto& m : msgs) {
      auto cmd = EvictionCommand::parse(m);
      if (!cmd) {
        exit(EXIT_FAILURE);
      }

      switch (cmd->type) {
        case EvictionCommandType::Read:
          policy->read(cmd->job_id);
          break;
        case EvictionCommandType::Write:
          policy->write(cmd->job_id);
          break;
        default:
          exit(EXIT_FAILURE);
      }
    }
  } while (state == MessageParserState::Continue);

  exit(state == MessageParserState::StopSuccess ? EXIT_SUCCESS : EXIT_FAILURE);
}

}  // namespace job_cache
