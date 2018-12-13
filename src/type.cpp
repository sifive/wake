#include <iostream>
#include <cassert>
#include <map>
#include "type.h"

TypeVar::TypeVar() : parent(0), dob(0), nargs(0), pargs(0) { }

TypeVar::TypeVar(const std::string &name_, int nargs_)
 : parent(0), dob(0), nargs(nargs_), name(name_) {
  pargs = nargs ? new TypeVar[nargs] : 0;
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

static int globalClock = 0;
void TypeVar::setDOB() {
  dob = ++globalClock;
  for (int i = 0; i < nargs; ++i)
    pargs[i].setDOB();
}

// Always point RHS at LHS (so RHS can be a temporary)
bool TypeVar::unify(TypeVar &other) {
  TypeVar *a = find();
  TypeVar *b = other.find();

  if (a == b) {
    return true;
  } else if (b->isFree()) {
    b->parent = a;
    if (b->dob < a->dob) a->dob = b->dob;
    return true;
  } else if (a->isFree()) {
    std::swap(a->name,  b->name);
    std::swap(a->nargs, b->nargs);
    std::swap(a->pargs, b->pargs);
    b->parent = a;
    if (b->dob < a->dob) a->dob = b->dob;
    return true;
  } else if (a->name != b->name || a->nargs != b->nargs) {
    std::cerr << "Unification failure!" << std::endl;
    return false;
  } else {
    bool ok = true;
    for (int i = 0; i < a->nargs; ++i)
      if (!a->pargs[i].unify(b->pargs[i]))
        ok = false;
    if (ok) {
      b->parent = a;
      if (b->dob < a->dob) a->dob = b->dob;
      b->name.clear();
      // we cannot clear pargs, because other TypeVars might point through our children
    }
    return ok;
  }
}

struct CloneMap {
  std::map<const TypeVar*, TypeVar*> map;
};

void TypeVar::do_clone(TypeVar &out, const TypeVar &x, int dob, CloneMap &clones) {
  out.dob = ++globalClock;
  const TypeVar *in = x.find();
  if (in->name.size() == 0 && in->dob < dob) { // no need to clone
    out.parent = const_cast<TypeVar*>(in);
  } else {
    auto dup = clones.map.insert(std::make_pair(in, &out));
    if (dup.second) { // not previously cloned
      out.name = in->name;
      out.nargs = in->nargs;
      out.pargs = out.nargs ? new TypeVar[out.nargs] : 0;
      for (int i = 0; i < out.nargs; ++i)
        do_clone(out.pargs[i], in->pargs[i], dob, clones);
    } else { // this TypeVar was already cloned; replicate sharing
      dup.first->second->unify(out);
    }
  }
}

void TypeVar::clone(TypeVar &into, int dob) const {
  assert (!into.parent && into.isFree());
  CloneMap clones;
  do_clone(into, *this, dob, clones);
}

static void tag2str(std::ostream &os, int tag) {
  int radix = ('z' - 'a') + 1;
  if (tag >= radix) tag2str(os, tag / radix);
  os << (char)('a' + (tag % radix));
}

struct LabelMap {
  std::map<const TypeVar*, int> map;
};

void TypeVar::do_format(std::ostream &os, const TypeVar &value, LabelMap &labels, bool parens) {
  const TypeVar *a = value.find();
  if (a->isFree()) {
    auto label = labels.map.insert(std::make_pair(a, (int)labels.map.size()));
    tag2str(os, label.first->second);
  } else if (a->nargs == 0) {
    os << a->name;
  } else if (a->name[0] == '=') {
    if (parens) os << "(";
    do_format(os, a->pargs[0], labels, true);
    os << " => ";
    do_format(os, a->pargs[1], labels, false);
    if (parens) os << ")";
  } else {
    if (parens) os << "(";
    os << a->name;
    for (int i = 0; i < a->nargs; ++i) {
      os << " ";
      do_format(os, a->pargs[i], labels, true);
    }
    if (parens) os << ")";
  }
}

std::ostream & operator << (std::ostream &os, const TypeVar &value) {
  LabelMap labels;
  TypeVar::do_format(os, value, labels, false);
  return os;
}
