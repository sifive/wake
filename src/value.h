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

#ifndef VALUE_H
#define VALUE_H

#include "gc.h"
#include "type.h"
#include <string>
#include <memory>
#include <vector>
#include <limits>
#include <gmp.h>
#include <stdlib.h>
#include <re2/re2.h>

#define APP_PRECEDENCE 22

/* Values */

struct Lambda;
struct Tuple;

struct FormatEntry {
  const HeapObject *value;
  int precedence;
  int state;
  FormatEntry(const HeapObject *value_ = nullptr, int precedence_ = 0, int state_ = 0)
   : value(value_), precedence(precedence_), state(state_) { }
};

struct FormatState {
  std::vector<FormatEntry> stack;
  FormatEntry current;
  bool detailed;
  int indent; // -1 -> single-line
  void resume();
  void child(const HeapObject *value, int precedence);
  int get() const { return current.state; }
  int p() const { return current.precedence; }
};

struct String final : public GCObject<String> {
  typedef GCObject<String> Parent;

  static TypeVar typeVar;
  size_t length;

  String(size_t length_);
  String(const String &s);

  const char *c_str() const { return static_cast<const char*>(data()); }
  char *c_str() { return static_cast<char*>(data()); }

  void format(std::ostream &os, FormatState &state) const override;
  PadObject *next();

  static size_t reserve(size_t length);
  static String *claim(Heap &h, size_t length);
  static String *alloc(Heap &h, size_t length);

  // Never call this during runtime! It can invalidate the heap.
  static RootPointer<String> literal(Heap &h, const std::string &value);
};

// An exception-safe wrapper for mpz_t
struct MPZ {
  mpz_t value;
  MPZ() { mpz_init(value); }
  MPZ(long v) { mpz_init_set_si(value, v); }
  MPZ(const std::string &v) { mpz_init_set_str(value, v.c_str(), 0); }
  ~MPZ() { mpz_clear(value); }
  MPZ(const MPZ& x) = delete;
  MPZ& operator = (const MPZ& x) = delete;
};

struct Integer final : public GCObject<Integer> {
  typedef GCObject<Integer> Parent;

  static TypeVar typeVar;
  int length; // abs(length) = number of mp_limb_t in object

  Integer(int length_);
  Integer(const Integer &i);

  std::string str(int base = 10) const;
  void format(std::ostream &os, FormatState &state) const override;
  PadObject *next();

  static size_t reserve(const MPZ &mpz);
  static Integer *claim(Heap &h, const MPZ& mpz);
  static Integer *alloc(Heap &h, const MPZ& mpz);

  // create a fake mpz_t out of the heap object
  const __mpz_struct wrap() const {
    __mpz_struct out;
    out._mp_size = length;
    out._mp_alloc = abs(length);
    out._mp_d = static_cast<mp_limb_t*>(const_cast<void*>(data()));
    return out;
  }

  // Never call this during runtime! It can invalidate the heap.
  static RootPointer<Integer> literal(Heap &h, const std::string &str);
};

#define FIXED 0
#define SCIENTIFIC 1
#define HEXFLOAT 2
#define DEFAULTFLOAT 3
struct Double final : public GCObject<Double> {
  typedef std::numeric_limits< double > limits;

  static TypeVar typeVar;
  double value;

  Double(double value_ = 0) : value(value_) { }
  Double(const char *str) { char *end; value = strtod(str, &end); }

  std::string str(int format = DEFAULTFLOAT, int precision = limits::max_digits10) const;
  void format(std::ostream &os, FormatState &state) const;

  // Never call this during runtime! It can invalidate the heap.
  static RootPointer<Double> literal(Heap &h, const char *str);
};

struct RegExp final : public GCObject<RegExp, DestroyableObject> {
  typedef GCObject<RegExp, DestroyableObject> Parent;

  static TypeVar typeVar;
  std::shared_ptr<RE2> exp;

  RegExp(Heap &h, const std::string &regexp, const RE2::Options &opts);
  RegExp(Heap &h, const std::string &regexp);

  void format(std::ostream &os, FormatState &state) const;

  // Never call this during runtime! It can invalidate the heap.
  static RootPointer<RegExp> literal(Heap &h, const std::string &value);
};

struct Closure final : public GCObject<Closure, HeapObject> {
  static TypeVar typeVar;
  Lambda *lambda;
  HeapPointer<Tuple> scope;

  Closure(Lambda *lambda_, Tuple *scope_);
  void format(std::ostream &os, FormatState &state) const override;

  PadObject *recurse(PadObject *free) {
    free = HeapObject::recurse(free);
    free = scope.moveto(free);
    return free;
  }
};

struct Data {
  static TypeVar typeBoolean;
  static TypeVar typeOrder;
  static TypeVar typeUnit;
  static TypeVar typeJValue;
  static TypeVar typeError;
  // these are const to prevent unify() on them; use clone
  static const TypeVar typeList;
  static const TypeVar typePair;
  static const TypeVar typeResult;
};

#endif
