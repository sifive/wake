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

#include <algorithm>
#include <future>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#include "db_helpers.h"
#include "eviction_command.h"
#include "job_cache_impl_common.h"

namespace job_cache {

enum class CommandParserState { Continue, StopSuccess, StopFail };

struct CommandParser {
  std::string command_buff = "";

  CommandParser() {}

  CommandParserState read_commands(std::vector<std::string>& commands) {
    commands = {};

    while (true) {
      uint8_t buffer[4096] = {};
      ssize_t count = read(STDIN_FILENO, static_cast<void*>(buffer), 4096);
      // Pipe has been closed. Stop processing
      if (count == 0) {
        return CommandParserState::StopSuccess;
      }

      // An error occured during read
      if (count < 0) {
        return CommandParserState::StopFail;
      }

      uint8_t* iter = buffer;
      uint8_t* buffer_end = buffer + count;
      while (iter < buffer_end) {
        auto end = std::find(iter, buffer_end, 0);
        command_buff.append(iter, end);
        if (end != buffer_end) {
          commands.emplace_back(std::move(command_buff));
          command_buff = "";
        }
        iter = end + 1;
      }

      if (count < 4096) {
        return CommandParserState::Continue;
      }
    }

    // not actually reachable
    return CommandParserState::Continue;
  }
};

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
  static constexpr const char* does_job_exist_query =
      "select * from jobs where job_id = ?";

  // This is meant to be used as part of a transaction with remove_least_recently_used.
  // It simply returns the job_ids in last use order.
  static constexpr const char* find_least_recently_used_query =
      "select l.last_use, o.obytes, j.job_id "
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
      //std::cerr << "update_size: About to bind" << std::endl;
      update_size.bind_integer(1, job_id);
      //std::cerr << "update_size: Now bound" << std::endl;
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
    // Older versions of sqlite don't have upserts so
    // we have to do this junk.
    transact.run([this, job_id, &tp]() {
      //std::cerr << "checking if the job exists" << std::endl;
      does_job_exist.bind_integer(1, job_id);
      auto exists = does_job_exist.step();
      does_job_exist.reset();
      if (exists != SQLITE_ROW) return;
      //std::cerr << "set_last_use: About to bind" << std::endl;
      set_last_use.bind_integer(1, tp.tv_sec);
      //std::cerr << "set_last_use: Now bound" << std::endl;
      set_last_use.bind_integer(2, job_id);
      set_last_use.step();
      set_last_use.reset();
      //std::cerr << "get_last_use: About to bind" << std::endl;
      get_last_use.bind_integer(1, job_id);
      //std::cerr << "get_last_use: Now bound" << std::endl;
      auto result = get_last_use.step();
      get_last_use.reset();
      // If there was already a result, we can safely assume it was set and return
      if (result == SQLITE_ROW) return;

      // If there wasn't however we need to insert the result
      if (result == SQLITE_DONE) {
        //std::cerr << "insert_last_use: About to bind" << std::endl;
        insert_last_use.bind_integer(1, job_id);
        //std::cerr << "insert_last_use: Now bound" << std::endl;
        //std::cerr << "job_id = " << job_id << std::endl;
        insert_last_use.bind_integer(2, tp.tv_sec);
        insert_last_use.step();
        insert_last_use.reset();
      } else {
        log_fatal("get_last_use result was unexpected: %d", result);
      }
    });
  }

  void cleanup(uint64_t current_size, uint64_t bytes_to_remove) {
    std::vector<int64_t> jobs_to_remove;
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

          jobs_to_remove.push_back(find_least_recently_used.read_integer(2));

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
      auto reset2 = wcl::make_defer([this]() { remove_least_recently_used.reset(); reset_size.reset(); });
      //std::cerr << "before remove_least_recently_used" << std::endl;
      remove_least_recently_used.bind_integer(1, last_use);
      //std::cerr << "after remove_least_recently_used" << std::endl;
      remove_least_recently_used.step();
      //std::cerr << "before reset_size" << std::endl;
      reset_size.bind_integer(1, current_size - removed_so_far);
      //std::cerr << "after reset_size" << std::endl;
      reset_size.step();
    });

    // Now we remove the threads in the background.
    // TODO: Figure out how many cores we actully have and use a multiple of that
    // NOTE: We don't wait for this to finish
    std::async(std::launch::async, [dir = cache_dir, jobs_to_remove = std::move(jobs_to_remove)]() {
      remove_backing_files(dir, jobs_to_remove, 8, 128);
    });
  }
};

void LRUEvictionPolicy::init(const std::string& cache_dir) {
  auto db = std::make_unique<job_cache::Database>(cache_dir);
  impl = std::make_unique<LRUEvictionPolicyImpl>(cache_dir, std::move(db));
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

  CommandParser cmd_parser;
  CommandParserState state;
  do {
    std::vector<std::string> cmds;
    state = cmd_parser.read_commands(cmds);

    for (const auto& c : cmds) {
      auto cmd = EvictionCommand::parse(c);
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
  } while (state == CommandParserState::Continue);

  exit(state == CommandParserState::StopSuccess ? EXIT_SUCCESS : EXIT_FAILURE);
}

}  // namespace job_cache
