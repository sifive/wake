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

#ifndef TYPE_H
#define TYPE_H

#include "dsu.h"
#include <ostream>
struct Location;

#define FN "binary =>"

struct TypeErrorMessage {
  virtual void formatA(std::ostream &os) const = 0;
  virtual void formatB(std::ostream &os) const = 0;
};

struct LegacyErrorMessage : public TypeErrorMessage {
  const Location *l;
  LegacyErrorMessage(const Location *l_) : l(l_) { }
  void formatA(std::ostream &os) const;
  void formatB(std::ostream &os) const;
};

struct TypeChild;
struct TypeVar {
private:
  struct Imp {
    // Scratch variables useful for tree traversals
    mutable TypeVar *link;
    mutable int epoch;

    // free_dob is the DOB of a free variable, unified to the oldest
    int free_dob;
    int nargs;
    TypeChild *cargs;
    const char *name;

    bool isFree() const { return name[0] == 0; }
    bool contains(const Imp *other) const;
    void do_sweep() const;
    void do_cap(int dob);

    Imp(const char *name_, int nargs_);
    Imp();
    ~Imp();
  };

  // Handle to the set leader
  DSU<Imp> imp;
  // var_dob is unchanging after setDOB
  int var_dob;

  static void do_clone(TypeVar &out, const TypeVar &x, int dob);
  static int do_format(std::ostream &os, int dob, const TypeVar &value, const char *tag, const TypeVar *other, int tags, int p);
  bool do_unify(TypeVar &other);

public:
  TypeVar(); // free type-var
  TypeVar(const char *name_, int nargs_);

  const TypeVar & operator[](int i) const;
  TypeVar & operator[](int i);

  const char *getName() const;
  const char *getTag(int i) const;

  void setDOB();
  void setDOB(const TypeVar &other);
  void setTag(int i, const char *tag);
  bool unify(TypeVar &other,  const TypeErrorMessage *message);
  bool unify(TypeVar &&other, const TypeErrorMessage *message) { return unify(other, message); }
  //  Deprecated:
  bool unify(TypeVar &other,  const Location *l = 0) { LegacyErrorMessage m(l); return unify(other, &m); }
  bool unify(TypeVar &&other, const Location *l = 0) { LegacyErrorMessage m(l); return unify(other, &m); }

  void clone(TypeVar &into) const;
  void format(std::ostream &os, const TypeVar &top) const; // use top's dob

friend std::ostream & operator << (std::ostream &os, const TypeVar &value);
};

struct TypeChild {
  struct TypeVar var;
  std::string tag;
  TypeChild();
};

inline const TypeVar & TypeVar::operator[](int i) const { return imp->cargs[i].var; }
inline       TypeVar & TypeVar::operator[](int i) { return imp->cargs[i].var; }

inline const char *TypeVar::getName() const { return imp->name; }
inline const char *TypeVar::getTag(int i) const { return imp->cargs[i].tag.c_str(); }

#endif
