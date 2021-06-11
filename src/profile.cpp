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

// Open Group Base Specifications Issue 7
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <fstream>
#include <iostream>
#include <string.h>
#include <errno.h>

#include "profile.h"
#include "json5.h"
#include "execpath.h"

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
  size_t colon = name.find(':');
  os << "\"name\":\"" << json_escape(name.substr(0, colon))
     << "\",\"file\":\"" << json_escape(name.substr(colon+2))
     << "\",\"value\":" << value
     << "}";
  return value;
}

void Profile::report(const char *file, const std::string &command) const {
  if (file) {
    std::ofstream f(file, std::ios_base::trunc);
    if (!f.fail()) {
      f << "<meta charset=\"UTF-8\">" << std::endl;
      f << "<style type=\"application/json\" id=\"dataset\">";
      dump_tree(f, command + ": command-line", this);
      f << "</style>" << std::endl;
      std::ifstream html(find_execpath() + "/../share/wake/html/profile.html");
      f << html.rdbuf();
    }
    if (f.fail()) {
      std::cerr << "Saving profile trace to '" << file << "': " << strerror(errno) << std::endl;
    }
  }
}
