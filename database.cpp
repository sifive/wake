#include "database.h"
#include <iostream>
#include <sqlite3.h>
#include <cstdlib>

struct Database::detail {
  sqlite3 *db;
  detail() : db(0) { }
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
    sqlite3_close(imp->db);
    return out;
  }

  const char *sql =
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
    "  file_id integer not null references files(file_id),"
    "  run_id  integer not null references runs(run_id),"
    "  hash    text    not null,"
    "  primary key(file_id, run_id));"
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
    "  file_id integer not null references files(file_id),"
    "  primary key(access, job_id));"
    "create table if not exists log("
    "  job_id     integer not null references jobs(job_id),"
    "  descriptor integer not null," // 1=stdout, 2=stderr"
    "  seconds    real    not null," // seconds after job start
    "  output     text    not null,"
    "  primary key(job_id, descriptor, seconds));";
  char *fail;
  ret = sqlite3_exec(imp->db, sql, 0, 0, &fail);
  if (ret != SQLITE_OK) {
    std::string out = fail;
    sqlite3_free(fail);
    sqlite3_close(imp->db);
    imp->db = 0;
    return out;
  }

  // prepare statements
  return "";
}

void Database::close() {
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

std::vector<std::string> Database::get_targets() {
  std::vector<std::string> out;
  int ret = sqlite3_exec(imp->db, "select expression from targets;", &fill_vector, &out, 0);
  if (ret != SQLITE_OK)
    std::cerr << "Could not enumerate wake targets: " << sqlite3_errmsg(imp->db) << std::endl;
  return out;
}

void Database::add_target(const std::string &target) {
  int ret;
  sqlite3_stmt *stmt;
  ret = sqlite3_prepare_v2(imp->db, "insert into targets(expression) values(?);", -1, &stmt, 0);
  if (ret != SQLITE_OK) {
    std::cerr << "Could not add a wake target; sqlite3_prepare_v2: " << sqlite3_errmsg(imp->db) << std::endl;
    exit(1);
  }
  ret = sqlite3_bind_text(stmt, 1, target.c_str(), target.size(), SQLITE_STATIC);
  if (ret != SQLITE_OK) {
    std::cerr << "Could not add a wake target; sqlite3_bind_text: " << sqlite3_errmsg(imp->db) << std::endl;
    exit(1);
  }
  ret = sqlite3_step(stmt);
  if (ret != SQLITE_DONE) {
    std::cerr << "Could not add a wake target; sqlite3_step: " << sqlite3_errmsg(imp->db) << std::endl;
    exit(1);
  }
  ret = sqlite3_finalize(stmt);
  if (ret != SQLITE_OK) {
    std::cerr << "Could not add a wake target; sqlite3_finalize: " << sqlite3_errmsg(imp->db) << std::endl;
    exit(1);
  }
}

void Database::del_target(const std::string &target) {
  int ret;
  sqlite3_stmt *stmt;
  ret = sqlite3_prepare_v2(imp->db, "delete from targets where expression=?;", -1, &stmt, 0);
  if (ret != SQLITE_OK) {
    std::cerr << "Could not remove a wake target; sqlite3_prepare_v2: " << sqlite3_errmsg(imp->db) << std::endl;
    exit(1);
  }
  ret = sqlite3_bind_text(stmt, 1, target.c_str(), target.size(), SQLITE_STATIC);
  if (ret != SQLITE_OK) {
    std::cerr << "Could not remove a wake target; sqlite3_bind_text: " << sqlite3_errmsg(imp->db) << std::endl;
    exit(1);
  }
  ret = sqlite3_step(stmt);
  if (ret != SQLITE_DONE) {
    std::cerr << "Could not remove a wake target; sqlite3_step: " << sqlite3_errmsg(imp->db) << std::endl;
    exit(1);
  }
  ret = sqlite3_finalize(stmt);
  if (ret != SQLITE_OK) {
    std::cerr << "Could not remove a wake target; sqlite3_finalize: " << sqlite3_errmsg(imp->db) << std::endl;
    exit(1);
  }
}

void Database::prepare() { }
void Database::clean(bool verbose) { }

bool Database::needs_build(
  int   cache,
  const std::string &directory,
  const std::string &commandline,
  const std::string &environment,
  const std::string &stdin,
  const std::string &visible_files,
  const std::string &stack,
  int   *job)
{
  *job = 0;
  return true;
}

void Database::save_output(int job, int descriptor, const char *buffer,int size) {
}

void Database::save_job(int job, const std::string &inputs, const std::string &outputs) {
}

std::string Database::get_output(int job, int descriptor) { return ""; }
std::vector<std::string> Database::get_inputs(int job)  { return std::vector<std::string>(); }
std::vector<std::string> Database::get_outputs(int job) { return std::vector<std::string>(); }
