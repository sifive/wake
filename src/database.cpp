#include "database.h"
#include <iostream>
#include <sstream>
#include <sqlite3.h>
#include <unistd.h>
#include <cstdlib>

#define VISIBLE 0
#define INPUT 1
#define OUTPUT 2

struct Database::detail {
  sqlite3 *db;
  sqlite3_stmt *add_target;
  sqlite3_stmt *del_target;
  sqlite3_stmt *begin_txn;
  sqlite3_stmt *commit_txn;
  sqlite3_stmt *insert_job;
  sqlite3_stmt *insert_tree;
  sqlite3_stmt *insert_log;
  sqlite3_stmt *insert_file;
  sqlite3_stmt *insert_hash;
  sqlite3_stmt *get_log;
  sqlite3_stmt *get_tree;
  sqlite3_stmt *set_runtime;
  sqlite3_stmt *delete_tree;
  sqlite3_stmt *delete_prior;
  sqlite3_stmt *insert_temp;
  sqlite3_stmt *find_prior;
  sqlite3_stmt *needs_build;
  sqlite3_stmt *wipe_temp;
  sqlite3_stmt *find_owner;
  long run_id;
  detail() : db(0), add_target(0), del_target(0), begin_txn(0), commit_txn(0), insert_job(0),
             insert_tree(0), insert_log(0), insert_file(0), insert_hash(0), get_log(0), get_tree(0),
             set_runtime(0), delete_tree(0), delete_prior(0), insert_temp(0), find_prior(0),
             needs_build(0), wipe_temp(0), find_owner(0) { }
};

Database::Database() : imp(new detail) { }
Database::~Database() { close(); }

std::string Database::open() {
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
    "create table if not exists targets("
    "  expression text primary key);"
    "create table if not exists runs("
    "  run_id integer primary key,"
    "  time   text    not null default current_timestamp);"
    "create table if not exists files("
    "  file_id integer primary key,"
    "  path    text    not null);"
    "create unique index if not exists filenames on files(path);"
    "create table if not exists hashes("
    "  run_id  integer not null references runs(run_id),"
    "  file_id integer not null references files(file_id),"
    "  hash    text    not null,"
    "  primary key(run_id, file_id));"
    "create table if not exists jobs("
    "  job_id      integer primary key,"
    "  run_id      integer not null references runs(run_id),"
    "  directory   text    not null,"
    "  commandline text    not null,"
    "  environment text    not null,"
    "  stack       text    not null,"
    "  stdin       integer," // null means /dev/null
    "  time        text    not null default current_timestamp,"
    "  status      integer,"
    "  runtime     real,"
    "  foreign key(run_id, stdin) references hashes(run_id, file_id));"
    "create index if not exists job on jobs(directory, commandline, environment, stdin);"
    "create table if not exists filetree("
    "  access  integer not null," // 0=visible, 1=input, 2=output
    "  job_id  integer not null references jobs(job_id),"
    "  file_id integer not null," // implied by hashes constraint: references files(file_id)
    "  run_id  integer not null," // implied by hashes constraint: references runs(run_id)
    "  primary key(job_id, access, file_id),"
    "  foreign key(run_id, file_id) references hashes(run_id, file_id));"
    "create index if not exists filesearch on filetree(access, file_id);"
    "create temp table temptree("
    "  run_id  integer not null," // implied by hashes constraint: references runs(run_id)
    "  file_id integer not null," // implied by hashes constraint: references files(file_id)
    "  foreign key(run_id, file_id) references hashes(run_id, file_id));"
    "create table if not exists log("
    "  job_id     integer not null references jobs(job_id),"
    "  descriptor integer not null," // 1=stdout, 2=stderr"
    "  seconds    real    not null," // seconds after job start
    "  output     text    not null);"
    "create index if not exists logorder on log(job_id, descriptor, seconds);";
  char *fail;
  ret = sqlite3_exec(imp->db, schema_sql, 0, 0, &fail);
  if (ret != SQLITE_OK) {
    std::string out = fail;
    sqlite3_free(fail);
    close();
    return out;
  }

  // prepare statements
  const char *sql_add_target = "insert into targets(expression) values(?)";
  const char *sql_del_target = "delete from targets where expression=?";
  const char *sql_begin_txn = "begin transaction";
  const char *sql_commit_txn = "commit transaction";
  const char *sql_insert_job =
    "insert into jobs(run_id, directory, commandline, environment, stack, stdin) "
    "values(?, ?, ?, ?, ?, (select file_id from files where path=?))";
  const char *sql_insert_tree =
    "insert into filetree(access, job_id, file_id, run_id) "
    "values(?, ?, (select file_id from files where path=?), ?)";
  const char *sql_insert_log =
    "insert into log(job_id, descriptor, seconds, output) "
    "values(?, ?, ?, ?)";
  const char *sql_insert_file = "insert or ignore into files(path) values (?)";
  const char *sql_insert_hash =
    "insert into hashes(run_id, file_id, hash) "
    "values(?, (select file_id from files where path=?), ?)";
  const char *sql_get_log = "select output from log where job_id=? and descriptor=? order by seconds";
  const char *sql_get_tree =
    "select p.path, h.hash from filetree t, hashes h, files p"
    " where t.access=? and t.job_id=? and h.run_id=t.run_id and h.file_id=t.file_id and p.file_id=t.file_id";
  const char *sql_set_runtime = "update jobs set status=?, runtime=? where job_id=?";
  const char *sql_delete_tree =
    "delete from filetree where job_id in (select job_id from jobs where "
    "directory=? and commandline=? and environment=?)";
  const char *sql_delete_prior =
    "delete from jobs where "
    "directory=? and commandline=? and environment=?";
  const char *sql_insert_temp =
    "insert into temptree(run_id, file_id) values(?, (select file_id from files where path=?))";
  const char *sql_find_prior =
    "select job_id from jobs where "
    "directory=? and commandline=? and environment=? and status=0";
  const char *sql_needs_build =
    "select h.file_id, h.hash from jobs j, filetree f, hashes h"
    " where j.directory=? and j.commandline=? and j.environment=? and j.status=0"
    "  and f.access=1 and f.job_id=j.job_id"
    "  and h.run_id=f.run_id and h.file_id=f.file_id "
    "except "
    "select h.file_id, h.hash from temptree f, hashes h"
    " where h.run_id=f.run_id and h.file_id=f.file_id";
  const char *sql_wipe_temp = "delete from temptree";
  const char *sql_find_owner =
    "select j.job_id, j.directory, j.commandline, j.environment, j.stack, j.stdin, j.time, j.status, j.runtime"
    " from files f, filetree t, jobs j"
    " where f.path=? and t.access=? and t.file_id=f.file_id and j.job_id=t.job_id";

#define PREPARE(sql, member)										\
  ret = sqlite3_prepare_v2(imp->db, sql, -1, &imp->member, 0);						\
  if (ret != SQLITE_OK) {										\
    std::string out = std::string("sqlite3_prepare_v2 " #member ": ") + sqlite3_errmsg(imp->db);	\
    close();												\
    return out;												\
  }

  PREPARE(sql_add_target,   add_target);
  PREPARE(sql_del_target,   del_target);
  PREPARE(sql_begin_txn,    begin_txn);
  PREPARE(sql_commit_txn,   commit_txn);
  PREPARE(sql_insert_job,   insert_job);
  PREPARE(sql_insert_tree,  insert_tree);
  PREPARE(sql_insert_log,   insert_log);
  PREPARE(sql_insert_file,  insert_file);
  PREPARE(sql_insert_hash,  insert_hash);
  PREPARE(sql_get_log,      get_log);
  PREPARE(sql_get_tree,     get_tree);
  PREPARE(sql_set_runtime,  set_runtime);
  PREPARE(sql_delete_tree,  delete_tree);
  PREPARE(sql_delete_prior, delete_prior);
  PREPARE(sql_insert_temp,  insert_temp);
  PREPARE(sql_find_prior,   find_prior);
  PREPARE(sql_needs_build,  needs_build);
  PREPARE(sql_wipe_temp,    wipe_temp);
  PREPARE(sql_find_owner,   find_owner);

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

  FINALIZE(add_target);
  FINALIZE(del_target);
  FINALIZE(begin_txn);
  FINALIZE(commit_txn);
  FINALIZE(insert_job);
  FINALIZE(insert_tree);
  FINALIZE(insert_log);
  FINALIZE(insert_file);
  FINALIZE(insert_hash);
  FINALIZE(get_log);
  FINALIZE(get_tree);
  FINALIZE(set_runtime);
  FINALIZE(delete_tree);
  FINALIZE(delete_prior);
  FINALIZE(insert_temp);
  FINALIZE(find_prior);
  FINALIZE(needs_build);
  FINALIZE(wipe_temp);
  FINALIZE(find_owner);

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

static void finish_stmt(const char *why, sqlite3_stmt *stmt) {
  int ret;

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

static void single_step(const char *why, sqlite3_stmt *stmt) {
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

  finish_stmt(why, stmt);
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
    reinterpret_cast<const char*>(sqlite3_column_text(stmt, col)),
    sqlite3_column_bytes(stmt, col));
}

std::vector<std::string> Database::get_targets() {
  std::vector<std::string> out;
  int ret = sqlite3_exec(imp->db, "select expression from targets;", &fill_vector, &out, 0);
  if (ret != SQLITE_OK)
    std::cerr << "Could not enumerate wake targets: " << sqlite3_errmsg(imp->db) << std::endl;
  return out;
}

void Database::add_target(const std::string &target) {
  const char *why = "Could not add a wake target";
  bind_string(why, imp->add_target, 1, target);
  single_step(why, imp->add_target);
}

void Database::del_target(const std::string &target) {
  const char *why = "Could not remove a wake target";
  bind_string(why, imp->del_target, 1, target);
  single_step(why, imp->del_target);
}

void Database::prepare() {
  std::vector<std::string> out;
  const char *sql = "insert into runs(run_id) values(null);";
  int ret = sqlite3_exec(imp->db, sql, 0, 0, 0);
  if (ret != SQLITE_OK) {
    std::cerr << "Could not enumerate wake targets: " << sqlite3_errmsg(imp->db) << std::endl;
    exit(1);
  }
  imp->run_id = sqlite3_last_insert_rowid(imp->db);
}

void Database::clean(bool verbose) {
  // !!!
}

void Database::begin_txn() {
  single_step("Could not begin a transaction", imp->begin_txn);
}

void Database::end_txn() {
  single_step("Could not commit a transaction", imp->commit_txn);
}

bool Database::reuse_job(
  const std::string &directory,
  const std::string &stdin,
  const std::string &environment,
  const std::string &commandline,
  const std::string &visible,
  long *job)
{
  const char *why = "Could not check for a cached job";
  begin_txn();
  bind_string (why, imp->find_prior, 1, directory);
  bind_string (why, imp->find_prior, 2, commandline);
  bind_string (why, imp->find_prior, 3, environment);
  bool prior = sqlite3_step(imp->find_prior) == SQLITE_ROW;
  if (prior) *job = sqlite3_column_int(imp->find_prior, 0);
  finish_stmt (why, imp->find_prior);

  if (!prior) {
    end_txn();
    return false;
  }

  const char *tok = visible.c_str();
  const char *end = tok + visible.size();
  for (const char *scan = tok; scan != end; ++scan) {
    if (*scan == 0 && scan != tok) {
      bind_integer(why, imp->insert_temp, 1, imp->run_id);
      bind_string (why, imp->insert_temp, 2, tok, scan-tok);
      single_step (why, imp->insert_temp);
      tok = scan+1;
    }
  }
  bind_string (why, imp->needs_build, 1, directory);
  bind_string (why, imp->needs_build, 2, commandline);
  bind_string (why, imp->needs_build, 3, environment);
  bool rerun = sqlite3_step(imp->needs_build) == SQLITE_ROW;
  finish_stmt (why, imp->needs_build);
  single_step (why, imp->wipe_temp);

  if (rerun) {
    end_txn();
    return false;
  }

  bool exist = true;
  bind_integer(why, imp->get_tree, 1, OUTPUT);
  bind_integer(why, imp->get_tree, 2, *job);
  while (sqlite3_step(imp->get_tree) == SQLITE_ROW) {
    std::string path = rip_column(imp->get_tree, 0);
    if (access(path.c_str(), R_OK) != 0) exist = false;
  }
  finish_stmt(why, imp->get_tree);
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
  long out;
  const char *why = "Could not insert a job";
  begin_txn();
  bind_string (why, imp->delete_tree, 1, directory);
  bind_string (why, imp->delete_tree, 2, commandline);
  bind_string (why, imp->delete_tree, 3, environment);
  single_step (why, imp->delete_tree);
  bind_string (why, imp->delete_prior, 1, directory);
  bind_string (why, imp->delete_prior, 2, commandline);
  bind_string (why, imp->delete_prior, 3, environment);
  single_step (why, imp->delete_prior);
  bind_integer(why, imp->insert_job, 1, imp->run_id);
  bind_string (why, imp->insert_job, 2, directory);
  bind_string (why, imp->insert_job, 3, commandline);
  bind_string (why, imp->insert_job, 4, environment);
  bind_string (why, imp->insert_job, 5, stack);
  bind_string (why, imp->insert_job, 6, stdin);
  single_step (why, imp->insert_job);
  out = sqlite3_last_insert_rowid(imp->db);
  end_txn();
  *job = out;
}

void Database::finish_job(long job, const std::string &inputs, const std::string &outputs, int status, double runtime) {
  const char *why = "Could not save job inputs and outputs";
  begin_txn();
  bind_integer(why, imp->set_runtime, 1, status);
  bind_double (why, imp->set_runtime, 2, runtime);
  bind_integer(why, imp->set_runtime, 3, job);
  single_step (why, imp->set_runtime);
  const char *tok = inputs.c_str();
  const char *end = tok + inputs.size();
  for (const char *scan = tok; scan != end; ++scan) {
    if (*scan == 0 && scan != tok) {
      bind_integer(why, imp->insert_tree, 1, INPUT);
      bind_integer(why, imp->insert_tree, 2, job);
      bind_string (why, imp->insert_tree, 3, tok, scan-tok);
      bind_integer(why, imp->insert_tree, 4, imp->run_id);
      single_step (why, imp->insert_tree);
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
      bind_integer(why, imp->insert_tree, 4, imp->run_id);
      single_step (why, imp->insert_tree);
      tok = scan+1;
    }
  }
  end_txn();
}

std::vector<FileReflection> Database::get_tree(int kind, long job)  {
  std::vector<FileReflection> out;
  const char *why = "Could not read job tree";
  bind_integer(why, imp->get_tree, 1, kind);
  bind_integer(why, imp->get_tree, 2, job);
  while (sqlite3_step(imp->get_tree) == SQLITE_ROW)
    out.emplace_back(rip_column(imp->get_tree, 0), rip_column(imp->get_tree, 1));
  finish_stmt(why, imp->get_tree);
  return out;
}

void Database::save_output(long job, int descriptor, const char *buffer, int size, double runtime) {
  const char *why = "Could not save job output";
  bind_integer(why, imp->insert_log, 1, job);
  bind_integer(why, imp->insert_log, 2, descriptor);
  bind_double (why, imp->insert_log, 3, runtime);
  bind_string (why, imp->insert_log, 4, buffer, size);
  single_step (why, imp->insert_log);
}

std::string Database::get_output(long job, int descriptor) {
  std::stringstream out;
  const char *why = "Could not read job output";
  bind_integer(why, imp->get_log, 1, job);
  bind_integer(why, imp->get_log, 2, descriptor);
  while (sqlite3_step(imp->get_log) == SQLITE_ROW) {
    out.write(
      reinterpret_cast<const char*>(sqlite3_column_text(imp->get_log, 0)),
      sqlite3_column_bytes(imp->get_log, 0));
  }
  finish_stmt(why, imp->get_log);
  return out.str();
}

void Database::add_hash(const std::string &file, const std::string &hash) {
  const char *why = "Could not insert a hash";
  begin_txn();
  bind_string (why, imp->insert_file, 1, file);
  single_step (why, imp->insert_file);
  bind_integer(why, imp->insert_hash, 1, imp->run_id);
  bind_string (why, imp->insert_hash, 2, file);
  bind_string (why, imp->insert_hash, 3, hash);
  single_step (why, imp->insert_hash);
  end_txn();
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

std::vector<JobReflection> Database::explain(const std::string &file, int use) {
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
    // inputs
    bind_integer(why, imp->get_tree, 1, INPUT);
    bind_integer(why, imp->get_tree, 2, desc.job);
    while (sqlite3_step(imp->get_tree) == SQLITE_ROW)
      desc.inputs.emplace_back(
        rip_column(imp->get_tree, 0),
        rip_column(imp->get_tree, 1));
    finish_stmt(why, imp->get_tree);
    // inputs
    bind_integer(why, imp->get_tree, 1, OUTPUT);
    bind_integer(why, imp->get_tree, 2, desc.job);
    while (sqlite3_step(imp->get_tree) == SQLITE_ROW)
      desc.outputs.emplace_back(
        rip_column(imp->get_tree, 0),
        rip_column(imp->get_tree, 1));
    finish_stmt(why, imp->get_tree);
  }
  finish_stmt(why, imp->find_owner);
  end_txn();

  return out;
}
