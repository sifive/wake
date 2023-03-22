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

#include <string>

namespace config {

struct WakeConfig {
  const std::string version = "";
  const std::string user_config = "";

  WakeConfig() = delete;
  WakeConfig(const WakeConfig&) = delete;
  WakeConfig(const WakeConfig&&) = delete;

 private:
  WakeConfig(std::string version, std::string user_config)
      : version(version), user_config(user_config) {}

  friend bool init(const std::string& wakeroot);
};

std::ostream& operator<<(std::ostream& os, const WakeConfig& config);

bool init(const std::string& wakeroot);
const WakeConfig* const get();

}  // namespace config
