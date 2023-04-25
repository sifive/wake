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

#pragma once

#include <map>
#include <set>
#include <string>

#include "json/json5.h"

// For future work, currently empty
struct WakeConfigOverrides {};

template <class T>
using Override = wcl::optional<T> WakeConfigOverrides::*;

/********************************************************************
 * Polcies
 *********************************************************************/

struct VersionPolicy {
  static constexpr const char* key = "version";
  static constexpr bool allowed_in_wakeroot = true;
  static constexpr bool allowed_in_userconfig = false;
  std::string version = "";
  static constexpr std::string VersionPolicy::*value = &VersionPolicy::version;
  static constexpr Override<std::string> override_value = nullptr;

  VersionPolicy() = default;
  static void set(VersionPolicy& p, const JAST& json);
};

struct UserConfigPolicy {
  static constexpr const char* key = "user_config";
  static constexpr bool allowed_in_wakeroot = true;
  static constexpr bool allowed_in_userconfig = false;
  std::string user_config;
  static constexpr std::string UserConfigPolicy::*value = &UserConfigPolicy::user_config;
  static constexpr Override<std::string> override_value = nullptr;

  UserConfigPolicy();
  static void set(UserConfigPolicy& p, const JAST& json);
};

/********************************************************************
 * Generic WakeConfig implementation
 *********************************************************************/

enum class WakeConfigProvinence { Default, Wakeroot, Userconfig, CommandLine };

static inline const char* to_string(WakeConfigProvinence p) {
  switch (p) {
    case WakeConfigProvinence::Default:
      return "Default";
    case WakeConfigProvinence::Wakeroot:
      return "Wakeroot";
    case WakeConfigProvinence::Userconfig:
      return "Userconfig";
    case WakeConfigProvinence::CommandLine:
      return "Commandline";
  }
  return "Default";
}

template <class... Polcies>
struct WakeConfigImpl : public Polcies... {
  WakeConfigImpl(const WakeConfigImpl&) = delete;
  WakeConfigImpl(WakeConfigImpl&&) = delete;
  WakeConfigImpl() = default;

  std::map<std::string, WakeConfigProvinence> provinence;

 private:
  struct void_t {};

  template <class... Args>
  static void call_all(Args... x) {}

  template <class P>
  void_t emit_each(std::ostream& os) const {
    auto iter = provinence.find(P::key);
    WakeConfigProvinence p = WakeConfigProvinence::Default;
    if (iter != provinence.end()) {
      p = iter->second;
    }
    os << "  " << P::key << " = '" << this->*P::value << "' (" << to_string(p) << ")" << std::endl;
    return {};
  }

  template <class P>
  static void_t add_wakeroot_key(std::set<std::string>& out) {
    if (P::allowed_in_wakeroot) {
      out.emplace(P::key);
    }
    return {};
  }

  template <class P>
  static void_t add_userconfig_key(std::set<std::string>& out) {
    if (P::allowed_in_userconfig) {
      out.emplace(P::key);
    }
    return {};
  }

  template <WakeConfigProvinence p, class P>
  void_t set_policy(const JAST& json) {
    // The current semantics want us to
    if (p == WakeConfigProvinence::Wakeroot && !P::allowed_in_wakeroot) {
      return {};
    }
    if (p == WakeConfigProvinence::Userconfig && !P::allowed_in_userconfig) {
      return {};
    }
    auto opt_value = json.get_opt(P::key);
    if (opt_value) {
      provinence[P::key] = p;
      P::set(*this, **opt_value);
    }
    return {};
  }

  template <class P>
  void_t override_policy(const WakeConfigOverrides& overrides) {
    if (P::override_value && overrides.*P::override_value) {
      provinence[P::key] = WakeConfigProvinence::CommandLine;
      this->*P::value = *(overrides.*P::override_value);
    }
    return {};
  }

 public:
  void emit(std::ostream& os) const {
    os << "Wake config:" << std::endl;
    call_all(emit_each<Polcies>(os)...);
  }

  static std::set<std::string> wakeroot_allowed_keys() {
    std::set<std::string> out;
    call_all(add_wakeroot_key<Polcies>(out)...);
    return out;
  }

  static std::set<std::string> userconfig_allowed_keys() {
    std::set<std::string> out;
    call_all(add_userconfig_key<Polcies>(out)...);
    return out;
  }

  template <WakeConfigProvinence p>
  void set_all(const JAST& json) {
    call_all(set_policy<p, Polcies>(json)...);
  }

  void override_all(const WakeConfigOverrides& overrides) {
    call_all(override_policy<Polcies>(overrides)...);
  }
};

using WakeConfigImplFull = WakeConfigImpl<UserConfigPolicy, VersionPolicy>;

struct WakeConfig final : public WakeConfigImplFull {
  static bool init(const std::string& wakeroot_path, const WakeConfigOverrides& overrides);
  static const WakeConfig* const get();

  WakeConfig(const WakeConfig&) = delete;
  WakeConfig(WakeConfig&&) = delete;

 private:
  WakeConfig() = default;
};

std::ostream& operator<<(std::ostream& os, const WakeConfig& config);
