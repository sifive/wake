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

#include <algorithm>
#include <iostream>
#include <memory>
#include <vector>

#include "db_helpers.h"
#include "eviction_command.h"
#include "gopt/gopt-arg.h"
#include "gopt/gopt.h"
#include "wcl/defer.h"

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
        std::cerr << "Failed to read from stdin: " << strerror(errno) << std::endl;
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
  PreparedStatement update_size;
  PreparedStatement update_last_use;
  PreparedStatement find_least_recently_used;
  PreparedStatement remove_least_recently_used;
  Transaction transact;

  // TODO: Right now we're using `obytes` as a proxy for the size of a job
  //       but this is an over aproximation of the storage cost really.

  // Update size to account for new job. Also return the size
  // so we can know if we should trigger a collection or not
  static constexpr const char* update_size_query =
      "update total_size set size = size + (select sum(obytes) from jobs where job_id = ?) "
      "returning size";

  // Takes two arguments, first is the latest time as an integer.
  // second is the job_id to update.
  static constexpr const char* update_last_use_query =
      "update lru_stats set last_use = ? where job_id = ?";

  // This is meant to be used as part of a transaction with remove_least_recently_used.
  // It simply returns the job_ids in last use order.
  static constexpr const char* find_least_recently_used_query =
      "select l.last_use, j.obytes, j.job_id from lru_stats l, jobs j where l.job_id = j.job_id "
      "order by "
      "l.last_use";

  // This query is meant to be used as part of a transaction with find_least_recently_used.
  // It removes all jobs that have a last use time less than a certain amount.
  static constexpr const char* remove_least_recently_used_query =
      "delete from jobs where job_id in (select job_id from lru_stats where last_use <= ?)";

  LRUEvictionPolicyImpl(std::shared_ptr<Database> db)
      : update_size(db, update_size_query),
        update_last_use(db, update_last_use_query),
        find_least_recently_used(db, find_least_recently_used_query),
        remove_least_recently_used(db, remove_least_recently_used_query),
        transact(db) {
    update_size.set_why("Could not update total size");
    update_last_use.set_why("Could not update last use");
    find_least_recently_used.set_why("Could not find least recently used");
    remove_least_recently_used.set_why("Could not remove least recently used");
  }

  uint64_t update_size(uint64_t job_id) {
    update_size.bind_integer(1, job_id);
    update_size.step();
    uint64_t out = update_size.read_integer(0);
    update_size.reset();
    return out;
  }

  void update_last_use(uint64_t job_id) {
    timespec tp;
    clock_gettime(CLOCK_MONOTONIC, &tp);
    update_last_use.bind_integer(1, tp.tv_nsec);
    update_last_use.bind_integer(2, job_id);
    update_last_use.step();
    update_last_use.reset();
  }

  void cleanup(uint64_t bytes_to_remove) {
    std::vector<int64_t> jobs_to_remove;
    transact.run([this, &jobs_to_remove, bytes_to_remove]() {
      uint64_t last_use;
      uint64_t to_remove = bytes_to_remove;

      auto reset = wcl::make_defer([this]() { find_least_recently_used.reset(); });

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

      // Next actully remove those jobs from the database
      // even though the files haven't been deleted yet.
      // Note that just because we delete jobs from the database
      // before we delete the files backing them doesn't mean that
      // we can't see a race. It's perfectly valid for a read to
      // occur before we do this transaction but for the reads of
      // the backing files to occur *after* this transaction completes.
      // Still, doing things in this order reduces the chance of a
      // failed read occuring.
      auto reset = wcl::make_defer([this]() { remove_least_recently_used.reset(); });
      remove_least_recently_used.bind_integer(1, last_use);
      remove_least_recently_used.step();
    });
  }
};

void LRUEvictionPolicy::init(const std::string& cache_dir) {}

void LRUEvictionPolicy::read(int id) {}

void LRUEvictionPolicy::write(int id) {}

int eviction_loop(std::string cache_dir, std::unique_ptr<EvictionPolicy> policy) {
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
          std::cerr << "Unhandled command type" << std::endl;
          exit(EXIT_FAILURE);
      }
    }
  } while (state == CommandParserState::Continue);

  exit(state == CommandParserState::StopSuccess ? EXIT_SUCCESS : EXIT_FAILURE);
}
