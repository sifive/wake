--dummy, R"(
pragma auto_vacuum=incremental;
pragma journal_mode=wal;
pragma synchronous=0;
pragma locking_mode=exclusive;
pragma foreign_keys=on;

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


-- We only record the input hashes, and not all visible files.
-- The input file blobs are not stored on disk. Only their hash
-- is stored
create table if not exists input_files(
  input_file_id integer primary key autoincrement,
  path          text    not null,
  hash          text    not null,
  job           job_id  not null references jobs(job_id) on delete cascade);
create index if not exists input_file on input_files(path, hash);


-- We don't record where a wake job writes an output file
-- only where the file is placed within the sandbox. Each
-- seperate sandbox will provide a distinct remapping of
-- these items. The blobs of these hashes will also
-- be stored on disk.
-- TODO(jake): Add mode
create table if not exists output_files(
  output_file_id integer primary key autoincrement,
  path           text    not null,
  hash           text    not null,
  job            job_id  not null references jobs(job_id) on delete cascade);
create index if not exists output_file on output_files(path, hash);
create index if not exists find_file on output_files(hash);


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
create index if not exists input_dir on input_dirs(path, hash);

)"
