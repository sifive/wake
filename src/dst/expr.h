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

#include "util/fragment.h"
#include "util/hash.h"
#include "types/type.h"
#include "types/datatype.h"
#include "types/primfn.h"

#include <memory>
#include <string>
#include <map>
#include <vector>

struct Receiver;
struct Value;
struct Runtime;
struct Scope;
struct Continuation;

#define FLAG_TOUCHED   0x01 // already explored for _
#define FLAG_AST       0x02 // useful to include in AST
#define FLAG_RECURSIVE 0x04 // recursive function
#define FLAG_SYNTHETIC 0x08 // sugar-generated function

extern CPPFile expr_h;

/* Expression AST */
struct Expr {
  const TypeDescriptor *type;
  FileFragment fragment;
  TypeVar typeVar;
  uintptr_t meta;
  long flags;

  Expr(const TypeDescriptor *type_, const FileFragment &fragment_, long flags_ = 0) : type(type_), fragment(fragment_), meta(0), flags(flags_) { }
  virtual ~Expr();

  void set(long flag, long value /* 0 or 1 */) { flags = (flags & ~flag) | (-value & flag); }

  std::string to_str() const;
  virtual void format(std::ostream &os, int depth) const = 0;
};

std::ostream & operator << (std::ostream &os, const Expr *expr);

struct Prim : public Expr {
  std::string name;

  int args, pflags;
  PrimFn fn;
  void *data;

  static const TypeDescriptor type;
  Prim(const FileFragment &fragment_, const std::string &name_) : Expr(&type, fragment_), name(name_), args(0), pflags(0) { }

  void format(std::ostream &os, int depth) const override;
};

struct App : public Expr {
  std::unique_ptr<Expr> fn;
  std::unique_ptr<Expr> val;

  static const TypeDescriptor type;
  App(const FileFragment &fragment_, Expr *fn_, Expr *val_)
   : Expr(&type, fragment_), fn(fn_), val(val_) { }
  App(const App &app)
   : Expr(app), fn(), val() { }

  void format(std::ostream &os, int depth) const override;
};

struct Lambda : public Expr {
  std::string name, fnname;
  std::unique_ptr<Expr> body;
  FileFragment token;

  static const TypeDescriptor type;
  Lambda(const FileFragment &fragment_, const std::string &name_, Expr *body_, const char *fnname_ = "")
   : Expr(&type, fragment_), name(name_), fnname(fnname_), body(body_), token(FileFragment(&expr_h, __LINE__, __LINE__)) { }
  Lambda(const Lambda &lambda)
   : Expr(lambda), name(lambda.name), fnname(lambda.fnname), body(), token(lambda.token) { }

  void format(std::ostream &os, int depth) const override;
};

struct VarRef : public Expr {
  std::string name;
  int index;
  Lambda *lambda;
  FileFragment target;

  static const TypeDescriptor type;
  VarRef(const FileFragment &fragment_, const std::string &name_, int index_ = 0)
   : Expr(&type, fragment_), name(name_), index(index_), lambda(nullptr), target(FileFragment(&expr_h, __LINE__, __LINE__)) { }

  void format(std::ostream &os, int depth) const override;
};

struct Literal : public Expr {
  std::string value;
  TypeVar *litType;

  static const TypeDescriptor type;
  Literal(const FileFragment &fragment_, std::string &&value_, TypeVar *litType_);

  void format(std::ostream &os, int depth) const override;
};

struct Pattern {
  AST pattern;
  std::unique_ptr<Expr> expr;
  std::unique_ptr<Expr> guard;

  Pattern(AST &&pattern_, Expr *expr_, Expr *guard_) : pattern(std::move(pattern_)), expr(expr_), guard(guard_) { }
};

struct Match : public Expr {
  bool refutable;
  std::vector<std::unique_ptr<Expr> > args;
  std::vector<Pattern> patterns;
  std::unique_ptr<Expr> otherwise;

  static const TypeDescriptor type;
  Match(const FileFragment &fragment_, bool refutable_ = false)
   : Expr(&type, fragment_), refutable(refutable_) { }

  void format(std::ostream &os, int depth) const override;
};

struct Subscribe : public Expr {
  std::string name;

  static const TypeDescriptor type;
  Subscribe(const FileFragment &fragment_, const std::string &name_)
   : Expr(&type, fragment_), name(name_) { }

  void format(std::ostream &os, int depth) const override;
};

struct Ascribe : public Expr {
  AST signature;
  std::unique_ptr<Expr> body;
  FileFragment body_fragment;

  static const TypeDescriptor type;
  Ascribe(const FileFragment &fragment_, AST &&signature_, Expr *body_, const FileFragment &body_fragment_)
   : Expr(&type, fragment_), signature(std::move(signature_)), body(body_), body_fragment(body_fragment_) { }

  void format(std::ostream &os, int depth) const override;
};

struct DefValue {
  FileFragment fragment;
  std::unique_ptr<Expr> body;
  std::vector<ScopedTypeVar> typeVars;
  DefValue(const FileFragment &fragment_, std::unique_ptr<Expr> &&body_)
   : fragment(fragment_), body(std::move(body_)) { }
  DefValue(const FileFragment &fragment_, std::unique_ptr<Expr> &&body_, std::vector<ScopedTypeVar> &&typeVars_)
   : fragment(fragment_), body(std::move(body_)), typeVars(std::move(typeVars_)) { }
};

#define SYM_LEAF 1 // qualified = a definition
#define SYM_GRAY 2 // currently exploring this symbol

struct SymbolSource {
  FileFragment fragment;
  std::string qualified; // from@package
  long flags;

  SymbolSource(const FileFragment &fragment_, long flags_ = 0)
   : fragment(fragment_), qualified(), flags(flags_) { }
  SymbolSource(const FileFragment &fragment_, const std::string &qualified_, long flags_ = 0)
   : fragment(fragment_), qualified(qualified_), flags(flags_) { }

  SymbolSource clone(const std::string &qualified) const {
    return SymbolSource(fragment, qualified, flags);
  }
};

struct Symbols {
  typedef std::map<std::string, SymbolSource> SymbolMap; // to -> from@package
  SymbolMap defs;
  SymbolMap types;
  SymbolMap topics;
  void format(const char *kind, std::ostream &os, int depth) const;
  bool join(const Symbols &symbols, const char *scope);
  void setpkg(const std::string &pkgname);
};

struct Imports : public Symbols {
  SymbolMap mixed;
  std::vector<std::pair<std::string, FileFragment>> import_all;
  bool empty() const { return import_all.empty() && mixed.empty() && defs.empty() && types.empty() && topics.empty(); }
};

struct DefMap : public Expr {
  typedef std::map<std::string, DefValue> Defs;
  Defs defs;
  Imports imports;
  std::unique_ptr<Expr> body;

  static const TypeDescriptor type;
  DefMap(const FileFragment &fragment_, Defs &&defs_, Expr *body_)
   : Expr(&type, fragment_), defs(std::move(defs_)), body(body_) { }
  DefMap(const FileFragment &fragment_)
   : Expr(&type, fragment_), defs(), body(nullptr) { }

  void format(std::ostream &os, int depth) const override;
};

struct Topic {
  FileFragment fragment;
  AST type;
  Topic(const FileFragment &fragment_, AST &&type_) : fragment(fragment_), type(std::move(type_)) { }
};

struct File {
  typedef std::vector<std::pair<std::string, DefValue> > Pubs;
  typedef std::map<std::string, Topic> Topics;
  std::unique_ptr<DefMap> content;
  Symbols local;
  Pubs pubs;
  Topics topics;
};

struct Package {
  std::string name;
  std::vector<File> files;
  Symbols package;
  Symbols exports; // subset of package; used to fill imports

  void format(std::ostream &os, int depth) const;
};

struct Top {
  typedef std::map<std::string, std::unique_ptr<Package> > Packages;
  Packages packages;
  Symbols globals;
  const char *def_package;
  std::unique_ptr<Expr> body;

  Top();

  void format(std::ostream &os, int depth) const;
};

// Created by transforming DefMap+Package+Top
struct DefBinding : public Expr {
  struct OrderValue {
    FileFragment fragment;
    int index;
    OrderValue(const FileFragment &fragment_, int index_)
     : fragment(fragment_), index(index_) { }
  };
  typedef std::vector<std::unique_ptr<Expr> > Values;
  typedef std::vector<std::unique_ptr<Lambda> > Functions;
  typedef std::vector<std::vector<ScopedTypeVar> > TypeVars;
  typedef std::map<std::string, OrderValue> Order;

  std::unique_ptr<Expr> body;
  Values val;     // access prior binding
  Functions fun;  // access current binding
  Order order;    // values, then functions
  TypeVars valVars;
  TypeVars funVars;
  std::vector<unsigned> scc; // SCC id per function

  static const TypeDescriptor type;
  DefBinding(const FileFragment &fragment_, std::unique_ptr<Expr> body_)
   : Expr(&type, fragment_), body(std::move(body_)) { }

  void format(std::ostream &os, int depth) const override;
};

// Created by transforming Data
struct Constructor;
struct Get : public Expr {
  std::shared_ptr<Sum> sum;
  Constructor *cons;
  size_t index;

  static const TypeDescriptor type;
  Get(const FileFragment &fragment_, const std::shared_ptr<Sum> &sum_, Constructor *cons_, size_t index_)
   : Expr(&type, fragment_), sum(sum_), cons(cons_), index(index_) { }

  void format(std::ostream &os, int depth) const override;
};

struct Construct : public Expr {
  std::shared_ptr<Sum> sum;
  Constructor *cons;

  static const TypeDescriptor type;
  Construct(const FileFragment &fragment_, const std::shared_ptr<Sum> &sum_, Constructor *cons_)
   : Expr(&type, fragment_), sum(sum_), cons(cons_) { }

  void format(std::ostream &os, int depth) const override;
};

struct Destruct : public Expr {
  std::shared_ptr<Sum> sum;
  std::unique_ptr<Expr> arg;
  DefBinding::Values cases;

  static const TypeDescriptor type;
  Destruct(const FileFragment &fragment_, const std::shared_ptr<Sum> &sum_, Expr *arg_)
   : Expr(&type, fragment_), sum(sum_), arg(arg_) { }

  void format(std::ostream &os, int depth) const override;
};

// A dummy expression never actually used in the AST
struct VarDef : public Expr {
  static const TypeDescriptor type;
  VarDef(const FileFragment &fragment_) : Expr(&type, fragment_) { }
  void format(std::ostream &os, int depth) const override;
};

// A dummy expression never actually used in the AST
struct VarArg : public Expr {
  static const TypeDescriptor type;
  VarArg(const FileFragment &fragment_) : Expr(&type, fragment_) { }
  void format(std::ostream &os, int depth) const override;
};

#endif
