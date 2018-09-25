#include "location.h"
#include <sstream>

std::ostream & operator << (std::ostream &os, const Location &location) {
  os << location.file << ":";
  if (location.start.row == location.end.row) os << location.start.row;
  else os << "[" << location.start.row << "-" << location.end.row << "]";
  os << ":";
  if (location.start.column == location.end.column) os << location.start.column;
  else os << "[" << location.start.column << "-" << location.end.column << "]";
  return os;
}

std::string Location::str() const {
  std::stringstream str;
  str << *this;
  return str.str();
}
