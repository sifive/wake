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
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <unordered_map>

#include "json/json5.h"
#include "util/diagnostic.h"
#include "wcl/filepath.h"
#include "wcl/optional.h"
#include "wcl/result.h"

// Expands a string as echo would.
static std::string shell_expand(const std::string& to_expand) {
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
    std::string shell_string = "echo " + to_expand;

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

  // Remove the newline added by echo
  // echo -n doesn't work MacOS
  acc.pop_back();

  // Return the string we got back
  return acc;
}

// Find the default location for the user level wake config
static std::string default_user_config() {
  // If XDG_CONFIG_HOME is set use it, otherwise use home
  char* xdg_config_home = getenv("XDG_CONFIG_HOME");
  char* home_dir = getenv("HOME");

  if (home_dir == nullptr) {
    std::cerr << "$HOME is not set!" << std::endl;
    exit(EXIT_FAILURE);
  }

  std::string prefix = wcl::join_paths(std::string(home_dir), ".config");

  if (xdg_config_home != nullptr) {
    prefix = std::string(xdg_config_home);
  }

  return wcl::join_paths(prefix, "wake.json");
}

enum class ReadJsonFileError {
  BadFile,
  InvalidJson,
};

static wcl::result<JAST, std::pair<ReadJsonFileError, std::string>> read_json_file(
    const std::string& path) {
  std::ifstream file(path);
  if (!file) {
    return wcl::make_error<JAST, std::pair<ReadJsonFileError, std::string>>(
        ReadJsonFileError::BadFile, "Failed  to read '" + path + "'");
  }

  std::stringstream buff;
  buff << file.rdbuf();
  std::string contents = buff.str();

  JAST json;
  std::stringstream errors;
  if (!JAST::parse(contents, errors, json)) {
    return wcl::make_error<JAST, std::pair<ReadJsonFileError, std::string>>(
        ReadJsonFileError::InvalidJson, path + " must be a valid JSON object: " + errors.str());
  }

  return wcl::result_value<std::pair<ReadJsonFileError, std::string>>(std::move(json));
}

// Returns a list of keys present in *json* that are not listed in *keys*
// *json* must be of kind JSON_OBJECT to return any results.
static std::vector<std::string> find_disallowed_keys(const JAST& json,
                                                     const std::set<std::string>& keys) {
  std::vector<std::string> disallowed = {};

  if (json.kind != JSON_OBJECT) {
    return disallowed;
  }

  for (const auto& entry : json.children) {
    if (keys.count(entry.first) > 0) {
      continue;
    }
    disallowed.push_back(entry.first);
  }

  return disallowed;
}

#define POLCIY_STATIC_DEFINES(Policy)           \
  constexpr const char* Policy::key;            \
  constexpr bool Policy::allowed_in_wakeroot;   \
  constexpr bool Policy::allowed_in_userconfig; \
  constexpr typename Policy::type Policy::*Policy::value;

/********************************************************************
 * Definition boilerplate
 *********************************************************************/

POLCIY_STATIC_DEFINES(UserConfigPolicy)
POLCIY_STATIC_DEFINES(VersionPolicy)
POLCIY_STATIC_DEFINES(LogHeaderPolicy)
POLCIY_STATIC_DEFINES(LogHeaderSourceWidthPolicy)
POLCIY_STATIC_DEFINES(LabelFilterPolicy)
POLCIY_STATIC_DEFINES(SharedCacheMaxSize)
POLCIY_STATIC_DEFINES(SharedCacheLowSize)
POLCIY_STATIC_DEFINES(LogHeaderAlignPolicy)

/********************************************************************
 * Non-Trivial Defaults
 *********************************************************************/

UserConfigPolicy::UserConfigPolicy() { user_config = shell_expand(default_user_config()); }

/********************************************************************
 * Setter implementations
 ********************************************************************/

void VersionPolicy::set(VersionPolicy& p, const JAST& json) {
  auto json_version = json.expect_string();
  if (json_version) {
    p.version = *json_version;
  }
}

void UserConfigPolicy::set(UserConfigPolicy& p, const JAST& json) {
  auto json_user_config = json.expect_string();
  if (json_user_config) {
    p.user_config = shell_expand(*json_user_config);
  }
}

void LogHeaderPolicy::set(LogHeaderPolicy& p, const JAST& json) {
  auto json_log_header = json.expect_string();
  if (json_log_header) {
    p.log_header = *json_log_header;
  }
}

void LogHeaderSourceWidthPolicy::set(LogHeaderSourceWidthPolicy& p, const JAST& json) {
  auto json_log_header_soruce_width = json.expect_integer();
  if (json_log_header_soruce_width) {
    p.log_header_source_width = *json_log_header_soruce_width;
  }
}

void SharedCacheMaxSize::set(SharedCacheMaxSize& p, const JAST& json) {
  auto json_max_cache_size = json.expect_integer();
  if (json_max_cache_size) {
    p.max_cache_size = *json_max_cache_size;
  }
}

void SharedCacheLowSize::set(SharedCacheLowSize& p, const JAST& json) {
  auto json_low_cache_size = json.expect_integer();
  if (json_low_cache_size) {
    p.low_cache_size = *json_low_cache_size;
  }
}

void LogHeaderAlignPolicy::set(LogHeaderAlignPolicy& p, const JAST& json) {
  auto json_log_header_algih = json.expect_boolean();
  if (json_log_header_algih) {
    p.log_header_align = *json_log_header_algih;
  }
}

/********************************************************************
 * Core Implementation
 ********************************************************************/

static std::unique_ptr<WakeConfig> _config;

bool WakeConfig::init(const std::string& wakeroot_path, const WakeConfigOverrides& overrides) {
  if (_config != nullptr) {
    std::cerr << "Cannot initialize config twice" << std::endl;
    exit(1);
  }

  // Only keys that may be specified in .wakeroot
  std::set<std::string> wakeroot_allowed_keys = WakeConfigImplFull::wakeroot_allowed_keys();

  // Only keys that may be specified in the user config
  std::set<std::string> user_config_allowed_keys = WakeConfigImplFull::userconfig_allowed_keys();

  // Get a default WakeConfig, we can't use std::make_unique because it doesn't have access
  // to our default constructor. Thus we are forced to use `new`
  _config = std::unique_ptr<WakeConfig>(new WakeConfig());

  // Parse .wakeroot
  auto wakeroot_res = read_json_file(wakeroot_path);
  if (!wakeroot_res) {
    std::cerr << "Failed to load .wakeroot: " << wakeroot_res.error().second << std::endl;
    return false;
  }

  JAST wakeroot_json = std::move(*wakeroot_res);

  // Check for disallowed keys
  {
    auto disallowed_keys = find_disallowed_keys(wakeroot_json, wakeroot_allowed_keys);
    for (const auto& key : disallowed_keys) {
      std::cerr << wakeroot_path << ": Key '" << key << "' may not be set in .wakeroot";
      if (user_config_allowed_keys.count(key) > 0) {
        std::cerr << " but it may be set in user config";
      }
      std::cerr << "." << std::endl;
    }
  }

  // Parse values from .wakeroot
  _config->set_all<WakeConfigProvenance::WakeRoot>(wakeroot_json);

  // Parse user config
  auto user_config_res = read_json_file(_config->user_config);
  if (!user_config_res) {
    // When parsing the user config, its fine if the file is missing
    // but we should report all other errors.
    if (user_config_res.error().first != ReadJsonFileError::BadFile) {
      std::cerr << _config->user_config << ": " << user_config_res.error().second << std::endl;
      return false;
    }

    // since the user config was missig, ignore it and return the config thus far
    // before we do we need to handle overrides
    _config->override_all(overrides);
    return true;
  }

  JAST user_config_json = std::move(*user_config_res);

  // Check for disallowed keys
  {
    auto disallowed_keys = find_disallowed_keys(user_config_json, user_config_allowed_keys);
    for (const auto& key : disallowed_keys) {
      std::cerr << _config->user_config << ": Key '" << key << "' may not be set in user config";
      if (wakeroot_allowed_keys.count(key) > 0) {
        std::cerr << " but it may be set in .wakeroot";
      }
      std::cerr << "." << std::endl;
    }
  }

  // Parse values from the user config
  _config->set_all<WakeConfigProvenance::UserConfig>(user_config_json);

  // Finally apply command line overrides
  _config->override_all(overrides);

  return true;
}

const WakeConfig* const WakeConfig::get() {
  if (_config == nullptr) {
    std::cerr << "Cannot retrieve config before initialization" << std::endl;
    assert(false);
  }
  return _config.get();
}

std::ostream& operator<<(std::ostream& os, const WakeConfig& config) {
  config.emit(os);
  return os;
}
