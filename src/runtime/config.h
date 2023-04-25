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

enum class WakeConfigProvenance { Default, WakeRoot, UserConfig, CommandLine };

static inline const char* to_string(WakeConfigProvenance p) {
  switch (p) {
    case WakeConfigProvenance::Default:
      return "Default";
    case WakeConfigProvenance::WakeRoot:
      return "WakeRoot";
    case WakeConfigProvenance::UserConfig:
      return "UserConfig";
    case WakeConfigProvenance::CommandLine:
      return "Commandline";
  }
  return "Unknown (this is an error, please report this to Wake devs)";
}

template <class... Polcies>
struct WakeConfigImpl : public Polcies... {
  WakeConfigImpl(const WakeConfigImpl&) = delete;
  WakeConfigImpl(WakeConfigImpl&&) = delete;
  WakeConfigImpl() = default;

  std::map<std::string, WakeConfigProvenance> provenance;

 private:
  struct void_t {};

  template <class... Args>
  static void call_all(Args... x) {}

  template <class P>
  void_t emit_each(std::ostream& os) const {
    auto iter = provenance.find(P::key);
    auto p = WakeConfigProvenance::Default;
    if (iter != provenance.end()) {
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

  template <WakeConfigProvenance p, class P>
  void_t set_policy(const JAST& json) {
    if (p == WakeConfigProvenance::Wakeroot && !P::allowed_in_wakeroot) {
      return {};
    }
    if (p == WakeConfigProvenance::Userconfig && !P::allowed_in_userconfig) {
      return {};
    }
    auto opt_value = json.get_opt(P::key);
    if (opt_value) {
      provenance[P::key] = p;
      P::set(*this, **opt_value);
    }
    return {};
  }

  template <class P>
  void_t override_policy(const WakeConfigOverrides& overrides) {
    if (P::override_value && overrides.*P::override_value) {
      provenance[P::key] = WakeConfigProvenance::CommandLine;
      this->*P::value = *(overrides.*P::override_value);
    }
    return {};
  }

 protected:
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

  template <WakeConfigProvenance p>
  void set_all(const JAST& json) {
    call_all(set_policy<p, Polcies>(json)...);
  }

  void override_all(const WakeConfigOverrides& overrides) {
    call_all(override_policy<Polcies>(overrides)...);
  }

 public:
  void emit(std::ostream& os) const {
    os << "Wake config:" << std::endl;
    call_all(emit_each<Polcies>(os)...);
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
