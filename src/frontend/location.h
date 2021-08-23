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

#include <string>
#include <ostream>

struct Coordinates {
    int row, column;
    long bytes;
    Coordinates(int r = 1, int c = 1, long b = -1) : row(r), column(c), bytes(b) { }
};

struct Location {
    const char *filename;
    Coordinates start, end;

    Location(const char *filename_)
      : filename(filename_) { }
    Location(const char *filename_, Coordinates start_, Coordinates end_)
      : filename(filename_), start(start_), end(end_) { }
};

std::ostream & operator << (std::ostream &os, const Location &location);

#endif
