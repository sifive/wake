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

#include "types.h"

#include <sys/stat.h>
#include <wcl/filepath.h>
#include <wcl/tracing.h>
#include <wcl/unique_fd.h>

namespace job_cache {
static Hash256 do_hash_file(const char *file, int fd) {
  blake2b_state S;
  uint8_t hash[32];
  static thread_local uint8_t buffer[8192];
  ssize_t got;

  blake2b_init(&S, sizeof(hash));
  while ((got = read(fd, &buffer[0], sizeof(buffer))) > 0) blake2b_update(&S, &buffer[0], got);
  blake2b_final(&S, &hash[0], sizeof(hash));

  if (got < 0) {
    wcl::log::fatal("job-cache hash read(%s): %s", file, strerror(errno));
  }

  return Hash256::from_hash(&hash);
}

CachedOutputFile::CachedOutputFile(const JAST &json) {
  path = json.get("path").value;
  hash = Hash256::from_hex(json.get("hash").value);
  mode = std::stol(json.get("mode").value);
}

JAST CachedOutputFile::to_json() const {
  JAST json(JSON_OBJECT);
  json.add("path", path);
  json.add("hash", hash.to_hex());
  json.add("mode", int64_t(mode));
  return json;
}

CachedOutputSymlink::CachedOutputSymlink(const JAST &json) {
  path = json.get("path").value;
  value = json.get("value").value;
}

JAST CachedOutputSymlink::to_json() const {
  JAST json(JSON_OBJECT);
  json.add("path", path);
  json.add("value", value);
  return json;
}

CachedOutputDir::CachedOutputDir(const JAST &json) {
  path = json.get("path").value;
  mode = std::stol(json.get("mode").value);
}

JAST CachedOutputDir::to_json() const {
  JAST json(JSON_OBJECT);
  json.add("path", path);
  json.add("mode", int64_t(mode));
  return json;
}

JobOutputInfo::JobOutputInfo(const JAST &json) {
  stdout_str = json.get("stdout").value;
  stderr_str = json.get("stderr").value;
  status = std::stoi(json.get("status").value);
  runtime = std::stod(json.get("runtime").value);
  cputime = std::stod(json.get("cputime").value);
  mem = std::stoul(json.get("mem").value);
  ibytes = std::stoul(json.get("ibytes").value);
  obytes = std::stoul(json.get("obytes").value);
}

JAST JobOutputInfo::to_json() const {
  JAST json(JSON_OBJECT);
  json.add("stdout", stdout_str);
  json.add("stderr", stderr_str);
  json.add("status", status);
  json.add("runtime", runtime);
  json.add("cputime", cputime);
  json.add("mem", int64_t(mem));
  json.add("ibytes", int64_t(ibytes));
  json.add("obytes", int64_t(obytes));
  return json;
}

MatchingJob::MatchingJob(const JAST &json) {
  client_cwd = json.get("client_cwd").value;
  output_info = JobOutputInfo(json.get("output_info"));

  for (const auto &output_file_json : json.get("output_files").children) {
    CachedOutputFile output_file(output_file_json.second);
    // Canonicalize matching jobs to use client-relative paths.
    if (wcl::is_absolute(output_file.path)) {
      output_file.path = wcl::relative_to(client_cwd, output_file.path);
    }
    output_files.push_back(output_file);
  }

  for (const auto &output_dir_json : json.get("output_dirs").children) {
    CachedOutputDir output_dir(output_dir_json.second);
    // Canonicalize matching jobs to use client-relative paths.
    if (wcl::is_absolute(output_dir.path)) {
      output_dir.path = wcl::relative_to(client_cwd, output_dir.path);
    }
    output_dirs.push_back(output_dir);
  }

  for (const auto &output_symlink_json : json.get("output_symlinks").children) {
    CachedOutputSymlink output_sym(output_symlink_json.second);
    // Canonicalize matching jobs to use client-relative paths.
    if (wcl::is_absolute(output_sym.path)) {
      output_sym.path = wcl::relative_to(client_cwd, output_sym.path);
    }
    output_symlinks.push_back(output_sym);
  }

  for (const auto &input_file_json : json.get("input_files").children) {
    // Canonicalize matching jobs to use client-relative paths.
    std::string input_path = input_file_json.second.value;
    if (wcl::is_absolute(input_path)) {
      input_path = wcl::relative_to(client_cwd, input_path);
    }
    input_files.push_back(input_path);
  }

  for (const auto &input_dir_json : json.get("input_dirs").children) {
    // Canonicalize matching jobs to use client-relative paths.
    std::string input_dir = input_dir_json.second.value;
    if (wcl::is_absolute(input_dir)) {
      input_dir = wcl::relative_to(client_cwd, input_dir);
    }
    input_dirs.push_back(input_dir);
  }
}

JAST MatchingJob::to_json() const {
  JAST json(JSON_OBJECT);

  json.add("client_cwd", client_cwd);
  json.add("output_info", output_info.to_json());

  JAST output_files_json(JSON_ARRAY);
  for (const auto &output_file : output_files) {
    // Canonicalize matching jobs to use client-relative paths.
    auto output = output_file;
    if (wcl::is_absolute(output.path)) {
      output.path = wcl::relative_to(client_cwd, output.path);
    }
    output_files_json.add("", output.to_json());
  }
  json.add("output_files", std::move(output_files_json));

  JAST output_dirs_json(JSON_ARRAY);
  for (const auto &output_dir : output_dirs) {
    // Canonicalize matching jobs to use client-relative paths.
    auto output = output_dir;
    if (wcl::is_absolute(output.path)) {
      output.path = wcl::relative_to(client_cwd, output.path);
    }
    output_dirs_json.add("", output.to_json());
  }
  json.add("output_dirs", std::move(output_dirs_json));

  JAST output_symlinks_json(JSON_ARRAY);
  for (const auto &output_symlink : output_symlinks) {
    // Canonicalize matching jobs to use client-relative paths.
    auto output = output_symlink;
    if (wcl::is_absolute(output.path)) {
      output.path = wcl::relative_to(client_cwd, output.path);
    }
    output_symlinks_json.add("", output.to_json());
  }
  json.add("output_symlinks", std::move(output_symlinks_json));

  JAST input_files_json(JSON_ARRAY);
  for (const auto &input_file : input_files) {
    // Canonicalize matching jobs to use client-relative paths.
    auto input = input_file;
    if (wcl::is_absolute(input)) {
      input = wcl::relative_to(client_cwd, input);
    }
    input_files_json.add("", input);
  }
  json.add("input_files", std::move(input_files_json));

  JAST input_dirs_json(JSON_ARRAY);
  for (const auto &input_dir : input_dirs) {
    // Canonicalize matching jobs to use client-relative paths.
    auto input = input_dir;
    if (wcl::is_absolute(input)) {
      input = wcl::relative_to(client_cwd, input);
    }
    input_dirs_json.add("", input);
  }
  json.add("input_dirs", std::move(input_dirs_json));

  return json;
}

InputFile::InputFile(const JAST &json) {
  path = json.get("path").value;
  hash = Hash256::from_hex(json.get("hash").value);
}

JAST InputFile::to_json() const {
  JAST json(JSON_OBJECT);
  json.add("path", path);
  json.add("hash", hash.to_hex());
  return json;
}

InputDir::InputDir(const JAST &json) {
  path = json.get("path").value;
  hash = Hash256::from_hex(json.get("hash").value);
}

JAST InputDir::to_json() const {
  JAST json(JSON_OBJECT);
  json.add("path", path);
  json.add("hash", hash.to_hex());
  return json;
}

OutputFile::OutputFile(const JAST &json) {
  source = json.get("source").value;
  path = json.get("path").value;
  hash = Hash256::from_hex(json.get("hash").value);
  mode = std::stol(json.get("mode").value);
}

JAST OutputFile::to_json() const {
  JAST json(JSON_OBJECT);
  json.add("source", source);
  json.add("path", path);
  json.add("hash", hash.to_hex());
  json.add("mode", int64_t(mode));
  return json;
}

OutputDirectory::OutputDirectory(const JAST &json) {
  path = json.get("path").value;
  mode = std::stol(json.get("mode").value);
}

JAST OutputDirectory::to_json() const {
  JAST json(JSON_OBJECT);
  json.add("path", path);
  json.add("mode", int64_t(mode));
  return json;
}

OutputSymlink::OutputSymlink(const JAST &json) {
  path = json.get("path").value;
  value = json.get("value").value;
}

JAST OutputSymlink::to_json() const {
  JAST json(JSON_OBJECT);
  json.add("path", path);
  json.add("value", value);
  return json;
}

AddJobRequest AddJobRequest::from_implicit(const JAST &json) {
  AddJobRequest req;
  req.wakeroot = json.get("wakeroot").value;
  if (wcl::is_relative(req.wakeroot)) {
    wcl::log::fatal("AddJobRequest::from_implicit: wakeroot cannot be relative. found: '%s'",
                    req.wakeroot.c_str());
  }
  req.cwd = json.get("cwd").value;
  if (wcl::is_relative(req.cwd)) {
    req.cwd = wcl::join_paths(req.wakeroot, req.cwd);
  }
  req.command_line = json.get("command_line").value;
  req.envrionment = json.get("envrionment").value;
  req.stdin_str = json.get("stdin").value;
  req.stdout_str = json.get("stdout").value;
  req.stderr_str = json.get("stderr").value;
  req.status = std::stoi(json.get("status").value);
  req.runtime = std::stod(json.get("runtime").value);
  req.cputime = std::stod(json.get("cputime").value);
  req.mem = std::stoull(json.get("mem").value);
  req.ibytes = std::stoull(json.get("ibytes").value);
  req.obytes = std::stoull(json.get("obytes").value);
  req.client_cwd = json.get("client_cwd").value;
  if (wcl::is_relative(req.client_cwd)) {
    wcl::log::fatal("AddJobRequest::from_implicit: client_cwd cannot be relative. found: '%s'",
                    req.client_cwd.c_str());
  }

  // Read the input files
  for (const auto &input_file : json.get("input_files").children) {
    InputFile input(input_file.second);
    // Canonicalize input file paths to sandbox-absolute paths.
    if (wcl::is_relative(input.path)) {
      input.path = wcl::join_paths(req.wakeroot, input.path);
    }
    req.bloom.add_hash(input.hash);
    req.inputs.emplace_back(std::move(input));
  }

  // Read the input dirs
  for (const auto &input_dir : json.get("input_dirs").children) {
    InputDir input(input_dir.second);
    // Canonicalize input dir paths to sandbox-absolute paths.
    if (wcl::is_relative(input.path)) {
      input.path = wcl::join_paths(req.wakeroot, input.path);
    }
    req.bloom.add_hash(input.hash);
    req.directories.emplace_back(std::move(input));
  }

  // TODO: I hate this loop but its the fastest path to a demo.
  //       we need to figure out a path add things to the cache
  //       only after the files have been hashed so we don't
  //       need this loop. Since this job was just run, wake
  //       will eventually hash all these files so the fact
  //       that we have to re-hash them here is a shame.
  // TODO: Use aio_read and do these hashes online and interleaveed
  //       so that the IO can be in parallel despite the hashing being
  //       serial.s. It would also be nice to figure out how to do
  //       the hashing in parallel if we can't avoid it completely.
  // Read the output files which requires kicking off a hash
  for (const auto &output_file : json.get("output_files").children) {
    struct stat buf = {};
    std::string src = output_file.second.get("src").value;
    // Canonicalize src file paths to client-absolute paths.
    if (wcl::is_relative(src)) {
      src = wcl::join_paths(req.client_cwd, src);
    }
    if (lstat(src.c_str(), &buf) < 0) {
      wcl::log::fatal("lstat(%s): %s", src.c_str(), strerror(errno));
    }

    // Handle output directory
    if (S_ISDIR(buf.st_mode)) {
      OutputDirectory dir;
      dir.mode = buf.st_mode;
      dir.path = output_file.second.get("path").value;
      // Canonicalize output dir paths to sandbox-absolute paths.
      if (wcl::is_relative(dir.path)) {
        dir.path = wcl::join_paths(req.wakeroot, dir.path);
      }
      req.output_dirs.emplace_back(std::move(dir));
      continue;
    }

    // Handle symlink
    if (S_ISLNK(buf.st_mode)) {
      OutputSymlink sym;
      static thread_local char link[4097];
      int size = readlink(src.c_str(), link, sizeof(link));
      if (size == -1) {
        wcl::log::fatal("readlink(%s): %s", src.c_str(), strerror(errno));
      }
      sym.path = output_file.second.get("path").value;
      // Canonicalize output symlink paths to sandbox-absolute paths.
      if (wcl::is_relative(sym.path)) {
        sym.path = wcl::join_paths(req.wakeroot, sym.path);
      }
      sym.value = std::string(link, link + size);
      req.output_symlinks.emplace_back(std::move(sym));
      continue;
    }

    // Handle regular files but ignore everything else.
    if (!S_ISREG(buf.st_mode)) continue;
    OutputFile output;
    output.source = output_file.second.get("src").value;
    output.path = output_file.second.get("path").value;
    // Canonicalize output file sources to client-absolute paths.
    if (wcl::is_relative(output.source)) {
      output.source = wcl::join_paths(req.client_cwd, output.source);
    }
    // Canonicalize output file paths to sandbox-absolute paths.
    if (wcl::is_relative(output.path)) {
      output.path = wcl::join_paths(req.wakeroot, output.path);
    }

    auto fd = wcl::unique_fd::open(output.source.c_str(), O_RDONLY);
    if (!fd) {
      wcl::log::fatal("open(%s): %s", output.source.c_str(), strerror(fd.error()));
    }
    output.hash = do_hash_file(output.source.c_str(), fd->get());
    output.mode = buf.st_mode;
    req.outputs.emplace_back(std::move(output));
  }

  return req;
}

AddJobRequest::AddJobRequest(const JAST &json) {
  wakeroot = json.get("wakeroot").value;
  if (wcl::is_relative(wakeroot)) {
    wcl::log::fatal("AddJobRequest::AddJobRequest: wakeroot cannot be relative. found: '%s'",
                    wakeroot.c_str());
  }
  cwd = json.get("cwd").value;
  if (wcl::is_relative(cwd)) {
    cwd = wcl::join_paths(wakeroot, cwd);
  }
  command_line = json.get("command_line").value;
  envrionment = json.get("envrionment").value;
  stdin_str = json.get("stdin").value;
  stdout_str = json.get("stdout").value;
  stderr_str = json.get("stderr").value;
  status = std::stoi(json.get("status").value);
  runtime = std::stod(json.get("runtime").value);
  cputime = std::stod(json.get("cputime").value);
  mem = std::stoull(json.get("mem").value);
  ibytes = std::stoull(json.get("ibytes").value);
  obytes = std::stoull(json.get("obytes").value);
  client_cwd = json.get("client_cwd").value;
  if (wcl::is_relative(client_cwd)) {
    wcl::log::fatal("AddJobRequest::AddJobRequest: client_cwd cannot be relative. found: '%s'",
                    client_cwd.c_str());
  }

  // Read the input files
  for (const auto &input_file : json.get("input_files").children) {
    InputFile input(input_file.second);
    // Canonicalize input file paths to sandbox-absolute paths.
    if (wcl::is_relative(input.path)) {
      input.path = wcl::join_paths(wakeroot, input.path);
    }
    bloom.add_hash(input.hash);
    inputs.emplace_back(std::move(input));
  }

  // Read the input dirs
  for (const auto &input_dir : json.get("input_dirs").children) {
    InputDir input(input_dir.second);
    // Canonicalize input dir paths to sandbox-absolute paths.
    if (wcl::is_relative(input.path)) {
      input.path = wcl::join_paths(wakeroot, input.path);
    }
    bloom.add_hash(input.hash);
    directories.emplace_back(std::move(input));
  }

  for (const auto &output_file : json.get("output_files").children) {
    OutputFile output(output_file.second);
    // Canonicalize output file sources to client-absolute paths.
    if (wcl::is_relative(output.source)) {
      output.source = wcl::join_paths(client_cwd, output.source);
    }
    // Canonicalize output file paths to sandbox-absolute paths.
    if (wcl::is_relative(output.path)) {
      output.path = wcl::join_paths(wakeroot, output.path);
    }
    outputs.emplace_back(std::move(output));
  }

  for (const auto &output_directory : json.get("output_dirs").children) {
    OutputDirectory dir(output_directory.second);
    // Canonicalize output dir paths to sandbox-absolute paths.
    if (wcl::is_relative(dir.path)) {
      dir.path = wcl::join_paths(wakeroot, dir.path);
    }
    output_dirs.emplace_back(std::move(dir));
  }

  for (const auto &output_symlink : json.get("output_symlinks").children) {
    OutputSymlink symlink(output_symlink.second);
    // Canonicalize output symlink paths to sandbox-absolute paths.
    if (wcl::is_relative(symlink.path)) {
      symlink.path = wcl::join_paths(wakeroot, symlink.path);
    }
    output_symlinks.emplace_back(std::move(symlink));
  }
}

JAST AddJobRequest::to_json() const {
  JAST json(JSON_OBJECT);
  json.add("wakeroot", wakeroot);
  json.add("cwd", cwd);
  json.add("command_line", command_line);
  json.add("envrionment", envrionment);
  json.add("stdin", stdin_str);
  json.add("stdout", stdout_str);
  json.add("stderr", stderr_str);
  json.add("status", status);
  json.add("runtime", runtime);
  json.add("cputime", cputime);
  json.add("mem", int64_t(mem));
  json.add("ibytes", int64_t(ibytes));
  json.add("obytes", int64_t(obytes));
  json.add("client_cwd", client_cwd);

  JAST input_files_json(JSON_ARRAY);
  for (const auto &input_file : inputs) {
    input_files_json.add("", input_file.to_json());
  }
  json.add("input_files", std::move(input_files_json));

  JAST input_dirs_json(JSON_ARRAY);
  for (const auto &input_dir : directories) {
    input_dirs_json.add("", input_dir.to_json());
  }
  json.add("input_dirs", std::move(input_dirs_json));

  JAST output_files_json(JSON_ARRAY);
  for (const auto &output_file : outputs) {
    output_files_json.add("", output_file.to_json());
  }
  json.add("output_files", std::move(output_files_json));

  JAST output_directories_json(JSON_ARRAY);
  for (const auto &output_directory : output_dirs) {
    output_directories_json.add("", output_directory.to_json());
  }
  json.add("output_dirs", std::move(output_directories_json));

  JAST output_symlinks_json(JSON_ARRAY);
  for (const auto &output_symlink : output_symlinks) {
    output_symlinks_json.add("", output_symlink.to_json());
  }
  json.add("output_symlinks", std::move(output_symlinks_json));

  return json;
}

FindJobRequest::FindJobRequest(const JAST &find_job_json) {
  wakeroot = find_job_json.get("wakeroot").value;
  if (wcl::is_relative(wakeroot)) {
    wcl::log::fatal("FindJobRequest::FindJobRequest: wakeroot cannot be relative. found: '%s'",
                    wakeroot.c_str());
  }
  cwd = find_job_json.get("cwd").value;
  if (wcl::is_relative(cwd)) {
    cwd = wcl::join_paths(wakeroot, cwd);
  }
  command_line = find_job_json.get("command_line").value;
  envrionment = find_job_json.get("envrionment").value;
  stdin_str = find_job_json.get("stdin").value;
  client_cwd = find_job_json.get("client_cwd").value;
  if (wcl::is_relative(client_cwd)) {
    wcl::log::fatal("FindJobRequest::FindJobRequest: client_cwd cannot be relative. found: '%s'",
                    client_cwd.c_str());
  }

  // Read the input files, and compute the directory hashes as we go.
  for (const auto &input_file : find_job_json.get("input_files").children) {
    std::string path = input_file.second.get("path").value;
    // Canonicalize all input file paths to sandbox-absolute paths.
    // These paths are relative to the sandbox cwd.
    if (wcl::is_relative(path)) {
      path = wcl::join_paths(wakeroot, path);
    }
    Hash256 hash = Hash256::from_hex(input_file.second.get("hash").value);
    bloom.add_hash(hash);
    visible[std::move(path)] = hash;
  }

  // Now accumulate the hashables in the directory.
  std::unordered_map<std::string, std::string> dirs;
  // NOTE: `visible` is already sorted because its an std::map.
  // this means that we'll accumulate directories correctly.
  for (const auto &input : visible) {
    auto pair = wcl::parent_and_base(input.first);
    if (!pair) continue;
    std::string parent = std::move(pair->first);
    std::string base = std::move(pair->second);
    dirs[parent] += base;
    dirs[parent] += ":";
  }

  // Now actually perform those hashes
  for (auto dir : dirs) {
    dir_hashes[dir.first] = Hash256::blake2b(dir.second);
  }

  // When outputting files we need to map sandbox dirs to output dirs.
  // Collect those redirects here.
  for (const auto &dir_redirect : find_job_json.get("dir_redirects").children) {
    // Canonicalize all sanbox directories to sandbox-absolute paths.
    // These paths are relative to the sandbox cwd.
    std::string dir = dir_redirect.first;
    if (wcl::is_relative(dir)) {
      dir = wcl::join_paths(wakeroot, dir);
    }
    auto dir_range = wcl::make_filepath_range(dir);

    // Canonicalize all client directories to client-absolute paths.
    // These paths are relative to the client_cwd.
    std::string client_dir = dir_redirect.second.value;
    if (wcl::is_relative(client_dir)) {
      client_dir = wcl::join_paths(client_cwd, client_dir);
    }
    dir_redirects.move_emplace(dir_range.begin(), dir_range.end(), client_dir);
  }
}

JAST FindJobRequest::to_json() const {
  JAST json(JSON_OBJECT);
  json.add("wakeroot", wakeroot);
  json.add("cwd", cwd);
  json.add("command_line", command_line);
  json.add("envrionment", envrionment);
  json.add("stdin", stdin_str);
  json.add("client_cwd", client_cwd);

  JAST input_files(JSON_ARRAY);
  for (const auto &input_file : visible) {
    JAST input_entry(JSON_OBJECT);
    input_entry.add("path", input_file.first);
    input_entry.add("hash", input_file.second.to_hex());
    input_files.add("", std::move(input_entry));
  }
  json.add("input_files", std::move(input_files));

  JAST dir_redirects_json(JSON_OBJECT);
  dir_redirects.for_each(
      [&dir_redirects_json](const std::vector<std::string> &prefix, const std::string &value) {
        std::string path = "/";
        path += wcl::join('/', prefix.begin(), prefix.end());
        dir_redirects_json.add(path, value);
      });

  json.add("dir_redirects", std::move(dir_redirects_json));

  return json;
}

FindJobResponse::FindJobResponse(JAST json) {
  JAST found = json.get("found");
  if (found.kind != JSON_TRUE) {
    match = {};
    return;
  }
  match = wcl::make_some<MatchingJob>(json.get("match"));
}

JAST FindJobResponse::to_json() const {
  JAST json(JSON_OBJECT);

  if (!match) {
    json.add_bool("found", false);
    return json;
  }

  json.add_bool("found", true);
  json.add("match", match->to_json());
  return json;
}

}  // namespace job_cache
