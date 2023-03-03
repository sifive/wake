--dummy, R"(

pragma auto_vacuum=incremental;
pragma journal_mode=wal;
pragma synchronous=0;
pragma locking_mode=normal;
pragma foreign_keys=on;

begin immediate transaction;

-- In order to look up compaitable jobs quickly we need
-- a special table of some kind. We use a bloom filter
-- table. We have an index on (directory, commandline, environment,
-- stdin) and from there we do a scan over our bloom_filters. Any
-- remaining matching jobs can be checked against the the input_files,
-- and input_dirs tables.
create table if not exists jobs(
  job_id       integer primary key autoincrement,
  directory    text    not null,
  commandline  blob    not null,
  environment  blob    not null,
  stdin        text    not null,
  bloom_filter integer);
create index if not exists job on jobs(directory, commandline, environment, stdin);

-- This table stores all the details about a job that aren't known until
-- after the job has finished executing. As of right now it has no reason
-- to exist seperately from the jobs table but eventully jobs might exist
-- in a half finished state. This is basically the info needed by
-- prim "job_virtual" and RunnerOutput.
create table if not exists job_output_info(
  job     job_id primary key not null references jobs(job_id) on delete cascade,
  stdout  text               not null,
  stderr  text               not null,
  ret     integer            not null,
  runtime float              not null,
  cputime float              not null,
  mem     integer            not null,
  ibytes  integer            not null,
  obytes  integer            not null);

-- We only record the input hashes, and not all visible files.
-- The input file blobs are not stored on disk. Only their hash
-- is stored
create table if not exists input_files(
  input_file_id integer primary key autoincrement,
  path          text    not null,
  hash          text    not null,
  job           job_id  not null references jobs(job_id) on delete cascade);
create index if not exists input_file_by_job on input_files(job);

-- We also need to know about directories that have been read
-- in some way. For instance if a file fails to be read in
-- a directory or if a readdir is performed. We only
-- store a hash of a subset of the dirent information for
-- a directory. Namely the name of each entry and its d_type.
-- This hash is crptographic so we do not intend on seeing
-- collisions.
create table if not exists input_dirs(
  input_dir_id integer primary key autoincrement,
  path         text    not null,
  hash         text    not null,
  job          job_id  not null references jobs(job_id) on delete cascade);
create index if not exists input_dir_by_job on input_dirs(job);

-- We don't record where a wake job writes an output file
-- only where the file is placed within the sandbox. Each
-- seperate sandbox will provide a distinct remapping of
-- these items. The blobs of these hashes will also
-- be stored on disk.
create table if not exists output_files(
  output_file_id integer primary key autoincrement,
  path           text    not null,
  hash           text    not null,
  mode           integer not null,
  job            job_id  not null references jobs(job_id) on delete cascade);
create index if not exists output_file_by_job on output_files(job);
create index if not exists find_file on output_files(hash);

-- Similar to output_files but for directories. The mode is the only
-- content of a directory.
create table if not exists output_dirs(
  output_dir_id integer primary key autoincrement,
  path           text    not null,
  mode           integer not null,
  job            job_id  not null references jobs(job_id) on delete cascade);
create index if not exists output_dir_by_job on output_dirs(job);

-- Similar to output_files but for symlinks.
-- `value` is the result of running `readlink` on this.
create table if not exists output_symlinks(
  output_dir_id integer primary key autoincrement,
  path           text    not null,
  value          text    not null,
  job            job_id  not null references jobs(job_id) on delete cascade);
create index if not exists output_symlink_by_job on output_symlinks(job);

commit transaction;
--)"
