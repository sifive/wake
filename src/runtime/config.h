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

#include <re2/re2.h>

#include <map>
#include <set>
#include <string>

#include "json/json5.h"

// For future work, currently empty
struct WakeConfigOverrides {
  wcl::optional<std::string> log_header;
  wcl::optional<int64_t> log_header_source_width;

  // if label_filter == {} then we don't override
  // if label_filter == {{}} then we do override but the filter accepts everything
  // if label_filter == {{filter}} then
  // these semantics fall out of the normal way overrides work, this comment
  // is just to make this unusual type's meaning clear.
  wcl::optional<wcl::optional<std::string>> label_filter;
};

template <class T>
using Override = wcl::optional<T> WakeConfigOverrides::*;

/********************************************************************
 * Polcies
 *********************************************************************/

struct VersionPolicy {
  using type = std::string;
  using input_type = type;
  static constexpr const char* key = "version";
  static constexpr bool allowed_in_wakeroot = true;
  static constexpr bool allowed_in_userconfig = false;
  type version = "";
  static constexpr type VersionPolicy::*value = &VersionPolicy::version;
  static constexpr Override<input_type> override_value = nullptr;

  VersionPolicy() = default;
  static void set(VersionPolicy& p, const JAST& json);
  static void set_input(VersionPolicy& p, const input_type& v) { p.*value = v; }
  static void emit(const VersionPolicy& p, std::ostream& os) { os << p.*value; }
};

struct UserConfigPolicy {
  using type = std::string;
  using input_type = type;
  static constexpr const char* key = "user_config";
  static constexpr bool allowed_in_wakeroot = true;
  static constexpr bool allowed_in_userconfig = false;
  type user_config;
  static constexpr type UserConfigPolicy::*value = &UserConfigPolicy::user_config;
  static constexpr Override<input_type> override_value = nullptr;

  UserConfigPolicy();
  static void set(UserConfigPolicy& p, const JAST& json);
  static void set_input(UserConfigPolicy& p, const input_type& v) { p.*value = v; }
  static void emit(const UserConfigPolicy& p, std::ostream& os) { os << p.*value; }
};

struct LogHeaderPolicy {
  using type = std::string;
  using input_type = type;
  static constexpr const char* key = "log_header";
  static constexpr bool allowed_in_wakeroot = true;
  static constexpr bool allowed_in_userconfig = true;
  type log_header = "[$stream] $source: ";
  static constexpr type LogHeaderPolicy::*value = &LogHeaderPolicy::log_header;
  static constexpr Override<input_type> override_value = &WakeConfigOverrides::log_header;

  LogHeaderPolicy() = default;
  static void set(LogHeaderPolicy& p, const JAST& json);
  static void set_input(LogHeaderPolicy& p, const input_type& v) { p.*value = v; }
  static void emit(const LogHeaderPolicy& p, std::ostream& os) { os << p.*value; }
};

struct LogHeaderSourceWidthPolicy {
  using type = int64_t;
  using input_type = type;
  static constexpr const char* key = "log_header_source_width";
  static constexpr bool allowed_in_wakeroot = true;
  static constexpr bool allowed_in_userconfig = true;
  type log_header_source_width = 25;
  static constexpr type LogHeaderSourceWidthPolicy::*value =
      &LogHeaderSourceWidthPolicy::log_header_source_width;
  static constexpr Override<input_type> override_value =
      &WakeConfigOverrides::log_header_source_width;

  LogHeaderSourceWidthPolicy() {}
  static void set(LogHeaderSourceWidthPolicy& p, const JAST& json);
  static void set_input(LogHeaderSourceWidthPolicy& p, const input_type& v) { p.*value = v; }
  static void emit(const LogHeaderSourceWidthPolicy& p, std::ostream& os) { os << p.*value; }
};

struct LabelFilterPolicy {
  using type = std::unique_ptr<re2::RE2>;
  using input_type = wcl::optional<std::string>;
  static constexpr const char* key = "label_filter";
  static constexpr bool allowed_in_wakeroot = false;
  static constexpr bool allowed_in_userconfig = false;
  type label_filter;
  static constexpr type LabelFilterPolicy::*value = &LabelFilterPolicy::label_filter;
  static constexpr Override<input_type> override_value = &WakeConfigOverrides::label_filter;

  LabelFilterPolicy() : label_filter(std::make_unique<re2::RE2>(".*")) {}

  static void set(LabelFilterPolicy& p, const JAST& json) {}
  static void set_input(LabelFilterPolicy& p, const input_type& v) {
    if (v) {
      p.*value = std::make_unique<re2::RE2>(*v);
    }
  }
  static void emit(const LabelFilterPolicy& p, std::ostream& os) {
    os << p.label_filter->pattern();
  }
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

  static void call_all() {}

  template <class F, class... Args>
  static void call_all(F f, Args... fs) {
    f();
    call_all(fs...);
  }

  template <class P>
  void_t emit_each(std::ostream& os) const {
    auto iter = provenance.find(P::key);
    auto p = WakeConfigProvenance::Default;
    if (iter != provenance.end()) {
      p = iter->second;
    }
    os << "  " << P::key << " = '";
    P::emit(*this, os);
    os << "' (" << to_string(p) << ")" << std::endl;
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
    if (p == WakeConfigProvenance::WakeRoot && !P::allowed_in_wakeroot) {
      return {};
    }
    if (p == WakeConfigProvenance::UserConfig && !P::allowed_in_userconfig) {
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
      P::set_input(*this, *(overrides.*P::override_value));
    }
    return {};
  }

 protected:
  static std::set<std::string> wakeroot_allowed_keys() {
    std::set<std::string> out;
    call_all([&out]() { add_wakeroot_key<Polcies>(out); }...);
    return out;
  }

  static std::set<std::string> userconfig_allowed_keys() {
    std::set<std::string> out;
    call_all([&out]() { add_userconfig_key<Polcies>(out); }...);
    return out;
  }

  template <WakeConfigProvenance p>
  void set_all(const JAST& json) {
    call_all([this, &json]() { set_policy<p, Polcies>(json); }...);
  }

  void override_all(const WakeConfigOverrides& overrides) {
    call_all([this, &overrides]() { override_policy<Polcies>(overrides); }...);
  }

 public:
  void emit(std::ostream& os) const {
    os << "Wake config:" << std::endl;
    call_all([this, &os]() { emit_each<Polcies>(os); }...);
  }
};

using WakeConfigImplFull = WakeConfigImpl<UserConfigPolicy, VersionPolicy, LogHeaderPolicy,
                                          LogHeaderSourceWidthPolicy, LabelFilterPolicy>;

struct WakeConfig final : public WakeConfigImplFull {
  static bool init(const std::string& wakeroot_path, const WakeConfigOverrides& overrides);
  static const WakeConfig* const get();

  WakeConfig(const WakeConfig&) = delete;
  WakeConfig(WakeConfig&&) = delete;

 private:
  WakeConfig() = default;
};

std::ostream& operator<<(std::ostream& os, const WakeConfig& config);
