#include "type.h"
#include "location.h"
#include "symbol.h"
#include "expr.h"
#include "status.h"
#include <cstring>
#include <iostream>
#include <cassert>

static int globalClock = 0;
static int globalEpoch = 1; // before a tagging pass, globalEpoch > TypeVar.epoch for all TypeVars

TypeChild::TypeChild() : var(), tag(0) { }

TypeVar::TypeVar() : parent(0), epoch(0), var_dob(0), free_dob(0), nargs(0), cargs(0), name("") { }

TypeVar::TypeVar(const char *name_, int nargs_)
 : parent(0), epoch(0), var_dob(++globalClock), free_dob(var_dob), nargs(nargs_), name(name_) {
  cargs = nargs > 0 ? new TypeChild[nargs] : 0;
  for (int i = 0; i < nargs; ++i) {
    cargs[i].var.free_dob = cargs[i].var.var_dob = ++globalClock;
  }
}

TypeVar::~TypeVar() {
  if (nargs) delete [] cargs;
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
  if (!var_dob) {
    assert (!parent && isFree());
    free_dob = var_dob = ++globalClock;
  }
}

void TypeVar::setDOB(const TypeVar &other) {
  if (!var_dob) {
    assert (!parent && isFree());
    free_dob = var_dob = other.var_dob;
  }
}

void TypeVar::setTag(int i, const char *tag) {
  TypeVar *a = find();
  if (!a->cargs[i].tag) a->cargs[i].tag = tag;
}

bool TypeVar::contains(const TypeVar *other) const {
  const TypeVar *a = find();
  if (a->epoch < globalEpoch) a->epoch = globalEpoch;
  if (!(a->epoch & 2)) {
    a->epoch |= 2;
    if (a == other) return true;
    for (int i = 0; i < a->nargs; ++i)
      if (a->cargs[i].var.contains(other))
        return true;
  }
  return false;
}

void TypeVar::do_sweep() const {
  const TypeVar *a = find();
  if ((a->epoch & 2)) {
    a->epoch &= ~2;
    for (int i = 0; i < a->nargs; ++i)
      a->cargs[i].var.do_sweep();
  }
}

void TypeVar::do_cap(int dob) {
  TypeVar *a = find();
  if (dob < a->free_dob) a->free_dob = dob;
  for (int i = 0; i < a->nargs; ++i)
    a->cargs[i].var.do_cap(dob);
}

// Always point RHS at LHS (so RHS can be a temporary)
bool TypeVar::do_unify(TypeVar &other) {
  TypeVar *a = find();
  TypeVar *b = other.find();
  assert (a->var_dob);
  assert (b->var_dob);

  if (a == b) {
    return true;
  } else if (b->isFree()) {
    bool infinite = a->contains(b);
    a->do_sweep();
    if (!infinite) {
      a->do_cap(b->free_dob);
      b->parent = a;
    }
    return !infinite;
  } else if (a->isFree()) {
    bool infinite = b->contains(a);
    b->do_sweep();
    if (!infinite) {
      std::swap(a->name,     b->name);
      std::swap(a->nargs,    b->nargs);
      std::swap(a->cargs,    b->cargs);
      std::swap(a->free_dob, b->free_dob);
      a->do_cap(b->free_dob);
      b->parent = a;
    }
    return !infinite;
  } else if (strcmp(a->name, b->name) || a->nargs != b->nargs) {
    return false;
  } else {
    bool ok = true;
    for (int i = 0; i < a->nargs; ++i) {
      if (a->cargs[i].var.do_unify(b->cargs[i].var)) {
        if (!a->cargs[i].tag)
          a->cargs[i].tag = b->cargs[i].tag;
      } else {
        ok = false;
      }
    }
    if (ok) {
      b->parent = a;
      // we cannot clear cargs, because other TypeVars might point through our children
    } else {
      if (a->epoch < globalEpoch) a->epoch = globalEpoch;
      a->epoch |= 1;
    }
    return ok;
  }
}

void LegacyErrorMessage::formatA(std::ostream &os) const {
  os << "Type error; unable to unify";
  if (l) os << " " << l->text() << " of";
  os << " type";
}

void LegacyErrorMessage::formatB(std::ostream &os) const {
  os << "with incompatible type";
}

bool TypeVar::unify(TypeVar &other, const TypeErrorMessage *message) {
  globalEpoch += (-globalEpoch) & 3; // round up to a multiple of 4
  bool ok = do_unify(other);
  globalEpoch += 4;
  if (!ok) {
    std::ostream &os = std::cerr;
    message->formatA(os);
    os << ":" << std::endl << "    ";
    globalEpoch += do_format(os, 0, *this, 0, &other, 0, 0);
    os << std::endl << "  ";
    message->formatB(os);
    os << ":" << std::endl << "    ";
    globalEpoch += do_format(os, 0, other, 0, this, 0, 0);
    os << std::endl;
  }
  return ok;
}

void TypeVar::do_clone(TypeVar &out, const TypeVar &x, int dob) {
  out.free_dob = out.var_dob = ++globalClock;
  const TypeVar *in = x.find();
  if (in->isFree() && in->free_dob < dob) { // no need to clone
    out.parent = const_cast<TypeVar*>(in);
  } else {
    if (in->epoch < globalEpoch) { // not previously cloned
      in->epoch = globalEpoch;
      in->link = &out;
      out.name = in->name;
      out.nargs = in->nargs;
      out.cargs = out.nargs > 0 ? new TypeChild[out.nargs] : 0;
      for (int i = 0; i < out.nargs; ++i) {
        do_clone(out.cargs[i].var, in->cargs[i].var, dob);
        out.cargs[i].tag = in->cargs[i].tag;
      }
    } else { // this TypeVar was already cloned; replicate sharing
      out.parent = in->link;
    }
  }
}

void TypeVar::clone(TypeVar &into) const {
  assert (!into.parent && into.isFree());
  do_clone(into, *this, var_dob);
  ++globalEpoch;
}

static void tag2str(std::ostream &os, int tag) {
  int radix = ('z' - 'a') + 1;
  if (tag >= radix) tag2str(os, tag / radix);
  os << (char)('a' + (tag % radix));
}

int TypeVar::do_format(std::ostream &os, int dob, const TypeVar &value, const char *tag, const TypeVar *other, int tags, int o) {
  const TypeVar *a = value.find();
  const TypeVar *b = other?other->find():0;
  int p;

  if (tag) {
    op_type q = op_precedence(":");
    p = q.p + q.l;
    os << "(" << tag << ": ";
  } else {
    p = o;
  }

  if (b && (a->nargs != b->nargs || strcmp(a->name, b->name))) {
    os << term_red();
    if (a->isFree()) {
      os << "<infinite-type>";
    } else {
      do_format(os, dob, value, 0, 0, tags, p);
    }
    os << term_normal();
  } else if (a->isFree()) {
    int tag = a->epoch - globalEpoch;
    if (tag < 0) {
      tag = tags++;
      a->epoch = globalEpoch + tag;
    }
    if (a->free_dob < dob) os << "_";
    tag2str(os, tag);
  } else if (a->nargs == 0) {
    os << a->name;
  } else if (!strncmp(a->name, "binary ", 7)) {
    op_type q = op_precedence(a->name + 7);
    if (q.p < p) os << "(";
    tags = do_format(os, dob, a->cargs[0].var, a->cargs[0].tag, b?&b->cargs[0].var:0, tags, q.p + !q.l);
    if (a->name[7] != ',') os << " ";
    os << a->name + 7 << " ";
    tags = do_format(os, dob, a->cargs[1].var, a->cargs[1].tag, b?&b->cargs[1].var:0, tags, q.p + q.l);
    if (q.p < p) os << ")";
  } else if (!strncmp(a->name, "unary ", 6)) {
    op_type q = op_precedence(a->name + 6);
    if (q.p < p) os << "(";
    os << a->name + 6;
    tags = do_format(os, dob, a->cargs[0].var, a->cargs[0].tag, b?&b->cargs[0].var:0, tags, q.p);
    if (q.p < p) os << ")";
  } else {
    op_type q = op_precedence("a");
    if (q.p < p) os << "(";
    os << a->name;
    for (int i = 0; i < a->nargs; ++i) {
      os << " ";
      tags = do_format(os, dob, a->cargs[i].var, a->cargs[i].tag, b?&b->cargs[i].var:0, tags, q.p+q.l);
    }
    if (q.p < p) os << ")";
  }
  if (tag) os << ")";
  return tags;
}

void TypeVar::format(std::ostream &os, const TypeVar &top) const {
  globalEpoch += TypeVar::do_format(os, top.var_dob, *this, 0, 0, 0, 0);
}

std::ostream & operator << (std::ostream &os, const TypeVar &value) {
  globalEpoch += TypeVar::do_format(os, value.var_dob, value, 0, 0, 0, 0);
  return os;
}
