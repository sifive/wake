#include "location.h"
#include <sstream>

std::string Location::str() const {
  std::stringstream str;
  str << file << ":";
  if (start.row == end.row) str << start.row;
  else str << "[" << start.row << "-" << end.row << "]";
  str << ":";
  if (start.column == end.column) str << start.column;
  else str << "[" << start.column << "-" << end.column << "]";
  return str.str();
}
