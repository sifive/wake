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
  job_cache::PreparedStatement get_size;
  job_cache::PreparedStatement update_last_use;
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

  // Unconditionally returns the current total_size
  static constexpr const char* get_size_query =
      "select size from total_size where total_size_id = 0";

  // Takes two arguments, first is the latest time as an integer.
  // second is the job_id to update.
  static constexpr const char* update_last_use_query =
      "insert into lru_stats (job_id, last_use) values (?, ?) "
      "on conflict (job_id) do update set last_use=?2";

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
        get_size(db, get_size_query),
        update_last_use(db, update_last_use_query),
        find_least_recently_used(db, find_least_recently_used_query),
        remove_least_recently_used(db, remove_least_recently_used_query),
        transact(db) {
    update_size.set_why("Could not update total size");
    get_size.set_why("Could not get total size");
    update_last_use.set_why("Could not update last use");
    find_least_recently_used.set_why("Could not find least recently used");
    remove_least_recently_used.set_why("Could not remove least recently used");
  }

  uint64_t add_job_size(uint64_t job_id) {
    uint64_t out = 0;
    transact.run([this, &out, job_id]() {
      update_size.bind_integer(1, job_id);
      update_size.step();
      update_size.reset();
      out = get_size.read_integer(0);
      get_size.reset();
    });
    return out;
  }

  void mark_new_use(uint64_t job_id) {
    timespec tp;
    clock_gettime(CLOCK_MONOTONIC, &tp);
    update_last_use.bind_integer(1, job_id);
    update_last_use.bind_integer(2, tp.tv_nsec);
    update_last_use.step();
    update_last_use.reset();
  }

  void cleanup(uint64_t bytes_to_remove) {
    std::vector<int64_t> jobs_to_remove;
    transact.run([this, &jobs_to_remove, bytes_to_remove]() {
      uint64_t last_use;
      uint64_t to_remove = bytes_to_remove;

      {
        auto reset1 = wcl::make_defer([this]() { find_least_recently_used.reset(); });

        // First find the use time that we want to remove from
        while (true) {
          find_least_recently_used.step();

          // Since we entered this loop we know we need to remove at least
          // the next job so mark its last_use, and mark its job ids
          // for file removal later.
          last_use = find_least_recently_used.read_integer(0);
          uint64_t obytes = find_least_recently_used.read_integer(1);
          jobs_to_remove.push_back(find_least_recently_used.read_integer(2));

          // If obytes is more than to_remove then we can break because
          // this will put us over our bytes_to_remove.
          if (obytes < to_remove) {
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
      auto reset2 = wcl::make_defer([this]() { remove_least_recently_used.reset(); });
      remove_least_recently_used.bind_integer(1, last_use);
      remove_least_recently_used.step();
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

// TODO: Make this configurable...right now its just set at 16GB...because
//       that seems vaugely sensible.
constexpr uint64_t max_db_size = 1ULL << 34ULL;

// TODO: Same. I'm just sort of guessing that deleting a GB at
//       at a time is sensible. No real clue.
constexpr uint64_t low_water_mark = max_db_size - (1ULL << 30);

void LRUEvictionPolicy::write(int job_id) {
  impl->mark_new_use(job_id);
  uint64_t size = impl->add_job_size(job_id);
  if (size > max_db_size) {
    impl->cleanup(max_db_size - low_water_mark);
  }
}

LRUEvictionPolicy::LRUEvictionPolicy() {}
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
