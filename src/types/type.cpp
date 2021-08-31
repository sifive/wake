/*
 * Copyright 2019 SiFive, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You should have received a copy of LICENSE.Apache2 along with
 * this software. If not, you may obtain a copy at
 *
 *    https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Open Group Base Specifications Issue 7
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <cstring>
#include <sstream>
#include <cassert>

#include "util/fragment.h"
#include "util/diagnostic.h"
#include "util/colour.h"
#include "parser/lexer.h"
#include "type.h"

static int globalClock = 0;
static int globalEpoch = 1; // before a tagging pass, globalEpoch > TypeVar.epoch for all TypeVars

TypeVar::Imp::Imp() : link(nullptr), epoch(0), free_dob(0), nargs(0), cargs(nullptr), name() { }
TypeVar::TypeVar() : imp(new Imp), var_dob(0) { }
TypeChild::TypeChild() : var(), tag() { }

TypeVar::Imp::Imp(const char *name_, int nargs_)
 : link(nullptr), epoch(0), free_dob(++globalClock), nargs(nargs_), name(name_) {
  cargs = nargs > 0 ? new TypeChild[nargs] : nullptr;
  for (int i = 0; i < nargs; ++i) {
    cargs[i].var.imp->free_dob = cargs[i].var.var_dob = ++globalClock;
  }
}

TypeVar::TypeVar(const char *name_, int nargs_) : imp(new Imp(name_, nargs_)), var_dob(imp->free_dob) { }

TypeVar::Imp::~Imp() {
  if (nargs) delete [] cargs;
}

bool TypeVar::isFree() const {
  return imp->isFree() && var_dob == imp->free_dob;
}

bool TypeVar::operator < (const TypeVar &b) const {
  if (imp->free_dob < b.imp->free_dob) {
    return true;
  } else if (imp->free_dob > b.imp->free_dob) {
    return false;
  } else if (reinterpret_cast<uintptr_t>(static_cast<const void*>(imp.get()))
           < reinterpret_cast<uintptr_t>(static_cast<const void*>(b.imp.get()))) {
    return true;
  } else {
    return false;
  }
}

bool TypeVar::operator == (const TypeVar &b) const {
  return imp.get() == b.imp.get();
}

void TypeVar::setDOB() {
  if (!var_dob) {
    assert (imp->isFree());
    imp->free_dob = var_dob = ++globalClock;
  }
}

void TypeVar::setDOB(const TypeVar &other) {
  if (!var_dob) {
    assert (imp->isFree());
    imp->free_dob = var_dob = other.var_dob;
  }
}

void TypeVar::setTag(int i, const char *tag) {
  Imp *a = imp.get();
  if (a->cargs[i].tag.empty()) a->cargs[i].tag = tag;
}

bool TypeVar::Imp::isFree() const {
  return name.empty();
}

bool TypeVar::Imp::contains(const Imp *other) const {
  const Imp *a = this, *b = other;
  if (a->epoch < globalEpoch) a->epoch = globalEpoch;
  if (!(a->epoch & 2)) {
    a->epoch |= 2;
    if (a == b) return true;
    for (int i = 0; i < a->nargs; ++i)
      if (a->cargs[i].var.imp->contains(other))
        return true;
  }
  return false;
}

void TypeVar::Imp::do_sweep() const {
  if ((epoch & 2)) {
    epoch &= ~2;
    for (int i = 0; i < nargs; ++i)
      cargs[i].var.imp->do_sweep();
  }
}

void TypeVar::Imp::do_cap(int dob) {
  if (dob < free_dob) free_dob = dob;
  for (int i = 0; i < nargs; ++i)
    cargs[i].var.imp->do_cap(dob);
}

bool TypeVar::do_unify(TypeVar &other) {
  Imp *a = imp.get();
  Imp *b = other.imp.get();
  assert (var_dob);
  assert (other.var_dob);

  if (a == b) {
    return true;
  } else if (b->isFree()) {
    bool infinite = a->contains(b);
    a->do_sweep();
    if (!infinite) {
      a->do_cap(b->free_dob);
      imp.union_consume(other.imp);
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
      imp.union_consume(other.imp);
    }
    return !infinite;
  } else if (a->name != b->name || a->nargs != b->nargs) {
    return false;
  } else {
    bool ok = true;
    for (int i = 0; i < a->nargs; ++i) {
      if (a->cargs[i].var.do_unify(b->cargs[i].var)) {
        if (a->cargs[i].tag.empty())
          a->cargs[i].tag = b->cargs[i].tag;
      } else {
        ok = false;
      }
    }
    if (ok) {
      imp.union_consume(other.imp);
      // we cannot clear cargs, because other TypeVars might point through our children
    } else {
      if (a->epoch < globalEpoch) a->epoch = globalEpoch;
      a->epoch |= 1;
    }
    return ok;
  }
}

void LegacyErrorMessage::formatA(std::ostream &os) const {
  os << "type error; unable to unify";
  if (f) os << " " << f->segment() << " of";
  os << " type";
}

void LegacyErrorMessage::formatB(std::ostream &os) const {
  os << "with incompatible type";
}

bool TypeVar::tryUnify(TypeVar &other) {
  globalEpoch += (-globalEpoch) & 3; // round up to a multiple of 4
  bool ok = do_unify(other);
  globalEpoch += 4;
  return ok;
}

bool TypeVar::unify(TypeVar &other, const TypeErrorMessage *message) {
  bool ok = tryUnify(other);
  if (!ok) {
    std::ostringstream os;
    message->formatA(os);
    os << ":" << std::endl << "    ";
    globalEpoch += do_format(os, 0, *this, "", &other, 0, 0);
    os << std::endl << "  ";
    message->formatB(os);
    os << ":" << std::endl << "    ";
    globalEpoch += do_format(os, 0, other, "", this, 0, 0);
    reporter->reportError(message->f->location(), os.str());
  }
  return ok;
}

void TypeVar::do_clone(TypeVar &out, const TypeVar &x, int dob) {
  out.imp->free_dob = out.var_dob = ++globalClock;
  const Imp *in = x.imp.get();
  if (in->isFree() && in->free_dob < dob) { // no need to clone
    x.imp.union_consume(out.imp);
  } else {
    if (in->epoch < globalEpoch) { // not previously cloned
      in->epoch = globalEpoch;
      in->link = &out;
      Imp *imp = out.imp.get();
      imp->name = in->name;
      imp->nargs = in->nargs;
      imp->cargs = imp->nargs > 0 ? new TypeChild[imp->nargs] : 0;
      for (int i = 0; i < imp->nargs; ++i) {
        do_clone(imp->cargs[i].var, in->cargs[i].var, dob);
        imp->cargs[i].tag = in->cargs[i].tag;
      }
    } else { // this TypeVar was already cloned; replicate sharing
      in->link->imp.union_consume(out.imp);
    }
  }
}

void TypeVar::clone(TypeVar &into) const {
  assert (into.imp->isFree());
  do_clone(into, *this, var_dob);
  ++globalEpoch;
}

static void tag2str(std::ostream &os, int tag) {
  int radix = ('z' - 'a') + 1;
  if (tag >= radix) tag2str(os, tag / radix);
  os << (char)('a' + (tag % radix));
}

int TypeVar::do_format(std::ostream &os, int dob, const TypeVar &value, const char *tag, const TypeVar *other, int tags, int o, bool qualify) {
  const Imp *a = value.imp.get();
  const Imp *b = other?other->imp.get():nullptr;
  int p;

  if (tag[0]) {
    op_type q = op_precedence(":");
    p = q.p + q.l;
    os << "(" << tag << ": ";
  } else {
    p = o;
  }

  size_t at;
  if (qualify) {
    at = a->name.size();
  } else {
    at = a->name.find_first_of('@');
    if (at == std::string::npos) at = a->name.size();
  }

  if (b && (a->nargs != b->nargs || a->name != b->name)) {
    os << term_colour(TERM_RED);
    if (a->isFree()) {
      os << "<infinite-type>";
    } else {
      size_t bat = b->name.find_first_of('@');
      if (bat == std::string::npos) bat = b->name.size();
      do_format(os, dob, value, "", 0, tags, p, a->name.compare(0, at, b->name, 0, bat) == 0);
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
    os.write(a->name.c_str(), at);
  } else if (!a->name.compare(0, 7, "binary ", 7)) {
    op_type q = op_precedence(a->name.c_str() + 7);
    if (q.p < p) os << "(";
    tags = do_format(os, dob, a->cargs[0].var, a->cargs[0].tag.c_str(), b?&b->cargs[0].var:0, tags, q.p + !q.l);
    if (a->name[7] != ',') os << " ";
    os.write(a->name.c_str() + 7, at-7);
    os << " ";
    tags = do_format(os, dob, a->cargs[1].var, a->cargs[1].tag.c_str(), b?&b->cargs[1].var:0, tags, q.p + q.l);
    if (q.p < p) os << ")";
  } else if (!a->name.compare(0, 6, "unary ")) {
    op_type q = op_precedence(a->name.c_str() + 6);
    if (q.p < p) os << "(";
    os.write(a->name.c_str() + 6, at-6);
    tags = do_format(os, dob, a->cargs[0].var, a->cargs[0].tag.c_str(), b?&b->cargs[0].var:0, tags, q.p);
    if (q.p < p) os << ")";
  } else {
    op_type q = op_precedence("a");
    if (q.p < p) os << "(";
    os.write(a->name.c_str(), at);
    for (int i = 0; i < a->nargs; ++i) {
      os << " ";
      tags = do_format(os, dob, a->cargs[i].var, a->cargs[i].tag.c_str(), b?&b->cargs[i].var:0, tags, q.p+q.l);
    }
    if (q.p < p) os << ")";
  }
  if (tag[0]) os << ")";
  return tags;
}

void TypeVar::format(std::ostream &os, const TypeVar &top) const {
  globalEpoch += TypeVar::do_format(os, top.var_dob, *this, "", 0, 0, 0);
}

std::ostream & operator << (std::ostream &os, const TypeVar &value) {
  globalEpoch += TypeVar::do_format(os, value.var_dob, value, "", 0, 0, 0);
  return os;
}
