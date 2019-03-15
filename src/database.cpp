#include "database.h"
#include "status.h"
#include <iostream>
#include <sstream>
#include <sqlite3.h>
#include <unistd.h>
#include <cstdlib>

#define VISIBLE 0
#define INPUT 1
#define OUTPUT 2
#define INDEXES 3

struct Database::detail {
  bool debugdb;
  sqlite3 *db;
  sqlite3_stmt *get_entropy;
  sqlite3_stmt *set_entropy;
  sqlite3_stmt *add_target;
  sqlite3_stmt *del_target;
  sqlite3_stmt *begin_txn;
  sqlite3_stmt *commit_txn;
  sqlite3_stmt *insert_job;
  sqlite3_stmt *insert_tree;
  sqlite3_stmt *insert_log;
  sqlite3_stmt *wipe_file;
  sqlite3_stmt *insert_file;
  sqlite3_stmt *update_file;
  sqlite3_stmt *get_log;
  sqlite3_stmt *get_tree;
  sqlite3_stmt *add_stats;
  sqlite3_stmt *link_stats;
  sqlite3_stmt *detect_overlap;
  sqlite3_stmt *delete_overlap;
  sqlite3_stmt *find_prior;
  sqlite3_stmt *update_prior;
  sqlite3_stmt *find_owner;
  sqlite3_stmt *fetch_hash;
  sqlite3_stmt *delete_jobs;
  sqlite3_stmt *delete_stats;
  long run_id;
  detail(bool debugdb_)
   : debugdb(debugdb_), db(0), get_entropy(0), set_entropy(0), add_target(0), del_target(0), begin_txn(0),
     commit_txn(0), insert_job(0), insert_tree(0), insert_log(0), wipe_file(0), insert_file(0), update_file(0),
     get_log(0), get_tree(0), add_stats(0), link_stats(0), detect_overlap(0), delete_overlap(0), find_prior(0),
     update_prior(0), find_owner(0), fetch_hash(0), delete_jobs(0), delete_stats(0) { }
};

Database::Database(bool debugdb) : imp(new detail(debugdb)) { }
Database::~Database() { close(); }

std::string Database::open(bool wait) {
  if (imp->db) return "";
  int ret;

  ret = sqlite3_open_v2("wake.db", &imp->db, SQLITE_OPEN_READWRITE, 0);
  if (ret != SQLITE_OK) {
    if (!imp->db) return "sqlite3_open: out of memory";
    std::string out = sqlite3_errmsg(imp->db);
    close();
    return out;
  }

  const char *schema_sql =
    "pragma journal_mode=wal;"
    "pragma synchronous=0;"
    "pragma locking_mode=exclusive;"
    "pragma foreign_keys=on;"
    "create table if not exists targets("
    "  expression text primary key);"
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
    "create table if not exists stats(" // how to clean this? (stats \ linked stats) limit 9999999 offset 4*#jobs
    "  stat_id    integer primary key autoincrement,"
    "  hashcode   integer not null," // on collision, prefer largest stat_id (ie: newest)
    "  status     integer not null,"
    "  runtime    real    not null,"
    "  cputime    real    not null,"
    "  membytes   integer not null,"
    "  iobytes    integer not null);"
    "create index if not exists stathash on stats(hashcode);"
    "create table if not exists jobs("
    "  job_id      integer primary key,"
    "  run_id      integer not null references runs(run_id),"
    "  use_id      integer not null references runs(run_id),"
    "  directory   text    not null,"
    "  commandline blob    not null,"
    "  environment blob    not null,"
    "  stack       blob    not null,"
    "  stdin       text    not null," // might point outside the workspace
    "  stat_id     integer references stats(stat_id)," // null if unmerged
    "  endtime     text    not null default '',"
    "  keep        integer not null default 0);"       // 0=false, 1=true
    "create index if not exists job on jobs(directory, commandline, environment, stdin);"
    "create table if not exists filetree("
    "  access  integer not null," // 0=visible, 1=input, 2=output, 3=indexes
    "  job_id  integer not null references jobs(job_id) on delete cascade,"
    "  file_id integer not null references files(file_id),"
    "  primary key(job_id, access, file_id) on conflict ignore);"
    "create index if not exists filesearch on filetree(file_id, access);"
    "create table if not exists log("
    "  log_id     integer primary key autoincrement,"
    "  job_id     integer not null references jobs(job_id) on delete cascade,"
    "  descriptor integer not null," // 1=stdout, 2=stderr"
    "  seconds    real    not null," // seconds after job start
    "  output     text    not null);"
    "create index if not exists logorder on log(job_id, descriptor, log_id);";

  while (true) {
    char *fail;
    ret = sqlite3_exec(imp->db, schema_sql, 0, 0, &fail);
    if (ret == SQLITE_OK) break;

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
  const char *sql_add_target = "insert into targets(expression) values(?)";
  const char *sql_del_target = "delete from targets where expression=?";
  const char *sql_begin_txn = "begin transaction";
  const char *sql_commit_txn = "commit transaction";
  const char *sql_insert_job =
    "insert into jobs(run_id, use_id, directory, commandline, environment, stack, stdin)"
    " values(?, ?1, ?, ?, ?, ?, ?)";
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
  const char *sql_get_tree =
    "select f.path, f.hash from filetree t, files f"
    " where t.job_id=? and t.access=? and f.file_id=t.file_id";
  const char *sql_add_stats =
    "insert into stats(hashcode, status, runtime, cputime, membytes, iobytes)"
    " values(?, ?, ?, ?, ?, ?)";
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
    "select job_id from jobs where "
    "directory=? and commandline=? and environment=? and stdin=? and keep=1";
  const char *sql_update_prior =
    "update jobs set use_id=? where job_id=?";
  const char *sql_find_owner =
    "select j.job_id, j.directory, j.commandline, j.environment, j.stack, j.stdin, j.endtime, s.status, s.runtime"
    " from files f, filetree t, jobs j left join stats s on j.stat_id=s.stat_id"
    " where f.path=? and t.file_id=f.file_id and t.access=? and j.job_id=t.job_id";
  const char *sql_fetch_hash =
    "select hash from files where path=? and modified=?";
  const char *sql_delete_jobs =
    "delete from jobs where job_id in"
    " (select job_id from jobs where keep=0 except select job_id from filetree where access=2)";
  // !!! delete duplicate hashcode stats
  const char *sql_delete_stats =
    "delete from stats where stat_id in"
    " (select stat_id from stats except select stat_id from jobs)"
    " order by stat_id desc limit 9999999 offset 4*(select count(*) from jobs)";

#define PREPARE(sql, member)										\
  ret = sqlite3_prepare_v2(imp->db, sql, -1, &imp->member, 0);						\
  if (ret != SQLITE_OK) {										\
    std::string out = std::string("sqlite3_prepare_v2 " #member ": ") + sqlite3_errmsg(imp->db);	\
    close();												\
    return out;												\
  }

  PREPARE(sql_get_entropy,    get_entropy);
  PREPARE(sql_set_entropy,    set_entropy);
  PREPARE(sql_add_target,     add_target);
  PREPARE(sql_del_target,     del_target);
  PREPARE(sql_begin_txn,      begin_txn);
  PREPARE(sql_commit_txn,     commit_txn);
  PREPARE(sql_insert_job,     insert_job);
  PREPARE(sql_insert_tree,    insert_tree);
  PREPARE(sql_insert_log,     insert_log);
  PREPARE(sql_wipe_file,      wipe_file);
  PREPARE(sql_insert_file,    insert_file);
  PREPARE(sql_update_file,    update_file);
  PREPARE(sql_get_log,        get_log);
  PREPARE(sql_get_tree,       get_tree);
  PREPARE(sql_add_stats,      add_stats);
  PREPARE(sql_link_stats,     link_stats);
  PREPARE(sql_detect_overlap, detect_overlap);
  PREPARE(sql_delete_overlap, delete_overlap);
  PREPARE(sql_find_prior,     find_prior);
  PREPARE(sql_update_prior,   update_prior);
  PREPARE(sql_find_owner,     find_owner);
  PREPARE(sql_fetch_hash,     fetch_hash);
  PREPARE(sql_delete_jobs,    delete_jobs);
  PREPARE(sql_delete_stats,   delete_stats);

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
  FINALIZE(add_target);
  FINALIZE(del_target);
  FINALIZE(begin_txn);
  FINALIZE(commit_txn);
  FINALIZE(insert_job);
  FINALIZE(insert_tree);
  FINALIZE(insert_log);
  FINALIZE(wipe_file);
  FINALIZE(insert_file);
  FINALIZE(update_file);
  FINALIZE(get_log);
  FINALIZE(get_tree);
  FINALIZE(add_stats);
  FINALIZE(link_stats);
  FINALIZE(detect_overlap);
  FINALIZE(delete_overlap);
  FINALIZE(find_prior);
  FINALIZE(update_prior);
  FINALIZE(find_owner);
  FINALIZE(fetch_hash);
  FINALIZE(delete_jobs);
  FINALIZE(delete_stats);

  if (imp->db) {
    int ret = sqlite3_close(imp->db);
    if (ret != SQLITE_OK) {
      std::cerr << "Could not close wake.db: " << sqlite3_errmsg(imp->db) << std::endl;
      return;
    }
  }
  imp->db = 0;
}

static int fill_vector(void *data, int cols, char **text, char **colname) {
  (void)colname;
  if (cols >= 1) {
    std::vector<std::string> *vec = reinterpret_cast<std::vector<std::string>*>(data);
    vec->emplace_back(text[0]);
  }
  return 0;
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
    reinterpret_cast<const char*>(sqlite3_column_blob(stmt, col)),
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

std::vector<std::string> Database::get_targets() {
  std::vector<std::string> out;
  int ret = sqlite3_exec(imp->db, "select expression from targets;", &fill_vector, &out, 0);
  if (ret != SQLITE_OK) {
    std::cerr << "Could not enumerate wake targets: " << sqlite3_errmsg(imp->db) << std::endl;
    exit(1);
  }
  return out;
}

void Database::add_target(const std::string &target) {
  const char *why = "Could not add a wake target";
  bind_string(why, imp->add_target, 1, target);
  single_step(why, imp->add_target, imp->debugdb);
}

void Database::del_target(const std::string &target) {
  const char *why = "Could not remove a wake target";
  bind_string(why, imp->del_target, 1, target);
  single_step(why, imp->del_target, imp->debugdb);
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
  single_step("Could not clean database jobs",  imp->delete_jobs,  imp->debugdb);
  single_step("Could not clean database stats", imp->delete_stats, imp->debugdb);
}

void Database::begin_txn() {
  single_step("Could not begin a transaction", imp->begin_txn, imp->debugdb);
}

void Database::end_txn() {
  single_step("Could not commit a transaction", imp->commit_txn, imp->debugdb);
}

bool Database::reuse_job(
  const std::string &directory,
  const std::string &stdin,
  const std::string &environment,
  const std::string &commandline,
  const std::string &visible,
  bool check,
  long &job,
  std::vector<FileReflection> &out)
{
  const char *why = "Could not check for a cached job";
  begin_txn();
  bind_string (why, imp->find_prior, 1, directory);
  bind_blob   (why, imp->find_prior, 2, commandline);
  bind_blob   (why, imp->find_prior, 3, environment);
  bind_string (why, imp->find_prior, 4, stdin);
  bool prior = sqlite3_step(imp->find_prior) == SQLITE_ROW;
  if (prior) job = sqlite3_column_int(imp->find_prior, 0);
  finish_stmt (why, imp->find_prior, imp->debugdb);

  if (!prior) {
    end_txn();
    return false;
  }

  bool exist = true;
  bind_integer(why, imp->get_tree, 1, job);
  bind_integer(why, imp->get_tree, 2, OUTPUT);
  while (sqlite3_step(imp->get_tree) == SQLITE_ROW) {
    std::string path = rip_column(imp->get_tree, 0);
    if (access(path.c_str(), R_OK) != 0) exist = false;
    out.emplace_back(std::move(path), rip_column(imp->get_tree, 1));
  }
  finish_stmt(why, imp->get_tree, imp->debugdb);

  if (exist && !check) {
    bind_integer(why, imp->update_prior, 1, imp->run_id);
    bind_integer(why, imp->update_prior, 2, job);
    single_step (why, imp->update_prior, imp->debugdb);
  }

  end_txn();

  return exist;
}

void Database::insert_job(
  const std::string &directory,
  const std::string &stdin,
  const std::string &environment,
  const std::string &commandline,
  const std::string &stack,
  long  *job)
{
  const char *why = "Could not insert a job";
  bind_integer(why, imp->insert_job, 1, imp->run_id);
  bind_string (why, imp->insert_job, 2, directory);
  bind_blob   (why, imp->insert_job, 3, commandline);
  bind_blob   (why, imp->insert_job, 4, environment);
  bind_blob   (why, imp->insert_job, 5, stack);
  bind_string (why, imp->insert_job, 6, stdin);
  single_step (why, imp->insert_job, imp->debugdb);
  *job = sqlite3_last_insert_rowid(imp->db);
}

void Database::finish_job(long job, const std::string &inputs, const std::string &outputs, uint64_t hashcode, bool keep, int status, double runtime) {
  const char *why = "Could not save job inputs and outputs";
  begin_txn();
  bind_integer(why, imp->add_stats, 1, hashcode);
  bind_integer(why, imp->add_stats, 2, status);
  bind_double (why, imp->add_stats, 3, runtime);
  bind_double (why, imp->add_stats, 4, 0.0); // cputime
  bind_integer(why, imp->add_stats, 5, 0);   // membytes
  bind_integer(why, imp->add_stats, 6, 0);   // iobytes
  single_step (why, imp->add_stats, imp->debugdb);
  bind_integer(why, imp->link_stats, 1, sqlite3_last_insert_rowid(imp->db));
  bind_integer(why, imp->link_stats, 2, keep?1:0);
  bind_integer(why, imp->link_stats, 3, job);
  single_step (why, imp->link_stats, imp->debugdb);
  const char *tok = inputs.c_str();
  const char *end = tok + inputs.size();
  for (const char *scan = tok; scan != end; ++scan) {
    if (*scan == 0 && scan != tok) {
      bind_integer(why, imp->insert_tree, 1, INPUT);
      bind_integer(why, imp->insert_tree, 2, job);
      bind_string (why, imp->insert_tree, 3, tok, scan-tok);
      single_step (why, imp->insert_tree, imp->debugdb);
      tok = scan+1;
    }
  }
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
      reinterpret_cast<const char*>(sqlite3_column_blob(imp->get_log, 0)),
      sqlite3_column_bytes(imp->get_log, 0));
  }
  finish_stmt(why, imp->get_log, imp->debugdb);
  return out.str();
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

std::vector<JobReflection> Database::explain(const std::string &file, int use, bool verbose) {
  const char *why = "Could not explain file";
  std::vector<JobReflection> out;

  begin_txn();
  bind_string (why, imp->find_owner, 1, file);
  bind_integer(why, imp->find_owner, 2, use);
  while (sqlite3_step(imp->find_owner) == SQLITE_ROW) {
    out.resize(out.size()+1);
    JobReflection &desc = out.back();
    desc.job = sqlite3_column_int64(imp->find_owner, 0);
    desc.directory = rip_column(imp->find_owner, 1);
    desc.commandline = chop_null(rip_column(imp->find_owner, 2));
    desc.environment = chop_null(rip_column(imp->find_owner, 3));
    desc.stack = rip_column(imp->find_owner, 4);
    desc.stdin = rip_column(imp->find_owner, 5);
    desc.time = rip_column(imp->find_owner, 6);
    desc.status = sqlite3_column_int64(imp->find_owner, 7);
    desc.runtime = sqlite3_column_double(imp->find_owner, 8);
    if (desc.stdin.empty()) desc.stdin = "/dev/null";
    if (verbose) {
      desc.stdout = get_output(desc.job, 1);
      desc.stderr = get_output(desc.job, 2);
    }
    // inputs
    bind_integer(why, imp->get_tree, 1, desc.job);
    bind_integer(why, imp->get_tree, 2, INPUT);
    while (sqlite3_step(imp->get_tree) == SQLITE_ROW)
      desc.inputs.emplace_back(
        rip_column(imp->get_tree, 0),
        rip_column(imp->get_tree, 1));
    finish_stmt(why, imp->get_tree, imp->debugdb);
    // outputs
    bind_integer(why, imp->get_tree, 1, desc.job);
    bind_integer(why, imp->get_tree, 2, OUTPUT);
    while (sqlite3_step(imp->get_tree) == SQLITE_ROW)
      desc.outputs.emplace_back(
        rip_column(imp->get_tree, 0),
        rip_column(imp->get_tree, 1));
    finish_stmt(why, imp->get_tree, imp->debugdb);
  }
  finish_stmt(why, imp->find_owner, imp->debugdb);
  end_txn();

  return out;
}
