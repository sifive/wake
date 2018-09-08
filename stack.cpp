#include "stack.h"
#include <sstream>

Stack::Stack(const std::shared_ptr<Stack> &next_, const Location &location_) : next(next_), location(location_) { }

std::shared_ptr<Stack> Stack::grow(const std::shared_ptr<Stack> &parent, const Location &location) {
  if (parent->location.contains(location)) {
    return parent;
  } else {
    return std::make_shared<Stack>(parent, location);
  }
}

std::ostream & operator << (std::ostream &os, const Stack *stack) {
  for (const Stack *iter = stack; iter; iter = iter->next.get()) {
    os << "  from " << iter->location << std::endl;
  }
  return os;
}

std::string Stack::str() const {
  std::stringstream str;
  str << this;
  return str.str();
}
