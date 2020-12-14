/*
 * Copyright 2019 SiFive, Inc.
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

#include "database.h"
#include "status.h"
#include <unordered_set>
#include <iostream>
#include <sstream>
#include <set>
#include <sqlite3.h>
#include <unistd.h>
#include <string.h>

// Increment every time the database schema changes
#define SCHEMA_VERSION "1"

#define VISIBLE 0
#define INPUT 1
#define OUTPUT 2
#define INDEXES 3

struct Database::detail {
  bool debugdb;
  sqlite3 *db;
  sqlite3_stmt *get_entropy;
  sqlite3_stmt *set_entropy;
  sqlite3_stmt *begin_txn;
  sqlite3_stmt *commit_txn;
  sqlite3_stmt *predict_job;
  sqlite3_stmt *stats_job;
  sqlite3_stmt *insert_job;
  sqlite3_stmt *insert_tree;
  sqlite3_stmt *insert_log;
  sqlite3_stmt *wipe_file;
  sqlite3_stmt *insert_file;
  sqlite3_stmt *update_file;
  sqlite3_stmt *get_log;
  sqlite3_stmt *replay_log;
  sqlite3_stmt *get_tree;
  sqlite3_stmt *add_stats;
  sqlite3_stmt *link_stats;
  sqlite3_stmt *detect_overlap;
  sqlite3_stmt *delete_overlap;
  sqlite3_stmt *find_prior;
  sqlite3_stmt *update_prior;
  sqlite3_stmt *delete_prior;
  sqlite3_stmt *find_owner;
  sqlite3_stmt *find_last;
  sqlite3_stmt *find_failed;
  sqlite3_stmt *fetch_hash;
  sqlite3_stmt *delete_jobs;
  sqlite3_stmt *delete_dups;
  sqlite3_stmt *delete_stats;
  sqlite3_stmt *revtop_order;
  sqlite3_stmt *setcrit_path;
  sqlite3_stmt *tag_job;

  long run_id;
  detail(bool debugdb_)
   : debugdb(debugdb_), db(0), get_entropy(0), set_entropy(0), begin_txn(0),
     commit_txn(0), predict_job(0), stats_job(0), insert_job(0), insert_tree(0), insert_log(0),
     wipe_file(0), insert_file(0), update_file(0), get_log(0), replay_log(0), get_tree(0), add_stats(0),
     link_stats(0), detect_overlap(0), delete_overlap(0), find_prior(0), update_prior(0), delete_prior(0),
     find_owner(0), find_last(0), find_failed(0), fetch_hash(0), delete_jobs(0), delete_dups(0),
     delete_stats(0), revtop_order(0), setcrit_path(0) { }
};

Database::Database(bool debugdb) : imp(new detail(debugdb)) { }
Database::~Database() { close(); }

static int schema_cb(void *data, int columns, char **values, char **labels) {
  // values[0] = 0 if a fresh DB
  // values[1] = schema version

  // Returning non-zero causes SQLITE_ABORT
  if (columns != 2) return -1;

  // New DB? Ok to use it
  if (!strcmp(values[0], "0")) return 0;

  // No version set? This must be a pre-0.19 database.
  if (!values[1]) return -1;

  // Matching version? Ok to use it
  if (!strcmp(values[1], SCHEMA_VERSION)) return 0;

  // Versions do not match
  return -1;
}

std::string Database::open(bool wait, bool memory) {
  if (imp->db) return "";
  int ret;

  ret = sqlite3_open_v2(memory?":memory:":"wake.db", &imp->db, SQLITE_OPEN_READWRITE, 0);
  if (ret != SQLITE_OK) {
    if (!imp->db) return "sqlite3_open: out of memory";
    std::string out = sqlite3_errmsg(imp->db);
    close();
    return out;
  }

#if SQLITE_VERSION_NUMBER >= 3007011
  if (sqlite3_db_readonly(imp->db, 0)) {
    return "read-only";
  }
#endif

  // Increment the SCHEMA_VERSION every time the below string changes.
  const char *schema_sql =
    "pragma auto_vacuum=incremental;"
    "pragma journal_mode=wal;"
    "pragma synchronous=0;"
    "pragma locking_mode=exclusive;"
    "pragma foreign_keys=on;"
    "create table if not exists schema("
    "  version integer primary key);"
    "create table if not exists entropy("
    "  row_id integer primary key autoincrement,"
    "  seed   integer not null);"
    "create table if not exists runs("
    "  run_id integer primary key autoincrement,"
    "  time   text    not null default current_timestamp);"
    "create table if not exists files("
    "  file_id  integer primary key,"
    "  path     text    not null,"
    "  hash     text    not null,"
    "  modified integer not null);"
    "create unique index if not exists filenames on files(path);"
    "create table if not exists stats("
    "  stat_id    integer primary key autoincrement,"
    "  hashcode   integer not null," // on collision, prefer largest stat_id (ie: newest)
    "  status     integer not null,"
    "  runtime    real    not null,"
    "  cputime    real    not null,"
    "  membytes   integer not null,"
    "  ibytes     integer not null,"
    "  obytes     integer not null,"
    "  pathtime   real);"
    "create index if not exists stathash on stats(hashcode);"
    "create table if not exists jobs("
    "  job_id      integer primary key autoincrement,"
    "  run_id      integer not null references runs(run_id),"
    "  use_id      integer not null references runs(run_id),"
    "  label       text    not null,"
    "  directory   text    not null,"
    "  commandline blob    not null,"
    "  environment blob    not null,"
    "  stdin       text    not null," // might point outside the workspace
    "  signature   integer not null," // hash(FnInputs, FnOutputs, Resources, Keep)
    "  stack       blob    not null,"
    "  stat_id     integer references stats(stat_id)," // null if unmerged
    "  endtime     text    not null default '',"
    "  keep        integer not null default 0);"       // 0=false, 1=true
    "create index if not exists job on jobs(directory, commandline, environment, stdin, signature, keep, job_id, stat_id);"
    "create table if not exists filetree("
    "  tree_id  integer primary key autoincrement,"
    "  access   integer not null," // 0=visible, 1=input, 2=output
    "  job_id   integer not null references jobs(job_id) on delete cascade,"
    "  file_id  integer not null references files(file_id),"
    "  unique(job_id, access, file_id) on conflict ignore);"
    "create index if not exists filesearch on filetree(file_id, access, job_id);"
    "create table if not exists log("
    "  log_id     integer primary key autoincrement,"
    "  job_id     integer not null references jobs(job_id) on delete cascade,"
    "  descriptor integer not null," // 1=stdout, 2=stderr"
    "  seconds    real    not null," // seconds after job start
    "  output     text    not null);"
    "create index if not exists logorder on log(job_id, descriptor, log_id);"
    "create table if not exists tags("
    "  job_id     integer not null references jobs(job_id) on delete cascade,"
    "  uri        text,"
    "  unique(job_id, uri) on conflict ignore);";

  while (true) {
    char *fail;
    ret = sqlite3_exec(imp->db, schema_sql, 0, 0, &fail);
    if (ret == SQLITE_OK) {
      // Use an empty entropy table as a proxy for a new database (it gets filled automatically)
      const char *get_version = "select (select count(row_id) from entropy), (select max(version) from schema);";
      const char *set_version = "insert or ignore into schema(version) values(" SCHEMA_VERSION ");";
      ret = sqlite3_exec(imp->db, get_version, &schema_cb, 0, 0);
      if (ret == SQLITE_OK) {
        sqlite3_exec(imp->db, set_version, 0, 0, 0);
        break;
      } else {
        return "produced by an incompatible verison of wake; remove it.";
      }
    }

    std::string out = fail;
    sqlite3_free(fail);

    if (!wait || ret != SQLITE_BUSY) {
      close();
      return out;
    } else {
      std::cerr << "Database wake.db is busy; waiting 1 second ..." << std::endl;
      sleep(1);
    }
  }

  // prepare statements
  const char *sql_get_entropy = "select seed from entropy order by row_id";
  const char *sql_set_entropy = "insert into entropy(seed) values(?)";
  const char *sql_begin_txn = "begin transaction";
  const char *sql_commit_txn = "commit transaction";
  const char *sql_predict_job =
    "select status, runtime, cputime, membytes, ibytes, obytes, pathtime"
    " from stats where hashcode=? order by stat_id desc limit 1";
  const char *sql_stats_job =
    "select status, runtime, cputime, membytes, ibytes, obytes, pathtime"
    " from stats where stat_id=?";
  const char *sql_insert_job =
    "insert into jobs(run_id, use_id, label, directory, commandline, environment, stdin, signature, stack)"
    " values(?, ?1, ?, ?, ?, ?, ?, ?, ?)";
  const char *sql_insert_tree =
    "insert into filetree(access, job_id, file_id)"
    " values(?, ?, (select file_id from files where path=?))";
  const char *sql_insert_log =
    "insert into log(job_id, descriptor, seconds, output)"
    " values(?, ?, ?, ?)";
  const char *sql_wipe_file =
    "delete from jobs where job_id in"
    " (select t.job_id from files f, filetree t"
    "  where f.path=? and f.hash<>? and t.file_id=f.file_id and t.access=1)";
  const char *sql_insert_file =
    "insert or ignore into files(hash, modified, path) values (?, ?, ?)";
  const char *sql_update_file =
    "update files set hash=?, modified=? where path=?";
  const char *sql_get_log =
    "select output from log where job_id=? and descriptor=? order by log_id";
  const char *sql_replay_log =
    "select descriptor, output from log where job_id=? order by log_id";
  const char *sql_get_tree =
    "select f.path, f.hash from filetree t, files f"
    " where t.job_id=? and t.access=? and f.file_id=t.file_id order by t.tree_id";
  const char *sql_add_stats =
    "insert into stats(hashcode, status, runtime, cputime, membytes, ibytes, obytes)"
    " values(?, ?, ?, ?, ?, ?, ?)";
  const char *sql_link_stats =
    "update jobs set stat_id=?, endtime=current_timestamp, keep=? where job_id=?";
  const char *sql_detect_overlap =
    "select f.path from filetree t1, filetree t2, files f"
    " where t1.job_id=?1 and t1.access=2 and t2.file_id=t1.file_id and t2.access=2 and t2.job_id<>?1 and f.file_id=t1.file_id";
  const char *sql_delete_overlap =
    "delete from jobs where use_id<>? and job_id in "
    "(select t2.job_id from filetree t1, filetree t2"
    "  where t1.job_id=?2 and t1.access=2 and t2.file_id=t1.file_id and t2.access=2 and t2.job_id<>?2)";
  const char *sql_find_prior =
    "select job_id, stat_id from jobs where "
    "directory=? and commandline=? and environment=? and stdin=? and signature=? and keep=1";
  const char *sql_update_prior =
    "update jobs set use_id=? where job_id=?";
  const char *sql_delete_prior =
    "delete from jobs where use_id<>?1 and job_id in"
    " (select j2.job_id from jobs j1, jobs j2"
    "  where j1.job_id=?2 and j1.directory=j2.directory and j1.commandline=j2.commandline"
    "  and j1.environment=j2.environment and j1.stdin=j2.stdin and j2.job_id<>?2)";
  const char *sql_find_owner =
    "select j.job_id, j.label, j.directory, j.commandline, j.environment, j.stack, j.stdin, j.endtime, s.status, s.runtime, s.cputime, s.membytes, s.ibytes, s.obytes"
    " from files f, filetree t, jobs j left join stats s on j.stat_id=s.stat_id"
    " where f.path=? and t.file_id=f.file_id and t.access=? and j.job_id=t.job_id order by j.job_id";
  const char *sql_find_last =
    "select j.job_id, j.label, j.directory, j.commandline, j.environment, j.stack, j.stdin, j.endtime, s.status, s.runtime, s.cputime, s.membytes, s.ibytes, s.obytes"
    " from jobs j left join stats s on j.stat_id=s.stat_id"
    " where j.run_id==(select max(run_id) from jobs) and substr(cast(commandline as text),1,1) <> '<' order by j.job_id";
  const char *sql_find_failed =
    "select j.job_id, j.label, j.directory, j.commandline, j.environment, j.stack, j.stdin, j.endtime, s.status, s.runtime, s.cputime, s.membytes, s.ibytes, s.obytes"
    " from jobs j left join stats s on j.stat_id=s.stat_id"
    " where s.status<>0 order by j.job_id";
  const char *sql_fetch_hash =
    "select hash from files where path=? and modified=?";
  const char *sql_delete_jobs =
    "delete from jobs where job_id in"
    " (select job_id from jobs where keep=0 and use_id<>? except select job_id from filetree where access=2)";
  const char *sql_delete_dups =
    "delete from stats where stat_id in"
    " (select stat_id from (select hashcode, count(*) as num, max(stat_id) as keep from stats group by hashcode) d, stats s"
    "  where d.num>1 and s.hashcode=d.hashcode and s.stat_id<>d.keep except select stat_id from jobs)";
  const char *sql_delete_stats =
    "delete from stats where stat_id in"
    " (select stat_id from stats"
    "  where stat_id not in (select stat_id from jobs)"
    "  order by stat_id desc limit 9999999 offset 4*(select count(*) from jobs))";
  const char *sql_revtop_order =
    "select job_id from jobs where use_id=(select max(run_id) from runs) order by job_id desc;";
  const char *sql_setcrit_path =
    "update stats set pathtime=runtime+("
    "  select coalesce(max(s.pathtime),0) from filetree f1, filetree f2, jobs j, stats s"
    "  where f1.job_id=?1 and f1.access=2 and f1.file_id=f2.file_id and f2.access=1 and f2.job_id=j.job_id and j.stat_id=s.stat_id"
    ") where stat_id=(select stat_id from jobs where job_id=?1);";
  const char *sql_tag_job =
    "insert into tags(job_id, uri) values(?, ?)";

#define PREPARE(sql, member)										\
  ret = sqlite3_prepare_v2(imp->db, sql, -1, &imp->member, 0);						\
  if (ret != SQLITE_OK) {										\
    std::string out = std::string("sqlite3_prepare_v2 " #member ": ") + sqlite3_errmsg(imp->db);	\
    close();												\
    return out;												\
  }

  PREPARE(sql_get_entropy,    get_entropy);
  PREPARE(sql_set_entropy,    set_entropy);
  PREPARE(sql_begin_txn,      begin_txn);
  PREPARE(sql_commit_txn,     commit_txn);
  PREPARE(sql_predict_job,    predict_job);
  PREPARE(sql_stats_job,      stats_job);
  PREPARE(sql_insert_job,     insert_job);
  PREPARE(sql_insert_tree,    insert_tree);
  PREPARE(sql_insert_log,     insert_log);
  PREPARE(sql_wipe_file,      wipe_file);
  PREPARE(sql_insert_file,    insert_file);
  PREPARE(sql_update_file,    update_file);
  PREPARE(sql_get_log,        get_log);
  PREPARE(sql_replay_log,     replay_log);
  PREPARE(sql_get_tree,       get_tree);
  PREPARE(sql_add_stats,      add_stats);
  PREPARE(sql_link_stats,     link_stats);
  PREPARE(sql_detect_overlap, detect_overlap);
  PREPARE(sql_delete_overlap, delete_overlap);
  PREPARE(sql_find_prior,     find_prior);
  PREPARE(sql_update_prior,   update_prior);
  PREPARE(sql_delete_prior,   delete_prior);
  PREPARE(sql_find_owner,     find_owner);
  PREPARE(sql_find_last,      find_last);
  PREPARE(sql_find_failed,    find_failed);
  PREPARE(sql_fetch_hash,     fetch_hash);
  PREPARE(sql_delete_jobs,    delete_jobs);
  PREPARE(sql_delete_dups,    delete_dups);
  PREPARE(sql_delete_stats,   delete_stats);
  PREPARE(sql_revtop_order,   revtop_order);
  PREPARE(sql_setcrit_path,   setcrit_path);
  PREPARE(sql_tag_job,        tag_job);

  return "";
}

void Database::close() {
  int ret;

#define FINALIZE(member)						\
  if  (imp->member) {							\
    ret = sqlite3_finalize(imp->member);				\
    if (ret != SQLITE_OK) {						\
      std::cerr << "Could not sqlite3_finalize " << #member		\
                << ": " << sqlite3_errmsg(imp->db) << std::endl;	\
      return;								\
    }									\
  }									\
  imp->member = 0;

  FINALIZE(get_entropy);
  FINALIZE(set_entropy);
  FINALIZE(begin_txn);
  FINALIZE(commit_txn);
  FINALIZE(predict_job);
  FINALIZE(stats_job);
  FINALIZE(insert_job);
  FINALIZE(insert_tree);
  FINALIZE(insert_log);
  FINALIZE(wipe_file);
  FINALIZE(insert_file);
  FINALIZE(update_file);
  FINALIZE(get_log);
  FINALIZE(replay_log);
  FINALIZE(get_tree);
  FINALIZE(add_stats);
  FINALIZE(link_stats);
  FINALIZE(detect_overlap);
  FINALIZE(delete_overlap);
  FINALIZE(find_prior);
  FINALIZE(update_prior);
  FINALIZE(delete_prior);
  FINALIZE(find_owner);
  FINALIZE(find_last);
  FINALIZE(find_failed);
  FINALIZE(fetch_hash);
  FINALIZE(delete_jobs);
  FINALIZE(delete_dups);
  FINALIZE(delete_stats);
  FINALIZE(revtop_order);
  FINALIZE(setcrit_path);
  FINALIZE(tag_job);

  if (imp->db) {
    int ret = sqlite3_close(imp->db);
    if (ret != SQLITE_OK) {
      std::cerr << "Could not close wake.db: " << sqlite3_errmsg(imp->db) << std::endl;
      return;
    }
  }
  imp->db = 0;
}

static void finish_stmt(const char *why, sqlite3_stmt *stmt, bool debug) {
  int ret;

  if (debug) {
    std::stringstream s;
    s << "DB:: ";
#if SQLITE_VERSION_NUMBER >= 3014000
    char *tmp = sqlite3_expanded_sql(stmt);
    s << tmp;
    sqlite3_free(tmp);
#else
    s << sqlite3_sql(stmt);
#endif
    s << std::endl;
    std::string out = s.str();
    status_write(2, out.data(), out.size());
  }

  ret = sqlite3_reset(stmt);
  if (ret != SQLITE_OK) {
    std::cerr << why << "; sqlite3_reset: " << sqlite3_errmsg(sqlite3_db_handle(stmt)) << std::endl;
    exit(1);
  }

  ret = sqlite3_clear_bindings(stmt);
  if (ret != SQLITE_OK) {
    std::cerr << why << "; sqlite3_clear_bindings: " << sqlite3_errmsg(sqlite3_db_handle(stmt)) << std::endl;
    exit(1);
  }
}

static void single_step(const char *why, sqlite3_stmt *stmt, bool debug) {
  int ret;

  ret = sqlite3_step(stmt);
  if (ret != SQLITE_DONE) {
    std::cerr << why << "; sqlite3_step: " << sqlite3_errmsg(sqlite3_db_handle(stmt)) << std::endl;
    std::cerr << "The failing statement was: ";
#if SQLITE_VERSION_NUMBER >= 3014000
    char *tmp = sqlite3_expanded_sql(stmt);
    std::cerr << tmp;
    sqlite3_free(tmp);
#else
    std::cerr << sqlite3_sql(stmt);
#endif
    std::cerr << std::endl;
    exit(1);
  }

  finish_stmt(why, stmt, debug);
}

static void bind_blob(const char *why, sqlite3_stmt *stmt, int index, const char *str, size_t len) {
  int ret;
  ret = sqlite3_bind_blob(stmt, index, str, len, SQLITE_STATIC);
  if (ret != SQLITE_OK) {
    std::cerr << why << "; sqlite3_bind_blob(" << index << "): " << sqlite3_errmsg(sqlite3_db_handle(stmt)) << std::endl;
    exit(1);
  }
}

static void bind_blob(const char *why, sqlite3_stmt *stmt, int index, const std::string &x) {
  bind_blob(why, stmt, index, x.data(), x.size());
}

static void bind_string(const char *why, sqlite3_stmt *stmt, int index, const char *str, size_t len) {
  int ret;
  ret = sqlite3_bind_text(stmt, index, str, len, SQLITE_STATIC);
  if (ret != SQLITE_OK) {
    std::cerr << why << "; sqlite3_bind_text(" << index << "): " << sqlite3_errmsg(sqlite3_db_handle(stmt)) << std::endl;
    exit(1);
  }
}

static void bind_string(const char *why, sqlite3_stmt *stmt, int index, const std::string &x) {
  bind_string(why, stmt, index, x.data(), x.size());
}

static void bind_integer(const char *why, sqlite3_stmt *stmt, int index, long x) {
  int ret;
  ret = sqlite3_bind_int64(stmt, index, x);
  if (ret != SQLITE_OK) {
    std::cerr << why << "; sqlite3_bind_int64(" << index << "): " << sqlite3_errmsg(sqlite3_db_handle(stmt)) << std::endl;
    exit(1);
  }
}

static void bind_double(const char *why, sqlite3_stmt *stmt, int index, double x) {
  int ret;
  ret = sqlite3_bind_double(stmt, index, x);
  if (ret != SQLITE_OK) {
    std::cerr << why << "; sqlite3_bind_double(" << index << "): " << sqlite3_errmsg(sqlite3_db_handle(stmt)) << std::endl;
    exit(1);
  }
}

static std::string rip_column(sqlite3_stmt *stmt, int col) {
  return std::string(
    static_cast<const char*>(sqlite3_column_blob(stmt, col)),
    sqlite3_column_bytes(stmt, col));
}

void Database::entropy(uint64_t *key, int words) {
  const char *why = "Could not restore entropy";
  int word;

  begin_txn();

  // Use entropy from DB
  for (word = 0; word < words; ++word) {
    if (sqlite3_step(imp->get_entropy) != SQLITE_ROW) break;
    key[word] = sqlite3_column_int64(imp->get_entropy, 0);
  }
  finish_stmt(why, imp->get_entropy, imp->debugdb);

  // Save any additional entropy needed
  for (; word < words; ++word) {
    bind_integer(why, imp->set_entropy, 1, key[word]);
    single_step (why, imp->set_entropy, imp->debugdb);
  }

  end_txn();
}

void Database::prepare() {
  std::vector<std::string> out;
  const char *sql = "insert into runs(run_id) values(null);";
  int ret = sqlite3_exec(imp->db, sql, 0, 0, 0);
  if (ret != SQLITE_OK) {
    std::cerr << "Could not insert run: " << sqlite3_errmsg(imp->db) << std::endl;
    exit(1);
  }
  imp->run_id = sqlite3_last_insert_rowid(imp->db);
}

void Database::clean() {
  const char *why = "Could not compute critical path";
  begin_txn();
  while (sqlite3_step(imp->revtop_order) == SQLITE_ROW) {
    bind_integer(why, imp->setcrit_path, 1, sqlite3_column_int64(imp->revtop_order, 0));
    single_step(why, imp->setcrit_path, imp->debugdb);
  }
  finish_stmt(why, imp->revtop_order, imp->debugdb);
  end_txn();

  bind_integer(why, imp->delete_jobs, 1, imp->run_id);
  single_step("Could not clean database jobs",  imp->delete_jobs,  imp->debugdb);
  single_step("Could not clean database dups",  imp->delete_dups,  imp->debugdb);
  single_step("Could not clean database stats", imp->delete_stats, imp->debugdb);

  // This cannot be a prepared statement, because pragmas may run on prepare
  char *fail;
  int ret = sqlite3_exec(imp->db, "pragma incremental_vacuum;", 0, 0, &fail);
  if (ret != SQLITE_OK)
    std::cerr << "Could not recover space: " << fail << std::endl;
}

void Database::begin_txn() {
  single_step("Could not begin a transaction", imp->begin_txn, imp->debugdb);
}

void Database::end_txn() {
  single_step("Could not commit a transaction", imp->commit_txn, imp->debugdb);
}

// This function needs to be able to run twice in succession and return the same results
// ... because heap allocations are created to hold the file list output by this function.
// Fortunately, updating use_id is the only side-effect and it does not affect reuse_job.
Usage Database::reuse_job(
  const std::string &directory,
  const std::string &environment,
  const std::string &commandline,
  const std::string &stdin_file,
  uint64_t           signature,
  const std::string &visible,
  bool check,
  long &job,
  std::vector<FileReflection> &files,
  double *pathtime)
{
  Usage out;
  long stat_id;

  // When implementing indexed directories, beware of non-existent BADPATH files

  const char *why = "Could not check for a cached job";
  begin_txn();
  bind_string (why, imp->find_prior, 1, directory);
  bind_blob   (why, imp->find_prior, 2, commandline);
  bind_blob   (why, imp->find_prior, 3, environment);
  bind_string (why, imp->find_prior, 4, stdin_file);
  bind_integer(why, imp->find_prior, 5, signature);
  out.found = sqlite3_step(imp->find_prior) == SQLITE_ROW;
  if (out.found) {
    job     = sqlite3_column_int64(imp->find_prior, 0);
    stat_id = sqlite3_column_int64(imp->find_prior, 1);
  }
  finish_stmt (why, imp->find_prior, imp->debugdb);

  if (!out.found) {
    end_txn();
    return out;
  }

  bind_integer(why, imp->stats_job, 1, stat_id);
  if (sqlite3_step(imp->stats_job) == SQLITE_ROW) {
    out.status   = sqlite3_column_int64 (imp->stats_job, 0);
    out.runtime  = sqlite3_column_double(imp->stats_job, 1);
    out.cputime  = sqlite3_column_double(imp->stats_job, 2);
    out.membytes = sqlite3_column_int64 (imp->stats_job, 3);
    out.ibytes   = sqlite3_column_int64 (imp->stats_job, 4);
    out.obytes   = sqlite3_column_int64 (imp->stats_job, 5);
    *pathtime    = sqlite3_column_double(imp->stats_job, 6);
  } else {
    out.found = false;
  }
  finish_stmt(why, imp->stats_job, imp->debugdb);

  // Create a hash table of visible files
  std::unordered_set<std::string> vis;
  const char *tok = visible.c_str();
  const char *end = tok + visible.size();
  for (const char *scan = tok; scan != end; ++scan) {
    if (*scan == 0 && scan != tok) {
      vis.emplace(tok, scan-tok);
      tok = scan+1;
    }
  }

  // Confirm all inputs are still visible
  bind_integer(why, imp->get_tree, 1, job);
  bind_integer(why, imp->get_tree, 2, INPUT);
  while (sqlite3_step(imp->get_tree) == SQLITE_ROW) {
    if (vis.find(rip_column(imp->get_tree, 0)) == vis.end()) out.found = false;
  }
  finish_stmt(why, imp->get_tree, imp->debugdb);

  // Confirm all outputs still exist, and report their old hashes
  bind_integer(why, imp->get_tree, 1, job);
  bind_integer(why, imp->get_tree, 2, OUTPUT);
  while (sqlite3_step(imp->get_tree) == SQLITE_ROW) {
    std::string path = rip_column(imp->get_tree, 0);
    if (access(path.c_str(), R_OK) != 0) out.found = false;
    files.emplace_back(std::move(path), rip_column(imp->get_tree, 1));
  }
  finish_stmt(why, imp->get_tree, imp->debugdb);

  // If we need to rerun the job (outputs don't exist), wipe the files-to-check list
  if (!out.found) {
    files.clear();
  }

  if (out.found && !check) {
    bind_integer(why, imp->update_prior, 1, imp->run_id);
    bind_integer(why, imp->update_prior, 2, job);
    single_step (why, imp->update_prior, imp->debugdb);
  }

  end_txn();

  return out;
}

Usage Database::predict_job(uint64_t hashcode, double *pathtime)
{
  Usage out;
  const char *why = "Could not predict a job";
  bind_integer(why, imp->predict_job, 1, hashcode);
  if (sqlite3_step(imp->predict_job) == SQLITE_ROW) {
    out.found    = true;
    out.status   = sqlite3_column_int   (imp->predict_job, 0);
    out.runtime  = sqlite3_column_double(imp->predict_job, 1);
    out.cputime  = sqlite3_column_double(imp->predict_job, 2);
    out.membytes = sqlite3_column_int64 (imp->predict_job, 3);
    out.ibytes   = sqlite3_column_int64 (imp->predict_job, 4);
    out.obytes   = sqlite3_column_int64 (imp->predict_job, 5);
    *pathtime    = sqlite3_column_double(imp->predict_job, 6);
  } else {
    out.found    = false;
    out.status   = 0;
    out.runtime  = 0;
    out.cputime  = 0;
    out.membytes = 0;
    out.ibytes   = 0;
    out.obytes   = 0;
    *pathtime    = 0;
  }
  finish_stmt(why, imp->predict_job, imp->debugdb);
  return out;
}

void Database::insert_job(
  const std::string &directory,
  const std::string &commandline,
  const std::string &environment,
  const std::string &stdin_file,
  uint64_t          signature,
  const std::string &label,
  const std::string &stack,
  const std::string &visible,
  long  *job)
{
  const char *why = "Could not insert a job";
  begin_txn();
  bind_integer(why, imp->insert_job, 1, imp->run_id);
  bind_string (why, imp->insert_job, 2, label);
  bind_string (why, imp->insert_job, 3, directory);
  bind_blob   (why, imp->insert_job, 4, commandline);
  bind_blob   (why, imp->insert_job, 5, environment);
  bind_string (why, imp->insert_job, 6, stdin_file);
  bind_integer(why, imp->insert_job, 7, signature);
  bind_blob   (why, imp->insert_job, 8, stack);
  single_step (why, imp->insert_job, imp->debugdb);
  *job = sqlite3_last_insert_rowid(imp->db);
  const char *tok = visible.c_str();
  const char *end = tok + visible.size();
  for (const char *scan = tok; scan != end; ++scan) {
    if (*scan == 0 && scan != tok) {
      bind_integer(why, imp->insert_tree, 1, VISIBLE);
      bind_integer(why, imp->insert_tree, 2, *job);
      bind_string (why, imp->insert_tree, 3, tok, scan-tok);
      single_step (why, imp->insert_tree, imp->debugdb);
      tok = scan+1;
    }
  }
  end_txn();
}

void Database::finish_job(long job, const std::string &inputs, const std::string &outputs, uint64_t hashcode, bool keep, Usage reality) {
  const char *why = "Could not save job inputs and outputs";
  begin_txn();
  bind_integer(why, imp->add_stats, 1, hashcode);
  bind_integer(why, imp->add_stats, 2, reality.status);
  bind_double (why, imp->add_stats, 3, reality.runtime);
  bind_double (why, imp->add_stats, 4, reality.cputime);
  bind_integer(why, imp->add_stats, 5, reality.membytes);
  bind_integer(why, imp->add_stats, 6, reality.ibytes);
  bind_integer(why, imp->add_stats, 7, reality.obytes);
  single_step (why, imp->add_stats, imp->debugdb);
  bind_integer(why, imp->link_stats, 1, sqlite3_last_insert_rowid(imp->db));
  bind_integer(why, imp->link_stats, 2, keep?1:0);
  bind_integer(why, imp->link_stats, 3, job);
  single_step (why, imp->link_stats, imp->debugdb);
  // Grab the visible set
  std::set<std::string> visible;
  bind_integer(why, imp->get_tree, 1, job);
  bind_integer(why, imp->get_tree, 2, VISIBLE);
  while (sqlite3_step(imp->get_tree) == SQLITE_ROW)
    visible.insert(rip_column(imp->get_tree, 0));
  finish_stmt(why, imp->get_tree, imp->debugdb);
  // Insert inputs, confirming they are visible
  const char *tok = inputs.c_str();
  const char *end = tok + inputs.size();
  for (const char *scan = tok; scan != end; ++scan) {
    if (*scan == 0 && scan != tok) {
      std::string input(tok, scan-tok);
      if (visible.find(input) == visible.end()) {
        std::stringstream s;
        s << "Job " << job
          << " erroneously added input '" << input
          << "' which was not a visible file." << std::endl;
        std::string out = s.str();
        status_write(2, out.data(), out.size());
      } else {
        bind_integer(why, imp->insert_tree, 1, INPUT);
        bind_integer(why, imp->insert_tree, 2, job);
        bind_string (why, imp->insert_tree, 3, tok, scan-tok);
        single_step (why, imp->insert_tree, imp->debugdb);
      }
      tok = scan+1;
    }
  }
  // Insert outputs
  tok = outputs.c_str();
  end = tok + outputs.size();
  for (const char *scan = tok; scan != end; ++scan) {
    if (*scan == 0 && scan != tok) {
      bind_integer(why, imp->insert_tree, 1, OUTPUT);
      bind_integer(why, imp->insert_tree, 2, job);
      bind_string (why, imp->insert_tree, 3, tok, scan-tok);
      single_step (why, imp->insert_tree, imp->debugdb);
      tok = scan+1;
    }
  }

  bind_integer(why, imp->delete_prior, 1, imp->run_id);
  bind_integer(why, imp->delete_prior, 2, job);
  single_step (why, imp->delete_prior, imp->debugdb);

  bind_integer(why, imp->delete_overlap, 1, imp->run_id);
  bind_integer(why, imp->delete_overlap, 2, job);
  single_step (why, imp->delete_overlap, imp->debugdb);

  bool fail = false;
  bind_integer(why, imp->detect_overlap, 1, job);
  while (sqlite3_step(imp->detect_overlap) == SQLITE_ROW) {
    std::stringstream s;
    s << "File output by multiple Jobs: " << rip_column(imp->detect_overlap, 0) << std::endl;
    std::string out = s.str();
    status_write(2, out.data(), out.size());
    fail = true;
  }
  finish_stmt(why, imp->detect_overlap, imp->debugdb);

  end_txn();

  if (fail) exit(1);
}

void Database::tag_job(long job, const std::string &uri) {
  const char *why = "Could not tag a job";
  bind_integer(why, imp->tag_job, 1, job);
  bind_string (why, imp->tag_job, 2, uri);
  single_step (why, imp->tag_job, imp->debugdb);
}

std::vector<FileReflection> Database::get_tree(int kind, long job)  {
  std::vector<FileReflection> out;
  const char *why = "Could not read job tree";
  bind_integer(why, imp->get_tree, 1, job);
  bind_integer(why, imp->get_tree, 2, kind);
  while (sqlite3_step(imp->get_tree) == SQLITE_ROW)
    out.emplace_back(rip_column(imp->get_tree, 0), rip_column(imp->get_tree, 1));
  finish_stmt(why, imp->get_tree, imp->debugdb);
  return out;
}

void Database::save_output(long job, int descriptor, const char *buffer, int size, double runtime) {
  const char *why = "Could not save job output";
  bind_integer(why, imp->insert_log, 1, job);
  bind_integer(why, imp->insert_log, 2, descriptor);
  bind_double (why, imp->insert_log, 3, runtime);
  bind_string (why, imp->insert_log, 4, buffer, size);
  single_step (why, imp->insert_log, imp->debugdb);
}

std::string Database::get_output(long job, int descriptor) {
  std::stringstream out;
  const char *why = "Could not read job output";
  bind_integer(why, imp->get_log, 1, job);
  bind_integer(why, imp->get_log, 2, descriptor);
  while (sqlite3_step(imp->get_log) == SQLITE_ROW) {
    out.write(
      static_cast<const char*>(sqlite3_column_blob(imp->get_log, 0)),
      sqlite3_column_bytes(imp->get_log, 0));
  }
  finish_stmt(why, imp->get_log, imp->debugdb);
  return out.str();
}

void Database::replay_output(long job, bool dump_stdout, bool dump_stderr) {
  if (!dump_stdout && !dump_stderr) return;
  const char *why = "Could not replay job output";
  bind_integer(why, imp->replay_log, 1, job);
  bool lf = false;
  while (sqlite3_step(imp->replay_log) == SQLITE_ROW) {
    int fd = sqlite3_column_int64(imp->replay_log, 0);
    const char *str = static_cast<const char*>(sqlite3_column_blob(imp->replay_log, 1));
    int len = sqlite3_column_bytes(imp->replay_log, 1);
    if (len > 0 && ((fd == 2 && dump_stderr) || (fd == 1 && dump_stdout))) {
      status_write(fd, str, len);
      lf = str[len-1] != '\n';
    }
  }
  finish_stmt(why, imp->replay_log, imp->debugdb);
  if (lf) status_write(1, "\n", 1);
}

void Database::add_hash(const std::string &file, const std::string &hash, long modified) {
  const char *why = "Could not insert a hash";
  begin_txn();
  bind_string (why, imp->wipe_file, 1, file);
  bind_string (why, imp->wipe_file, 2, hash);
  single_step (why, imp->wipe_file, imp->debugdb);
  bind_string (why, imp->update_file, 1, hash);
  bind_integer(why, imp->update_file, 2, modified);
  bind_string (why, imp->update_file, 3, file);
  single_step (why, imp->update_file, imp->debugdb);
  bind_string (why, imp->insert_file, 1, hash);
  bind_integer(why, imp->insert_file, 2, modified);
  bind_string (why, imp->insert_file, 3, file);
  single_step (why, imp->insert_file, imp->debugdb);
  end_txn();
}

std::string Database::get_hash(const std::string &file, long modified) {
  std::string out;
  const char *why = "Could not fetch a hash";
  bind_string (why, imp->fetch_hash, 1, file);
  bind_integer(why, imp->fetch_hash, 2, modified);
  if (sqlite3_step(imp->fetch_hash) == SQLITE_ROW)
    out = rip_column(imp->fetch_hash, 0);
  finish_stmt(why, imp->fetch_hash, imp->debugdb);
  return out;
}

static std::vector<std::string> chop_null(const std::string &str) {
  std::vector<std::string> out;
  const char *tok = str.c_str();
  const char *end = tok + str.size();
  for (const char *scan = tok; scan != end; ++scan) {
    if (*scan == 0 && scan != tok) {
      out.emplace_back(tok, scan-tok);
      tok = scan+1;
    }
  }
  return out;
}

static std::vector<JobReflection> find_all(Database *db, sqlite3_stmt *query, bool verbose) {
  const char *why = "Could not explain file";
  std::vector<JobReflection> out;

  db->begin_txn();
  while (sqlite3_step(query) == SQLITE_ROW) {
    out.resize(out.size()+1);
    JobReflection &desc = out.back();
    desc.job            = sqlite3_column_int64(query, 0);
    desc.label          = rip_column(query, 1);
    desc.directory      = rip_column(query, 2);
    desc.commandline    = chop_null(rip_column(query, 3));
    desc.environment    = chop_null(rip_column(query, 4));
    desc.stack          = rip_column(query, 5);
    desc.stdin_file     = rip_column(query, 6);
    desc.time           = rip_column(query, 7);
    desc.usage.status   = sqlite3_column_int64 (query, 8);
    desc.usage.runtime  = sqlite3_column_double(query, 9);
    desc.usage.cputime  = sqlite3_column_double(query, 10);
    desc.usage.membytes = sqlite3_column_int64 (query, 11);
    desc.usage.ibytes   = sqlite3_column_int64 (query, 12);
    desc.usage.obytes   = sqlite3_column_int64 (query, 13);
    if (desc.stdin_file.empty()) desc.stdin_file = "/dev/null";
    if (verbose) {
      desc.stdout_payload = db->get_output(desc.job, 1);
      desc.stderr_payload = db->get_output(desc.job, 2);
      // visible
      bind_integer(why, db->imp->get_tree, 1, desc.job);
      bind_integer(why, db->imp->get_tree, 2, VISIBLE);
      while (sqlite3_step(db->imp->get_tree) == SQLITE_ROW)
        desc.visible.emplace_back(
          rip_column(db->imp->get_tree, 0),
          rip_column(db->imp->get_tree, 1));
      finish_stmt(why, db->imp->get_tree, db->imp->debugdb);
    }
    // inputs
    bind_integer(why, db->imp->get_tree, 1, desc.job);
    bind_integer(why, db->imp->get_tree, 2, INPUT);
    while (sqlite3_step(db->imp->get_tree) == SQLITE_ROW)
      desc.inputs.emplace_back(
        rip_column(db->imp->get_tree, 0),
        rip_column(db->imp->get_tree, 1));
    finish_stmt(why, db->imp->get_tree, db->imp->debugdb);
    // outputs
    bind_integer(why, db->imp->get_tree, 1, desc.job);
    bind_integer(why, db->imp->get_tree, 2, OUTPUT);
    while (sqlite3_step(db->imp->get_tree) == SQLITE_ROW)
      desc.outputs.emplace_back(
        rip_column(db->imp->get_tree, 0),
        rip_column(db->imp->get_tree, 1));
    finish_stmt(why, db->imp->get_tree, db->imp->debugdb);
  }
  finish_stmt(why, query, db->imp->debugdb);
  db->end_txn();

  return out;
}

std::vector<JobReflection> Database::failed(bool verbose) {
  return find_all(this, imp->find_failed, verbose);
}

std::vector<JobReflection> Database::last(bool verbose) {
  return find_all(this, imp->find_last, verbose);
}

std::vector<JobReflection> Database::explain(const std::string &file, int use, bool verbose) {
  const char *why = "Could not bind args";
  bind_string (why, imp->find_owner, 1, file);
  bind_integer(why, imp->find_owner, 2, use);
  return find_all(this, imp->find_owner, verbose);
}
