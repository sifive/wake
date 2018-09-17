#include "database.h"
#include <iostream>
#include <sstream>
#include <sqlite3.h>
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
  //sqlite3_stmt *needs_build;
  long run_id;
  detail() : db(0), add_target(0), del_target(0), begin_txn(0), commit_txn(0), insert_job(0),
             insert_tree(0), insert_log(0), insert_file(0), insert_hash(0), get_log(0),
             get_tree(0) { }
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
    "  stdin       integer references files(file_id),"
    "  time        text    not null default current_timestamp,"
    "  runtime     real);"
    "create index if not exists job on jobs(directory, commandline, environment);"
    "create table if not exists filetree("
    "  access  integer not null," // 0=visible, 1=input, 2=output
    "  job_id  integer not null references jobs(job_id),"
    "  file_id integer not null," // implied by hashes constraint: references files(file_id)
    "  run_id  integer not null," // implied by hashes constraint: references runs(run_id)
    "  primary key(access, job_id, file_id),"
    "  foreign key(run_id, file_id) references hashes(run_id, file_id));"
    "create table if not exists log("
    "  job_id     integer not null references jobs(job_id),"
    "  descriptor integer not null," // 1=stdout, 2=stderr"
    "  seconds    real    not null," // seconds after job start
    "  output     text    not null,"
    "  primary key(job_id, descriptor, seconds));";
  char *fail;
  ret = sqlite3_exec(imp->db, schema_sql, 0, 0, &fail);
  if (ret != SQLITE_OK) {
    std::string out = fail;
    sqlite3_free(fail);
    close();
    return out;
  }

  // prepare statements
  const char *sql_add_target = "insert into targets(expression) values(?);";
  ret = sqlite3_prepare_v2(imp->db, sql_add_target, -1, &imp->add_target, 0);
  if (ret != SQLITE_OK) {
    std::string out = std::string("sqlite3_prepare_v2 add_target: ") + sqlite3_errmsg(imp->db);
    close();
    return out;
  }

  const char *sql_del_target = "delete from targets where expression=?;";
  ret = sqlite3_prepare_v2(imp->db, sql_del_target, -1, &imp->del_target, 0);
  if (ret != SQLITE_OK) {
    std::string out = std::string("sqlite3_prepare_v2 del_target: ") + sqlite3_errmsg(imp->db);
    close();
    return out;
  }

  const char *sql_begin_txn = "begin transaction;";
  ret = sqlite3_prepare_v2(imp->db, sql_begin_txn, -1, &imp->begin_txn, 0);
  if (ret != SQLITE_OK) {
    std::string out = std::string("sqlite3_prepare_v2 begin_txn: ") + sqlite3_errmsg(imp->db);
    close();
    return out;
  }

  const char *sql_commit_txn = "commit transaction;";
  ret = sqlite3_prepare_v2(imp->db, sql_commit_txn, -1, &imp->commit_txn, 0);
  if (ret != SQLITE_OK) {
    std::string out = std::string("sqlite3_prepare_v2 commit_txn: ") + sqlite3_errmsg(imp->db);
    close();
    return out;
  }

  const char *sql_insert_job =
    "insert into jobs(run_id, directory, commandline, environment, stack, stdin) "
    "values(?, ?, ?, ?, ?, (select file_id from files where path=?));";
  ret = sqlite3_prepare_v2(imp->db, sql_insert_job, -1, &imp->insert_job, 0);
  if (ret != SQLITE_OK) {
    std::string out = std::string("sqlite3_prepare_v2 insert_job: ") + sqlite3_errmsg(imp->db);
    close();
    return out;
  }

  const char *sql_insert_tree =
    "insert into filetree(access, job_id, file_id, run_id) "
    "values(?, ?, (select file_id from files where path=?), ?);";
  ret = sqlite3_prepare_v2(imp->db, sql_insert_tree, -1, &imp->insert_tree, 0);
  if (ret != SQLITE_OK) {
    std::string out = std::string("sqlite3_prepare_v2 insert_tree: ") + sqlite3_errmsg(imp->db);
    close();
    return out;
  }

  const char *sql_insert_log =
    "insert into log(job_id, descriptor, seconds, output) "
    "values(?, ?, ?, ?);";
  ret = sqlite3_prepare_v2(imp->db, sql_insert_log, -1, &imp->insert_log, 0);
  if (ret != SQLITE_OK) {
    std::string out = std::string("sqlite3_prepare_v2 insert_log: ") + sqlite3_errmsg(imp->db);
    close();
    return out;
  }

  const char *sql_insert_file = "insert or ignore into files(path) values (?);";
  ret = sqlite3_prepare_v2(imp->db, sql_insert_file, -1, &imp->insert_file, 0);
  if (ret != SQLITE_OK) {
    std::string out = std::string("sqlite3_prepare_v2 insert_file: ") + sqlite3_errmsg(imp->db);
    close();
    return out;
  }

  const char *sql_insert_hash =
    "insert into hashes(run_id, file_id, hash) "
    "values(?, (select file_id from files where path=?), ?);";
  ret = sqlite3_prepare_v2(imp->db, sql_insert_hash, -1, &imp->insert_hash, 0);
  if (ret != SQLITE_OK) {
    std::string out = std::string("sqlite3_prepare_v2 insert_hash: ") + sqlite3_errmsg(imp->db);
    close();
    return out;
  }

  const char *sql_get_log = "select output from log where job_id=? and descriptor=? order by seconds;";
  ret = sqlite3_prepare_v2(imp->db, sql_get_log, -1, &imp->get_log, 0);
  if (ret != SQLITE_OK) {
    std::string out = std::string("sqlite3_prepare_v2 get_log: ") + sqlite3_errmsg(imp->db);
    close();
    return out;
  }

  const char *sql_get_tree =
    "select p.path from filetree t, files p"
    " where t.access=? and t.job_id=? and p.file_id=t.file_id;";
  ret = sqlite3_prepare_v2(imp->db, sql_get_tree, -1, &imp->get_tree, 0);
  if (ret != SQLITE_OK) {
    std::string out = std::string("sqlite3_prepare_v2 get_tree: ") + sqlite3_errmsg(imp->db);
    close();
    return out;
  }

  return "";
}

void Database::close() {
  int ret;

  if (imp->add_target) {
    ret = sqlite3_finalize(imp->add_target);
    if (ret != SQLITE_OK) {
      std::cerr << "Could not sqlite3_finalize add_target: " << sqlite3_errmsg(imp->db) << std::endl;
      return;
    }
  }
  imp->add_target = 0;

  if (imp->del_target) {
    ret = sqlite3_finalize(imp->del_target);
    if (ret != SQLITE_OK) {
      std::cerr << "Could not sqlite3_finalize del_target: " << sqlite3_errmsg(imp->db) << std::endl;
      return;
    }
  }
  imp->del_target = 0;

  if (imp->begin_txn) {
    ret = sqlite3_finalize(imp->begin_txn);
    if (ret != SQLITE_OK) {
      std::cerr << "Could not sqlite3_finalize begin_txn: " << sqlite3_errmsg(imp->db) << std::endl;
      return;
    }
  }
  imp->begin_txn = 0;

  if (imp->commit_txn) {
    ret = sqlite3_finalize(imp->commit_txn);
    if (ret != SQLITE_OK) {
      std::cerr << "Could not sqlite3_finalize commit_txn: " << sqlite3_errmsg(imp->db) << std::endl;
      return;
    }
  }
  imp->commit_txn = 0;

  if (imp->insert_job) {
    ret = sqlite3_finalize(imp->insert_job);
    if (ret != SQLITE_OK) {
      std::cerr << "Could not sqlite3_finalize insert_job: " << sqlite3_errmsg(imp->db) << std::endl;
      return;
    }
  }
  imp->insert_job = 0;

  if (imp->insert_tree) {
    ret = sqlite3_finalize(imp->insert_tree);
    if (ret != SQLITE_OK) {
      std::cerr << "Could not sqlite3_finalize insert_tree: " << sqlite3_errmsg(imp->db) << std::endl;
      return;
    }
  }
  imp->insert_tree = 0;

  if (imp->insert_log) {
    ret = sqlite3_finalize(imp->insert_log);
    if (ret != SQLITE_OK) {
      std::cerr << "Could not sqlite3_finalize insert_log: " << sqlite3_errmsg(imp->db) << std::endl;
      return;
    }
  }
  imp->insert_log = 0;

  if (imp->insert_file) {
    ret = sqlite3_finalize(imp->insert_file);
    if (ret != SQLITE_OK) {
      std::cerr << "Could not sqlite3_finalize insert_file: " << sqlite3_errmsg(imp->db) << std::endl;
      return;
    }
  }
  imp->insert_file = 0;

  if (imp->insert_hash) {
    ret = sqlite3_finalize(imp->insert_hash);
    if (ret != SQLITE_OK) {
      std::cerr << "Could not sqlite3_finalize insert_hash: " << sqlite3_errmsg(imp->db) << std::endl;
      return;
    }
  }
  imp->insert_hash = 0;

  if (imp->get_log) {
    ret = sqlite3_finalize(imp->get_log);
    if (ret != SQLITE_OK) {
      std::cerr << "Could not sqlite3_finalize get_log: " << sqlite3_errmsg(imp->db) << std::endl;
      return;
    }
  }
  imp->get_log = 0;

  if (imp->get_tree) {
    ret = sqlite3_finalize(imp->get_tree);
    if (ret != SQLITE_OK) {
      std::cerr << "Could not sqlite3_finalize get_tree: " << sqlite3_errmsg(imp->db) << std::endl;
      return;
    }
  }
  imp->get_tree = 0;

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

bool Database::needs_build(
  int   cache,
  const std::string &directory,
  const std::string &commandline,
  const std::string &environment,
  const std::string &stdin,
  const std::string &visible_files,
  const std::string &stack,
  long  *job)
{
  long out;
  const char *why = "Could not insert a job";
  begin_txn();
  bind_integer(why, imp->insert_job, 1, imp->run_id);
  bind_string (why, imp->insert_job, 2, directory);
  bind_string (why, imp->insert_job, 3, commandline);
  bind_string (why, imp->insert_job, 4, environment);
  bind_string (why, imp->insert_job, 5, stack);
  bind_string (why, imp->insert_job, 6, stdin); // !!! should do something about this dangling (impacts hash)
  single_step (why, imp->insert_job);
  out = sqlite3_last_insert_rowid(imp->db);
  const char *tok = visible_files.c_str();
  const char *end = tok + visible_files.size();
  for (const char *scan = tok; scan != end; ++scan) {
    if (*scan == 0 && scan != tok) {
      bind_integer(why, imp->insert_tree, 1, VISIBLE);
      bind_integer(why, imp->insert_tree, 2, out);
      bind_string (why, imp->insert_tree, 3, tok, scan-tok);
      bind_integer(why, imp->insert_tree, 4, imp->run_id);
      single_step (why, imp->insert_tree);
    }
  }
/*
  const char *sql_candidates =
    "select job_id, run_id from jobs where directory=?, commandline=?, environment=?;";
  // !!! fail if >1 candidate
  const char *sql =
    "(select h.file_id, h.hash from f filetree, h hashes"
    " where f.access=1, f.job_id=?CANDIDATE?, h.file_id=f.file_id, h.run_id=?CANDIDATE?)"
    "except"
    "(select h.file_id, h.hash from f filetree, h hashes"
    " where f.access=0, f.job_id=?THIS?, h.file_id=f.file_id, h.run_id=?NOW?)";
  // !!! any results -> must rerun
  // if want to reuse, confirm the outputs exist with same hash?
  //   -> suggests we should hash all outputs on completion of a command
*/
  end_txn();
  *job = out;
  return true;
}

void Database::save_job(long job, const std::string &inputs, const std::string &outputs) {
  const char *why = "Could not save job inputs and outputs";
  begin_txn();
  const char *tok = inputs.c_str();
  const char *end = tok + inputs.size();
  for (const char *scan = tok; scan != end; ++scan) {
    if (*scan == 0 && scan != tok) {
      bind_integer(why, imp->insert_tree, 1, INPUT);
      bind_integer(why, imp->insert_tree, 2, job);
      bind_string (why, imp->insert_tree, 3, tok, scan-tok);
      single_step (why, imp->insert_tree);
    }
  }
  tok = outputs.c_str();
  end = tok + outputs.size();
  for (const char *scan = tok; scan != end; ++scan) {
    if (*scan == 0 && scan != tok) {
      bind_integer(why, imp->insert_tree, 1, OUTPUT);
      bind_integer(why, imp->insert_tree, 2, job);
      bind_string (why, imp->insert_tree, 3, tok, scan-tok);
      single_step (why, imp->insert_tree);
    }
  }
  end_txn();
}

std::vector<std::string> Database::get_inputs(long job)  {
  std::vector<std::string> out;
  const char *why = "Could not read job inputs";
  bind_integer(why, imp->get_tree, 1, INPUT);
  bind_integer(why, imp->get_tree, 2, job);
  while (sqlite3_step(imp->get_tree) == SQLITE_ROW) {
    out.emplace_back(
      reinterpret_cast<const char*>(sqlite3_column_text(imp->get_tree, 0)),
      sqlite3_column_bytes(imp->get_tree, 0));
  }
  finish_stmt(why, imp->get_tree);
  return out;
}

std::vector<std::string> Database::get_outputs(long job) {
  std::vector<std::string> out;
  const char *why = "Could not read job outputs";
  bind_integer(why, imp->get_tree, 1, OUTPUT);
  bind_integer(why, imp->get_tree, 2, job);
  while (sqlite3_step(imp->get_tree) == SQLITE_ROW) {
    out.emplace_back(
      reinterpret_cast<const char*>(sqlite3_column_text(imp->get_tree, 0)),
      sqlite3_column_bytes(imp->get_tree, 0));
  }
  finish_stmt(why, imp->get_tree);
  return out;
}

void Database::save_output(long job, int descriptor, const char *buffer, int size) {
  const char *why = "Could not save job output";
  bind_integer(why, imp->insert_log, 1, job);
  bind_integer(why, imp->insert_log, 2, descriptor);
  bind_integer(why, imp->insert_log, 3, 0); // !!! fix me
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
