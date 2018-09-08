#ifndef STACK_H
#define STACK_H

#include "location.h"
#include <memory>

struct Stack {
  std::shared_ptr<Stack> next;
  Location location;

  static std::shared_ptr<Stack> grow(const std::shared_ptr<Stack> &next, const Location &location);
  std::string str() const;

  Stack(const Location &location_) : next(), location(location_) { }
  Stack(const std::shared_ptr<Stack> &next_, const Location &location_);
};

std::ostream & operator << (std::ostream &os, const Stack *stack);

#endif
