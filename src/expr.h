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

#ifndef EXPR_H
#define EXPR_H

#include "location.h"
#include "primfn.h"
#include "hash.h"
#include "heap.h"
#include "type.h"
#include "datatype.h"
#include <memory>
#include <string>
#include <map>
#include <vector>

struct Receiver;
struct Value;

#define FLAG_TOUCHED 1

/* Expression AST */
struct Expr {
  const TypeDescriptor *type;
  Location location;
  TypeVar typeVar;
  Hash hashcode;
  long flags;

  Expr(const TypeDescriptor *type_, const Location &location_, long flags_ = 0) : type(type_), location(location_), flags(flags_) { }
  virtual ~Expr();

  std::string to_str() const;
  virtual void format(std::ostream &os, int depth) const = 0;
  virtual Hash hash() = 0;
};

std::ostream & operator << (std::ostream &os, const Expr *expr);

struct Prim : public Expr {
  std::string name;

  int args, flags;
  PrimFn fn;
  void *data;

  static const TypeDescriptor type;
  Prim(const Location &location_, const std::string &name_) : Expr(&type, location_), name(name_), args(0), flags(0) { }

  void format(std::ostream &os, int depth) const;
  Hash hash();
};

struct App : public Expr {
  std::unique_ptr<Expr> fn;
  std::unique_ptr<Expr> val;

  static const TypeDescriptor type;
  App(const Location &location_, Expr *fn_, Expr *val_)
   : Expr(&type, location_), fn(fn_), val(val_) { }

  void format(std::ostream &os, int depth) const;
  Hash hash();
};

struct Lambda : public Expr {
  std::string name;
  std::unique_ptr<Expr> body;

  static const TypeDescriptor type;
  Lambda(const Location &location_, const std::string &name_, Expr *body_)
   : Expr(&type, location_), name(name_), body(body_) { }

  void format(std::ostream &os, int depth) const;
  Hash hash();
};

struct VarRef : public Expr {
  std::string name;
  int depth;
  int offset;

  static const TypeDescriptor type;
  VarRef(const Location &location_, const std::string &name_, int depth_ = 0, int offset_ = -1)
   : Expr(&type, location_), name(name_), depth(depth_), offset(offset_) { }

  void format(std::ostream &os, int depth) const;
  Hash hash();
};

struct Literal : public Expr {
  std::shared_ptr<Value> value;

  static const TypeDescriptor type;
  Literal(const Location &location_, std::shared_ptr<Value> &&value_);
  Literal(const Location &location_, const char *value_);

  void format(std::ostream &os, int depth) const;
  Hash hash();
};

struct Memoize : public Expr {
  long skip;
  std::unique_ptr<Expr> body;
  std::map<Hash, Future> values;

  static const TypeDescriptor type;
  Memoize(const Location &location_, long skip_, Expr *body_)
   : Expr(&type, location_), skip(skip_), body(body_) { }

  void format(std::ostream &os, int depth) const;
  Hash hash();
};

struct Pattern {
  AST pattern;
  std::unique_ptr<Expr> expr;
  std::unique_ptr<Expr> guard;

  Pattern(AST &&pattern_, Expr *expr_, Expr *guard_) : pattern(std::move(pattern_)), expr(expr_), guard(guard_) { }
};

struct Match : public Expr {
  std::vector<std::unique_ptr<Expr> > args;
  std::vector<Pattern> patterns;

  static const TypeDescriptor type;
  Match(const Location &location_)
   : Expr(&type, location_) { }

  void format(std::ostream &os, int depth) const;
  Hash hash();
};

struct Subscribe : public Expr {
  std::string name;

  static const TypeDescriptor type;
  Subscribe(const Location &location_, const std::string &name_)
   : Expr(&type, location_), name(name_) { }

  void format(std::ostream &os, int depth) const;
  Hash hash();
};

typedef std::map<std::string, int> DefOrder;

struct DefMap : public Expr {
  typedef std::map<std::string, std::unique_ptr<Expr> > defs;
  defs map;
  defs publish;
  std::unique_ptr<Expr> body;

  static const TypeDescriptor type;
  DefMap(const Location &location_, defs &&map_, defs &&publish_, Expr *body_)
   : Expr(&type, location_), map(std::move(map_)), publish(std::move(publish_)), body(body_) { }
  DefMap(const Location &location_)
   : Expr(&type, location_), map(), publish(), body(new Literal(location, "top")) { }

  void format(std::ostream &os, int depth) const;
  Hash hash();
};

struct Top : public Expr {
  typedef std::vector<std::unique_ptr<DefMap> > DefMaps;
  DefMaps defmaps;
  DefOrder globals;
  std::unique_ptr<Expr> body;

  static const TypeDescriptor type;
  Top() : Expr(&type, LOCATION), defmaps(), globals() { }

  void format(std::ostream &os, int depth) const;
  Hash hash();
};

// Created by transforming DefMap+Top
struct DefBinding : public Expr {
  typedef std::vector<std::unique_ptr<Expr> > values;
  typedef std::vector<std::unique_ptr<Lambda> > functions;

  std::unique_ptr<Expr> body;
  values val;     // access prior binding
  functions fun;  // access current binding
  DefOrder order; // values, then functions
  std::vector<int> scc; // SCC id per function

  static const TypeDescriptor type;
  DefBinding(const Location &location_, std::unique_ptr<Expr> body_)
   : Expr(&type, location_), body(std::move(body_)) { }

  void format(std::ostream &os, int depth) const;
  Hash hash();
};

// Created by transforming Data
struct Constructor;
struct Construct : public Expr {
  Sum *sum;
  Constructor *cons;

  static const TypeDescriptor type;
  Construct(const Location &location_, Sum *sum_, Constructor *cons_)
   : Expr(&type, location_), sum(sum_), cons(cons_) { }

  void format(std::ostream &os, int depth) const;
  Hash hash();
};

struct Sum;
struct Destruct : public Expr {
  Sum sum;

  static const TypeDescriptor type;
  Destruct(const Location &location_, Sum &&sum_)
   : Expr(&type, location_), sum(std::move(sum_)) { }

  void format(std::ostream &os, int depth) const;
  Hash hash();
};

#endif
