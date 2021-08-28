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

#ifndef LOCATION_H
#define LOCATION_H

#include <ostream>

struct Coordinates {
  int row, column;
  long bytes;
  Coordinates(int r = 1, int c = 1, long b = -1) : row(r), column(c), bytes(b) { }

  bool operator == (const Coordinates &c) const {
    return row == c.row && column == c.column;
  }
  bool operator != (const Coordinates &c) const { return !(c == *this); }

  bool operator < (const Coordinates &c) const {
    if (row == c.row) { return column < c.column; }
    return row < c.row;
  }
  bool operator >  (const Coordinates &c) const { return   c < *this;  }
  bool operator <= (const Coordinates &c) const { return !(c < *this); }
  bool operator >= (const Coordinates &c) const { return !(*this < c); }

  Coordinates operator + (int x) const { return Coordinates(row, column+x, bytes+x); }
  Coordinates operator - (int x) const { return Coordinates(row, column-x, bytes-x); }
};

struct Location;

struct TextLocation {
  const Location *l;
  TextLocation(const Location *l_) : l(l_) { }
};

std::ostream & operator << (std::ostream &os, TextLocation location);

struct FileLocation {
  const Location *l;
  FileLocation(const Location *l_) : l(l_) { }
};

std::ostream & operator << (std::ostream &os, FileLocation location);

struct Location {
  const char *filename;
  Coordinates start, end;

  Location(const char *filename_)
    : filename(filename_) { }
  Location(const char *filename_, Coordinates start_, Coordinates end_)
    : filename(filename_), start(start_), end(end_) { }

  bool contains(const Location &loc) const {
    return filename == loc.filename && start <= loc.start && loc.end <= end;
  }

  FileLocation file() const { return FileLocation(this); }
  TextLocation text() const { return TextLocation(this); }
};

#define LOCATION Location(__FILE__, Coordinates(__LINE__), Coordinates(__LINE__))

#endif
