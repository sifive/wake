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

#include <job_cache/daemon_cache.h>
#include <json/json5.h>
#include <wcl/tracing.h>

// TODO: It would be nice if this could be wcl::optional<const V&> but wcl::optional does
// not play nice with references at this point in time. We might want to explicitly
// special case that at some point to gain that very powerful feature. It can be optimized
// to a single pointer that way as well.
template <class K, class V>
V* get_mut(std::map<K, V>& map, const K& key) {
  auto iter = map.find(key);
  if (iter == map.end()) {
    return nullptr;
  }

  return &iter->second;
}

struct Argument {
  std::string key;
  wcl::optional<std::string> value;
  explicit Argument(std::string key) : key(key), value() {}
};

struct Flag {
  std::string key;
  bool value = false;
  explicit Flag(std::string key) : key(key), value(false) {}
};

struct ArgParser {
  std::map<std::string, Argument*> arguments;
  std::map<std::string, Flag*> flags;

  ArgParser& arg(Argument& arg) {
    arguments.emplace(arg.key, &arg);
    return *this;
  }

  ArgParser& flag(Flag& arg) {
    flags.emplace(arg.key, &arg);
    return *this;
  }

  void parse(int argc, char** argv) {
    auto begin = argv + 1;
    auto end = argv + argc;
    while (begin < end) {
      std::string arg = *begin;
      if (auto* argument = get_mut<std::string, Argument*>(arguments, arg)) {
        (*argument)->value = wcl::make_some<std::string>(*++begin);
        ++begin;
        continue;
      }
      if (auto* flag = get_mut<std::string, Flag*>(flags, arg)) {
        (*flag)->value = true;
        ++begin;
        continue;
      }
      std::cerr << "Encountered '" << arg << "' which is not a recognized option" << std::endl;
    }
  }
};

int main(int argc, char** argv) {
  Argument cache_dir("--cache-dir");
  Argument bulk_logging_dir("--bulk-logging-dir");
  Argument eviction_policy("--eviction-type");
  Argument low_cache_size("--low-cache-size");
  Argument max_cache_size("--max-cache-size");
  Argument seconds_to_live("--seconds-to-live");
  ArgParser parser;
  parser.arg(cache_dir)
      .arg(bulk_logging_dir)
      .arg(eviction_policy)
      .arg(low_cache_size)
      .arg(max_cache_size)
      .arg(seconds_to_live);

  parser.parse(argc, argv);

  if (!eviction_policy.value) {
    std::cerr << "An eviction policy must be specified with " << eviction_policy.key << std::endl;
    return 1;
  }

  if (!cache_dir.value) {
    std::cerr << "A cache directory must be specified with " << cache_dir.key << std::endl;
    return 1;
  }

  if (!bulk_logging_dir.value) {
    std::cerr << "A bulk logging dir must be specified with " << cache_dir.key << std::endl;
  }

  job_cache::EvictionConfig config;
  if (*eviction_policy.value != "ttl" && *eviction_policy.value != "lru") {
    std::cerr << "'" << eviction_policy.value << "' is not a valid eviction policy" << std::endl;
    return 1;
  }

  if (*eviction_policy.value == "ttl") {
    if (!seconds_to_live.value) {
      std::cerr << "If the `ttl` eviction policy is selected, " << seconds_to_live.key
                << " must also be set" << std::endl;
    }
    try {
      config = job_cache::EvictionConfig::ttl_config(std::stoll(*seconds_to_live.value));
    } catch (...) {
      std::cerr << "`" << *seconds_to_live.value << "` is not a valid number of seconds"
                << std::endl;
      return 1;
    }
  }

  if (*eviction_policy.value == "lru") {
    if (!low_cache_size.value) {
      std::cerr << "If 'lru' eviction policy is selected, " << low_cache_size.key
                << " must also be set";
    }
    if (!max_cache_size.value) {
      std::cerr << "If 'lru' eviction policy is selected, " << max_cache_size.key
                << " must also be set";
    }
    try {
      config = job_cache::EvictionConfig::lru_config(std::stoll(*low_cache_size.value),
                                                     std::stoll(*max_cache_size.value));
    } catch (...) {
      std::cerr << "`" << *seconds_to_live.value << "` is not a valid number of seconds"
                << std::endl;
      return 1;
    }
  }

  int status = 1;
  {
    job_cache::DaemonCache dcache(std::move(*cache_dir.value), std::move(*bulk_logging_dir.value),
                                  config);
    status = dcache.run();
  }

  return status;
}
