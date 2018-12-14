#include "type.h"
#include "location.h"
#include <iostream>
#include <cassert>

static int globalClock = 0;
static int globalEpoch = 1; // before a tagging pass, globalEpoch > TypeVar.epoch for all TypeVars

TypeVar::TypeVar() : parent(0), epoch(0), dob(0), nargs(0), pargs(0) { }

TypeVar::TypeVar(const std::string &name_, int nargs_)
 : parent(0), epoch(0), dob(++globalClock), nargs(nargs_), name(name_) {
  pargs = nargs ? new TypeVar[nargs] : 0;
  for (int i = 0; i < nargs; ++i) pargs[i].dob = ++globalClock;
}

TypeVar::~TypeVar() {
  if (nargs) delete [] pargs;
}

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

void TypeVar::setDOB() {
  assert (isFree());
  dob = ++globalClock;
}

bool TypeVar::contains(const TypeVar *other) const {
  const TypeVar *a = find();
  if (a->epoch < globalEpoch) a->epoch = globalEpoch;
  if (!(a->epoch & 2)) {
    a->epoch |= 2;
    if (a == other) return true;
    for (int i = 0; i < a->nargs; ++i)
      if (a->pargs[i].contains(other))
        return true;
  }
  return false;
}

void TypeVar::do_sweep() const {
  const TypeVar *a = find();
  if ((a->epoch & 2)) {
    epoch &= ~2;
    for (int i = 0; i < a->nargs; ++i)
      a->pargs[i].do_sweep();
  }
}

// Always point RHS at LHS (so RHS can be a temporary)
bool TypeVar::do_unify(TypeVar &other) {
  TypeVar *a = find();
  TypeVar *b = other.find();
  assert (a->dob);
  assert (b->dob);

  if (a == b) {
    return true;
  } else if (b->isFree()) {
    bool infinite = a->contains(b);
    a->do_sweep();
    if (!infinite) {
      b->parent = a;
      if (b->dob < a->dob) a->dob = b->dob;
    }
    return !infinite;
  } else if (a->isFree()) {
    bool infinite = b->contains(a);
    b->do_sweep();
    if (!infinite) {
      std::swap(a->name,  b->name);
      std::swap(a->nargs, b->nargs);
      std::swap(a->pargs, b->pargs);
      b->parent = a;
      if (b->dob < a->dob) a->dob = b->dob;
    }
    return !infinite;
  } else if (a->name != b->name || a->nargs != b->nargs) {
    return false;
  } else {
    bool ok = true;
    for (int i = 0; i < a->nargs; ++i)
      if (!a->pargs[i].do_unify(b->pargs[i]))
        ok = false;
    if (ok) {
      b->parent = a;
      if (b->dob < a->dob) a->dob = b->dob;
      b->name.clear();
      // we cannot clear pargs, because other TypeVars might point through our children
    } else {
      if (a->epoch < globalEpoch) a->epoch = globalEpoch;
      a->epoch |= 1;
    }
    return ok;
  }
}

void TypeVar::do_debug(std::ostream &os, TypeVar &other, int who, bool parens) {
  TypeVar *a = find();
  TypeVar *b = other.find();
  TypeVar *w = who ? b : a;

  if (a->nargs != b->nargs || a->name != b->name) {
    if (parens && w->nargs > 0) os << "(";
    // os << "[[[ ";
    if (w->name[0] == '=') {
      os << "_ => _";
    } else if (w->isFree()) {
      os << "infinite-type";
    } else {
      os << w->name;
      for (int i = 0; i < w->nargs; ++i)
        os << " _";
    }
    // os << " ]]]";
    if (parens && w->nargs > 0) os << ")";
  } else if (a->epoch - globalEpoch != 1) {
    os << "_";
  } else {
    if (parens) os << "(";
    if (a->name[0] == '=') {
      a->pargs[0].do_debug(os, b->pargs[0], who, true);
      os << " => ";
      a->pargs[1].do_debug(os, b->pargs[1], who, false);
    } else {
      os << a->name;
      for (int i = 0; i < a->nargs; ++i) {
        os << " ";
        a->pargs[i].do_debug(os, b->pargs[i], who, true);
      }
    }
    if (parens) os << ")";
  }
}

bool TypeVar::unify(TypeVar &other, Location *location) {
  globalEpoch += (-globalEpoch) & 3; // round up to a multiple of 4
  bool ok = do_unify(other);
  if (!ok) {
    std::ostream &os = std::cerr;
    os << "Type inference error";
    if (location) os << " at " << *location;
    os << ":" << std::endl << "    ";
    do_debug(os, other, 0, false);
    os << std::endl << "  does not match:" << std::endl << "    ";
    do_debug(os, other, 1, false);
    os << std::endl;
  }
  globalEpoch += 4;
  return ok;
}

void TypeVar::do_clone(TypeVar &out, const TypeVar &x, int dob) {
  out.dob = ++globalClock;
  const TypeVar *in = x.find();
  if (in->name.size() == 0 && in->dob < dob) { // no need to clone
    out.parent = const_cast<TypeVar*>(in);
  } else {
    if (in->epoch < globalEpoch) { // not previously cloned
      in->epoch = globalEpoch;
      in->link = &out;
      out.name = in->name;
      out.nargs = in->nargs;
      out.pargs = out.nargs ? new TypeVar[out.nargs] : 0;
      for (int i = 0; i < out.nargs; ++i)
        do_clone(out.pargs[i], in->pargs[i], dob);
    } else { // this TypeVar was already cloned; replicate sharing
      out.parent = in->link;
    }
  }
}

void TypeVar::clone(TypeVar &into) const {
  assert (!into.parent && into.isFree());
  do_clone(into, *this, dob);
  ++globalEpoch;
}

static void tag2str(std::ostream &os, int tag) {
  int radix = ('z' - 'a') + 1;
  if (tag >= radix) tag2str(os, tag / radix);
  os << (char)('a' + (tag % radix));
}

int TypeVar::do_format(std::ostream &os, int dob, const TypeVar &value, int tags, bool parens) {
  const TypeVar *a = value.find();
  if (a->isFree()) {
    int tag = a->epoch - globalEpoch;
    if (tag < 0) {
      tag = tags++;
      a->epoch = globalEpoch + tag;
    }
    if (a->dob <= dob) os << "_";
    tag2str(os, tag);
  } else if (a->nargs == 0) {
    os << a->name;
  } else if (a->name[0] == '=') {
    if (parens) os << "(";
    tags = do_format(os, dob, a->pargs[0], tags, true);
    os << " => ";
    tags = do_format(os, dob, a->pargs[1], tags, false);
    if (parens) os << ")";
  } else {
    if (parens) os << "(";
    os << a->name;
    for (int i = 0; i < a->nargs; ++i) {
      os << " ";
      tags = do_format(os, dob, a->pargs[i], tags, true);
    }
    if (parens) os << ")";
  }
  return tags;
}

std::ostream & operator << (std::ostream &os, const TypeVar &value) {
  globalEpoch += TypeVar::do_format(os, value.find()->dob, value, 0, false);
  return os;
}
