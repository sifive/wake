#ifndef LOCATION_H
#define LOCATION_H

#include <string>

struct Coordinates {
  int row, column;
  Coordinates(int r = 1, int c = 1) : row(r), column(c) { }
};

struct Location {
  const char *file;
  Coordinates start, end;

  std::string str() const;
  Location(const char *file_ = "<null>", Coordinates start_ = Coordinates(), Coordinates end_ = Coordinates())
    : file(file_), start(start_), end(end_) { }
};

#endif
