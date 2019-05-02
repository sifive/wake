/*
 * Copyright 2019 SiFive, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
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
#include <sstream>
#include <stdio.h>

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
  FILE *f;
  char buf[40];
  size_t get;

  if (l->start.bytes >= 0 &&
      l->filename[0] != '<' &&
      l->start.row == l->end.row &&
      l->end.column >= l->start.column &&
      (get = l->end.column - l->start.column + 1) < sizeof(buf) &&
      (f = fopen(l->filename, "r")) != 0) {
    if (fseek(f, l->start.bytes, SEEK_SET) != -1 && fread(&buf[0], 1, get, f) == get) {
      buf[get] = 0;
    } else {
      buf[0] = 0;
    }
    fclose(f);
  } else {
    buf[0] = 0;
  }

  if (buf[0]) os << "'" << buf << "' (";
  os << l->file();
  if (buf[0]) os << ")";
  return os;
}
