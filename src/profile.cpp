/*
 * Copyright 2019 SiFive, Inc.
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

#include "profile.h"
#include "json5.h"
#include <fstream>
#include <iostream>
#include <string.h>
#include <errno.h>

static unsigned dump_tree(std::ostream &os, const std::string &name, const Profile *node) {
  unsigned value = node->count;
  os << "{";
  if (!node->children.empty()) {
    os << "\"children\":[";
    bool first = true;
    for (auto &x : node->children) {
      if (!first) os << ",";
      first = false;
      value += dump_tree(os, x.first, &x.second);
    }
    os << "],";
  }
  os << "\"name\":\"" << json_escape(name)
     << "\",\"value\":" << value
     << "}";
  return value;
}

void Profile::report(const char *file) const {
  if (file) {
    std::ofstream f(file, std::ios_base::trunc);
    if (!f.fail()) {
      dump_tree(f, "root", this);
      f << std::endl; // flushes
    }
    if (f.fail()) {
      std::cerr << "Saving profile trace to '" << file << "': " << strerror(errno) << std::endl;
    }
  }
}
