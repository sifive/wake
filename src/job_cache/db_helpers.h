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

#pragma once

// NOTE: PLEASE DO NOT INCLUDE THIS OUTSIDE OF .cpp FILE IN THIS DIR!!!
//       IMPORTANTLY DO NOT INCLUDE THEM IN HEADERS IN THIS DIR!!!
//       This file exposes sqlite3 which right now is abstracted away.
#include <sqlite3.h>
#include <wcl/filepath.h>
#include <wcl/tracing.h>

#include <random>
#include <string>

#include "job_cache_impl_common.h"

namespace job_cache {

class Database {
 private:
  sqlite3 *db = nullptr;

  // This is fairly critical to getting strong concurency.
  // Specifically adding in exponetial back off plus randomization.
  static inline int wait_handle(void *, int retries) {
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

 public:
  Database(const Database &) = delete;
  Database(Database &&) = delete;
  Database() = delete;
  ~Database() {
    if (db && sqlite3_close(db) != SQLITE_OK) {
      wcl::log::fatal("Could not close database: %s", sqlite3_errmsg(db));
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

    std::string db_path = wcl::join_paths(cache_dir, "/cache.db");
    if (sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                        nullptr) != SQLITE_OK) {
      wcl::log::fatal("error: %s", sqlite3_errmsg(db));
    }

    if (sqlite3_busy_handler(db, wait_handle, nullptr)) {
      wcl::log::fatal("error: failed to set sqlite3_busy_handler: %s", sqlite3_errmsg(db));
    }

    char *fail = nullptr;

    int ret = sqlite3_exec(db, cache_schema, nullptr, nullptr, &fail);

    if (ret == SQLITE_BUSY) {
      wcl::log::fatal(
          "warning: It appears another process is holding the database open, check `ps` for "
          "suspended wake instances");
    }
    if (ret != SQLITE_OK) {
      wcl::log::fatal("error: failed init stmt: %s: %s", fail, sqlite3_errmsg(db));
    }
  }

  sqlite3 *get() const { return db; }
};

class PreparedStatement {
 private:
  std::shared_ptr<job_cache::Database> db = nullptr;
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

  PreparedStatement(std::shared_ptr<job_cache::Database> db, const std::string &sql_str) : db(db) {
    if (sqlite3_prepare_v2(db->get(), sql_str.c_str(), sql_str.size(), &query_stmt, nullptr) !=
        SQLITE_OK) {
      wcl::log::fatal("error: failed to prepare statement: %s", sqlite3_errmsg(db->get()));
    }
  }

  ~PreparedStatement() {
    if (query_stmt) {
      int ret = sqlite3_finalize(query_stmt);
      if (ret != SQLITE_OK) {
        wcl::log::fatal("sqlite3_finalize: %s", sqlite3_errmsg(db->get()));
      }
      query_stmt = nullptr;
    }
  }

  void set_why(std::string why) { this->why = std::move(why); }

  void bind_integer(int64_t index, int64_t value) {
    int ret = sqlite3_bind_int64(query_stmt, index, value);
    if (ret != SQLITE_OK) {
      wcl::log::fatal("%s: sqlite3_bind_int64(%ld, %ld): %s", why.c_str(), index, value,
                      sqlite3_errmsg(db->get()));
    }
  }

  void bind_double(int64_t index, double value) {
    int ret = sqlite3_bind_double(query_stmt, index, value);
    if (ret != SQLITE_OK) {
      wcl::log::fatal("%s: sqlite3_bind_double(%ld, %f): %s", why.c_str(), index, value,
                      sqlite3_errmsg(db->get()));
    }
  }

  void bind_string(int64_t index, const std::string &value) {
    int ret = sqlite3_bind_text(query_stmt, index, value.c_str(), value.size(), SQLITE_TRANSIENT);
    if (ret != SQLITE_OK) {
      wcl::log::fatal("%s: sqlite3_bind_text(%ld, %s): %s", why.c_str(), index, value.c_str(),
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

    ret = sqlite3_reset(query_stmt);
    if (ret == SQLITE_LOCKED) {
      wcl::log::fatal("error: sqlite3_reset: SQLITE_LOCKED");
    }

    if (ret != SQLITE_OK) {
      wcl::log::fatal("error: %s; sqlite3_reset: %s", why.c_str(), sqlite3_errmsg(db->get()));
    }

    if (sqlite3_clear_bindings(query_stmt) != SQLITE_OK) {
      wcl::log::fatal("error: %s; sqlite3_clear_bindings: %s", why.c_str(),
                      sqlite3_errmsg(db->get()));
    }
  }

  int step() {
    int ret;
    ret = sqlite3_step(query_stmt);
    if (ret != SQLITE_DONE && ret != SQLITE_ROW) {
      wcl::log::fatal("error: %s; sqlite3_step: %s", why.c_str(), sqlite3_errmsg(db->get()));
    }
    return ret;
  }
};

class Transaction {
 private:
  PreparedStatement begin_txn_query;
  PreparedStatement commit_txn_query;

 public:
  static constexpr const char *sql_begin_txn = "begin immediate transaction";
  static constexpr const char *sql_commit_txn = "commit transaction";

  Transaction(std::shared_ptr<job_cache::Database> db)
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

}  // namespace job_cache
