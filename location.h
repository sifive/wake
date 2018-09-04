#ifndef LOCATION_H
#define LOCATION_H

#include <string>

struct Coordinates {
  int row, column;
  Coordinates(int r = 1, int c = 1) : row(r), column(c) { }

  bool operator < (const Coordinates &c) const {
    if (row == c.row) { return column < c.column; }
    return row < c.row;
  }
  bool operator >  (const Coordinates &c) const { return   c < *this;  }
  bool operator <= (const Coordinates &c) const { return !(c < *this); }
  bool operator >= (const Coordinates &c) const { return !(*this < c); }
};

struct Location {
  const char *file;
  Coordinates start, end;

  std::string str() const;
  Location(const char *file_ = "<null>", Coordinates start_ = Coordinates(), Coordinates end_ = Coordinates())
    : file(file_), start(start_), end(end_) { }

  bool contains(const Location &loc) const {
    return file == loc.file && start <= loc.start && loc.end <= end;
  }
};

#endif
