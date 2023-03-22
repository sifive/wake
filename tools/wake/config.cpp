/*
 * Copyright 2022 SiFive, Inc.
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

#include "config.h"

#include <sys/wait.h>
#include <unistd.h>

#include <cassert>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <unordered_map>

#include "json/json5.h"
#include "wcl/optional.h"

namespace config {

// TODO: remaining items for the implementation
//   - Document (probably in main readme) the various configuration flags
//   - Move error printing up via wcl::result
//   - Write unit tests for the config unit

static WakeConfig* _config = nullptr;

// Expands a string as echo would.
std::string shell_expand(const std::string& to_expand) {
  constexpr size_t read_side = 0;
  constexpr size_t write_side = 1;

  // Setup a pipe for the child process to write to
  int stdoutPipe[2];
  if (pipe(stdoutPipe) < 0) {
    perror("Failed to allocate eviction pipe");
    exit(EXIT_FAILURE);
  }

  // Fork to get a child process
  pid_t pid = fork();
  if (pid < 0) {
    perror("Failed to fork eviction process");
    exit(EXIT_FAILURE);
  }

  if (pid == 0) {
    // A slight quark is that spaces between seperate parts of `to_expand`
    // will only have one space between them at the end of this. This may
    // catch some people off guard but adding `double` quotes around this
    // would require escaping things and would change expansion. Since
    // the intended use cases will likely not have spaces in them this
    // seems like an ok trade off.
    std::string shell_string = "echo -n " + to_expand;

    // overwrite stdout with the write side of the pipe
    if (dup2(stdoutPipe[write_side], STDOUT_FILENO) == -1) {
      perror("Failed to dup2 stdout pipe for eviction process");
      exit(EXIT_FAILURE);
    }

    // We don't want stdin/stderr messing things up and
    // we no longer need the write/read side of the pipe
    // since stdout *is* the write side of the pipe now.
    close(STDIN_FILENO);
    // close(STDERR_FILENO);
    close(stdoutPipe[read_side]);
    close(stdoutPipe[write_side]);

    // Now replace this process with the shell command but
    // exit and fail if we don't manage to do that.
    execl("/bin/sh", "/bin/sh", "-c", shell_string.c_str(), nullptr);
    perror("Failed to exec /bin/sh");
    exit(EXIT_FAILURE);
  }

  // Make sure to close this side of the pipe so that the shell program doesn't
  // stay open waiting for commands forever.
  close(stdoutPipe[write_side]);

  // Read everything the child process outputs and append
  // it to a string as we go.
  std::string acc;
  char buf[4096];
  while (true) {
    ssize_t size = read(stdoutPipe[read_side], buf, sizeof(buf));
    if (size < 0 && errno == EINTR) {
      continue;
    }
    if (size < 0) {
      perror("failed to exec /bin/sh");
      exit(EXIT_FAILURE);
    }
    if (size == 0) {
      break;
    }
    acc.append(buf, buf + size);
  }

  // We should be done already but let's wait for the child and
  // make sure everything worked well there.
  int status = 1;
  if (waitpid(pid, &status, 0) != pid) {
    perror("waitpid: failed to wait /bin/sh -c echo ...");
    exit(EXIT_FAILURE);
  }
  if (status != 0) {
    fprintf(stderr, "waitpid: /bin/sh -c echo ... failed with non-zero exit status");
    exit(EXIT_FAILURE);
  }

  // Return the string we got back
  return acc;
}

// Find the default location for the user level wake config
std::string default_user_config() {
  // If XDG_CONFIG_HOME is set use it, otherwise use home
  std::string prefix = shell_expand("$XDG_CONFIG_HOME");
  if (prefix == "") {
    prefix = "~";
  }
  return prefix + "/.wake.json";
}

wcl::optional<JAST> read_json_file(const std::string& path) {
  std::ifstream file(path);
  if (!file) {
    // TODO: add this error message back once I have a result type
    // std::cerr << "Failed to read '" << path << "'" << std::endl;
    return {};
  }

  std::stringstream buff;
  buff << file.rdbuf();
  std::string contents = buff.str();

  JAST json;
  std::stringstream errors;
  if (!JAST::parse(contents, errors, json)) {
    std::cerr << path << " must be a valid JSON object: " << errors.str() << std::endl;
    return {};
  }

  return {wcl::in_place_t{}, std::move(json)};
}

bool insert_top_level_values(std::unordered_map<std::string, std::string>& map,
                             const std::string& path) {
  wcl::optional<JAST> json_opt = read_json_file(path);
  if (!json_opt) {
    return false;
  }

  for (const auto& it : (*json_opt).children) {
    if (it.second.kind == JSON_ARRAY || it.second.kind == JSON_OBJECT) {
      std::cerr << "[WARNING]: Key '" << it.first
                << "' is a nested structure. Only flat values suppored." << std::endl;
      continue;
    }
    map[it.first] = it.second.value;
  }

  return true;
}

bool init(const std::string& wakeroot_path) {
  if (_config != nullptr) {
    std::cerr << "Cannot initialize config twice" << std::endl;
    assert(false);
  }

  // These keys may only be specified in .wakeroot
  std::set<std::string> wakeroot_only_keys = {"version", "user_config"};

  // Set the default values for the listed keys
  std::unordered_map<std::string, std::string> wakeroot_config = {
      {"version", ""},
      {"user_config", default_user_config()},
  };

  // These keys may only be specified in the user config
  std::set<std::string> user_config_only_keys = {};

  // Set the default values for the listed keys
  std::unordered_map<std::string, std::string> user_config = {};

  if (!insert_top_level_values(wakeroot_config, wakeroot_path)) {
    std::cerr << "Failed to load .wakeroot" << std::endl;
    return false;
  }

  wakeroot_config["user_config"] = shell_expand(wakeroot_config["user_config"]);
  insert_top_level_values(user_config, wakeroot_config["user_config"]);

  // Verify only allowed keys are set in .wakeroot
  for (const auto& key : user_config_only_keys) {
    auto it = wakeroot_config.find(key);
    if (it == wakeroot_config.end()) {
      continue;
    }
    std::cerr << "[WARNING]: Key '" << key << "' may only be set in user config ("
              << wakeroot_config["user_config"] << "). Ignoring." << std::endl;
    wakeroot_config.erase(key);
  }

  // Verify only allowed keys are set in user config
  // then overlay the user configs on top of the repo configs
  for (const auto& it : user_config) {
    if (wakeroot_only_keys.count(it.first)) {
      std::cerr << "[WARNING]: Key '" << it.first << "' may only be set in .wakeroot. Ignoring."
                << std::endl;
      continue;
    }
    wakeroot_config[it.first] = it.second;
  }

  static WakeConfig wc(wakeroot_config["version"], wakeroot_config["user_config"]);
  _config = &wc;
  return false;
}

const WakeConfig* const get() {
  if (_config == nullptr) {
    std::cerr << "Cannot retrieve config before initialization" << std::endl;
    assert(false);
  }
  return _config;
}

std::ostream& operator<<(std::ostream& os, const WakeConfig& config) {
  os << "Wake config: " << std::endl;
  os << "  version = '" << config.version << "'" << std::endl;
  os << "  user config  = '" << config.user_config << "'" << std::endl;
  return os << std::endl;
}

}  // namespace config
