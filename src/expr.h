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

#define FLAG_TOUCHED	1 // already explored for _
#define FLAG_AST	2 // useful to include in AST

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

  int args, pflags;
  PrimFn fn;
  void *data;

  static const TypeDescriptor type;
  Prim(const Location &location_, const std::string &name_) : Expr(&type, location_), name(name_), args(0), pflags(0) { }

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
  Location token;

  static const TypeDescriptor type;
  Lambda(const Location &location_, const std::string &name_, Expr *body_)
   : Expr(&type, location_), name(name_), body(body_), token(LOCATION) { }

  void format(std::ostream &os, int depth) const;
  Hash hash();
};

struct VarRef : public Expr {
  std::string name;
  int depth;
  int offset;
  Location target;

  static const TypeDescriptor type;
  VarRef(const Location &location_, const std::string &name_, int depth_ = 0, int offset_ = -1)
   : Expr(&type, location_), name(name_), depth(depth_), offset(offset_), target(LOCATION) { }

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

struct DefMap : public Expr {
  struct Value {
    Location location;
    std::unique_ptr<Expr> body;
    Value(const Location &location_, std::unique_ptr<Expr> &&body_)
     : location(location_), body(std::move(body_)) { }
  };

  typedef std::map<std::string, Value> Defs;
  typedef std::map<std::string, std::vector<Value> > Pubs;
  Defs map;
  Pubs pub;
  std::unique_ptr<Expr> body;

  static const TypeDescriptor type;
  DefMap(const Location &location_, Defs &&map_, Pubs &&pub_, Expr *body_)
   : Expr(&type, location_), map(std::move(map_)), pub(std::move(pub_)), body(body_) { }
  DefMap(const Location &location_)
   : Expr(&type, location_), map(), pub(), body(new Literal(location, "top")) { }

  void format(std::ostream &os, int depth) const;
  Hash hash();

  // Convert into (\a\b\c body) va vb vc ... to prevent type generalization
  static std::unique_ptr<Expr> dont_generalize(std::unique_ptr<DefMap> &&map);
};

struct Top : public Expr {
  typedef std::vector<std::unique_ptr<DefMap> > DefMaps;
  typedef std::map<std::string, int> DefOrder;
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
  struct OrderValue {
    Location location;
    int index;
    OrderValue(const Location &location_, int index_)
     : location(location_), index(index_) { }
  };
  typedef std::vector<std::unique_ptr<Expr> > Values;
  typedef std::vector<std::unique_ptr<Lambda> > Functions;
  typedef std::map<std::string, OrderValue> Order;

  std::unique_ptr<Expr> body;
  Values val;     // access prior binding
  Functions fun;  // access current binding
  Order order; // values, then functions
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

// A dummy expression never actually used in the AST
struct VarDef : public Expr {
  static const TypeDescriptor type;
  Location target; // for publishes
  VarDef(const Location &location_) : Expr(&type, location_), target(LOCATION) { }
  void format(std::ostream &os, int depth) const;
  Hash hash();
};

// A dummy expression never actually used in the AST
struct VarArg : public Expr {
  static const TypeDescriptor type;
  VarArg(const Location &location_) : Expr(&type, location_) { }
  void format(std::ostream &os, int depth) const;
  Hash hash();
};

#endif
