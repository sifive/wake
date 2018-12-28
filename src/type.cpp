#include <iostream>
#include "type.h"

TypeVar::TypeVar() : parent(0), dob(0) { }
TypeVar::TypeVar(const std::string &name_, int nargs)
 : parent(0), dob(0), name(name_), args(nargs) { }

const TypeVar *TypeVar::find() const {
  if (parent == 0) return this;
  parent = parent->find();
  return parent;
}

TypeVar *TypeVar::find() {
  if (parent == 0) return this;
  parent = parent->find();
  return parent;
}

static int globalClock = 0;
void TypeVar::setDOB() {
  dob = ++globalClock;
  for (auto &i : args) i.setDOB();
}

TypeVar::TypeVar(int x, TypeVar &&child) : parent(0), dob(0), name("=>") {
  args.resize(1); // free typevar
  args.emplace_back(std::move(child));
}

TypeVar TypeVar::primFn(int nargs) {
  if (nargs == 0) return TypeVar();
  return TypeVar(0, TypeVar::primFn(nargs-1));
}

TypeVar &TypeVar::getArg(int arg) {
  if (arg == 0) {
    if (name == "=>") return args[0];
    return *this;
  } else {
    return args[1].getArg(arg-1);
  }
}

bool TypeVar::unifyVal(TypeVar &other) {
  TypeVar *a = find();
  TypeVar *b = other.find();

  if (a == b) {
    /* no op */
  } else if (a->isFree()) {
    a->parent = b;
    if (a->dob < b->dob) b->dob = a->dob;
  } else if (b->isFree()) {
    b->parent = a;
    if (b->dob < a->dob) a->dob = b->dob;
  } else if (a->name != b->name || a->args.size() != b->args.size()) {
    std::cerr << "Unification failure!" << std::endl;
    return false;
  } else {
    a->parent = b;
    if (a->dob < b->dob) b->dob = a->dob;
    for (int i = 0; i < (int)a->args.size(); ++i)
      a->args[i].unifyVal(b->args[i]);
  }

  return true;
}

bool TypeVar::unifyDef(const TypeVar &other, int dob) {
  TypeVar *a = find();
  const TypeVar *b = other.find();

  if (a == b) {
    /* no op */
  } else if (a->isFree()) {
    a->name = b->name;
    if (b->dob >= dob) { a->dob = ++globalClock; }
    a->args.resize(b->args.size());
    for (int i = 0; i < (int)a->args.size(); ++i)
      a->args[i].unifyDef(b->args[i], dob);
  } else if (b->isFree()) {
    /* no op */
  } else if (a->name != b->name || a->args.size() != b->args.size()) {
    std::cerr << "Unification failure!" << std::endl;
    return false;
  } else {
    for (int i = 0; i < (int)a->args.size(); ++i)
      a->args[i].unifyDef(b->args[i], dob);
  }
  return true;
}

std::ostream & operator << (std::ostream &os, const TypeVar &value) {
  if (value.isFree()) {
    os << value.getDOB();
  } else {
    os << value.getName();
    for (auto &i : value.getArgs()) os << " (" << i << ")";
  }
  return os;
}
