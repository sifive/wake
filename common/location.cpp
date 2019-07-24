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

#include "location.h"
#include <fstream>

std::ostream & operator << (std::ostream &os, FileLocation location) {
  const Location *l = location.l;
  os << l->filename << ":";
  if (l->start.row == l->end.row) os << l->start.row;
  else os << "[" << l->start.row << "-" << l->end.row << "]";
  os << ":";
  if (l->start.column == l->end.column) os << l->start.column;
  else os << "[" << l->start.column << "-" << l->end.column << "]";
  return os;
}

std::ostream & operator << (std::ostream &os, TextLocation location) {
  const Location *l = location.l;
  char buf[40];
  size_t get;

  if (l->start.bytes >= 0 &&
      l->filename[0] != '<' &&
      l->start.row == l->end.row &&
      l->end.column >= l->start.column &&
      (get = l->end.column - l->start.column + 1) < sizeof(buf)) {
    std::ifstream ifs(l->filename);
    ifs.seekg(l->start.bytes);
    ifs.read(&buf[0], get);
    if (ifs) {
      buf[get] = 0;
    } else {
      buf[0] = 0;
    }
  } else {
    buf[0] = 0;
  }

  if (buf[0]) os << "'" << buf << "' (";
  os << l->file();
  if (buf[0]) os << ")";
  return os;
}
