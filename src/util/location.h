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

#include <wcl/hash.h>

#include <ostream>

struct Coordinates {
  int row, column;
  Coordinates(int r = 1, int c = 1) : row(r), column(c) {}

  bool operator==(const Coordinates &c) const { return row == c.row && column == c.column; }
  bool operator!=(const Coordinates &c) const { return !(c == *this); }

  bool operator<(const Coordinates &c) const {
    if (row == c.row) {
      return column < c.column;
    }
    return row < c.row;
  }
  bool operator>(const Coordinates &c) const { return c < *this; }
  bool operator<=(const Coordinates &c) const { return !(c < *this); }
  bool operator>=(const Coordinates &c) const { return !(*this < c); }

  Coordinates operator+(int x) const { return Coordinates(row, column + x); }
  Coordinates operator-(int x) const { return Coordinates(row, column - x); }
};

template <>
struct std::hash<Coordinates> {
  size_t operator()(Coordinates const &coord) const noexcept {
    return wcl::hash_combine(std::hash<int>{}(coord.row), std::hash<int>{}(coord.column));
  }
};

struct Location {
  std::string filename;
  Coordinates start, end;

  Location(const char *filename_) : filename(filename_) {}
  Location(const char *filename_, Coordinates start_, Coordinates end_)
      : filename(filename_), start(start_), end(end_) {}

  bool contains(const Location &loc) const {
    return filename == loc.filename && start <= loc.start && loc.end <= end;
  }

  bool operator<(const Location &l) const {
    if (filename != l.filename) return filename < l.filename;
    if (start != l.start) return start < l.start;
    return end < l.end;
  }

  bool operator==(const Location &l) const { return this->contains(l) && l.contains(*this); }

  bool operator!=(const Location &l) const { return !(*this == l); }
};

template <>
struct std::hash<Location> {
  size_t operator()(Location const &loc) const noexcept {
    auto hash = wcl::hash_combine(std::hash<std::string>{}(loc.filename),
                                  std::hash<Coordinates>{}(loc.start));
    return wcl::hash_combine(hash, std::hash<Coordinates>{}(loc.end));
  }
};

#define LOCATION Location(__FILE__, Coordinates(__LINE__), Coordinates(__LINE__))

std::ostream &operator<<(std::ostream &os, const Location &location);

#endif
