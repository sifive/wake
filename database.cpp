/*
create table targets(
  expression text primary key);
create table runs(
  run_id integer primary key,
  time   text    not null default current_timestamp);
create table files(
  file_id integer primary key,
  path    text    not null);
create unique index filenames on files(path);
create table hashes(
  file_id integer not null references files(file_id),
  run_id  integer not null references runs(run_id),
  hash    text    not null,
  primary key(file_id, run_id));
create table jobs(
  job_id      integer primary key,
  run_id      integer not null references runs(run_id),
  directory   text    not null,
  commandline text    not null,
  environment text    not null,
  stack       text    not null,
  stdin       integer references files(file_id),
  time        text    not null default current_timestamp,
  runtime     real);
create index job on jobs(directory, commandline, environment);
create table filetree(
  access  integer not null, -- 0=visible, 1=input, 2=output
  job_id  integer not null references jobs(job_id),
  file_id integer not null references files(file_id),
  primary key(access, job_id));
create table log(
  job_id     integer not null references jobs(job_id),
  descriptor integer not null, -- 1=stdout, 2=stderr
  seconds    real    not null, -- seconds after job start
  output     text    not null,
  primary key(job_id, descriptor, seconds));
*/

#include "database.h"

struct Database::detail {
};

Database::Database() : impl(new detail) { }
Database::~Database() { close(); }

std::string Database::open() {
  return "";
}

void Database::close() {
}

std::vector<std::string> Database::get_targets() { return std::vector<std::string>(); }
void Database::add_target(const std::string &target) { }
void Database::del_target(const std::string &target) { }

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
