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

#ifndef SSA_H
#define SSA_H

#include "primfn.h"
#include "gc.h"
#include <vector>
#include <memory>
#include <ostream>
#include <string>

struct Value;
struct Expr;

struct TermFormat {
  int depth;
  size_t id;
  TermFormat() : depth(0), id(0) { }
};

struct Term {
  static const size_t invalid = ~static_cast<size_t>(0);

  std::string label; // not unique
  uintptr_t meta;    // passes can stash info here
  virtual void update(const std::vector<size_t> &map) = 0;
  virtual void format(std::ostream &os, TermFormat &format) const = 0;
  virtual std::unique_ptr<Term> clone() const = 0;
  virtual ~Term();
  Term(const char *label_) : label(label_) { }

  static std::unique_ptr<Term> fromExpr(std::unique_ptr<Expr> expr);
};

struct Leaf : public Term {
  void update(const std::vector<size_t> &map) final override;
  Leaf(const char *label_) : Term(label_) { }
};

struct Redux : public Term {
  std::vector<size_t> args;
  void update(const std::vector<size_t> &map) final override;
  void format_args(std::ostream &os, TermFormat &format) const;
  Redux(const char *label_, std::vector<size_t> &&args_) : Term(label_), args(std::move(args_)) { }
};

struct RArg final : public Leaf {
  void format(std::ostream &os, TermFormat &format) const override;
  RArg(const char *label_ = "") : Leaf(label_) { }
  std::unique_ptr<Term> clone() const override;
};

struct RLit final : public Leaf {
  std::shared_ptr<RootPointer<Value> > value;
  void format(std::ostream &os, TermFormat &format) const override;
  RLit(std::shared_ptr<RootPointer<Value> > value_, const char *label_ = "") : Leaf(label_), value(std::move(value_)) { }
  std::unique_ptr<Term> clone() const override;
};

struct RApp final : public Redux {
  // arg0 = fn, arg1+ = arguments
  void format(std::ostream &os, TermFormat &format) const override;
  RApp(size_t fn, size_t arg, const char *label_ = "") : Redux(label_, {fn, arg}) { }
  std::unique_ptr<Term> clone() const override;
};

struct RPrim final : public Redux {
  std::string name;
  PrimFn fn;
  void *data;
  int pflags;
  void format(std::ostream &os, TermFormat &format) const override;
  RPrim(const char *name_, PrimFn fn_, void *data_, int pflags_, std::vector<size_t> &&args_, const char *label_ = "")
   : Redux(label_, std::move(args_)), name(name_), fn(fn_), data(data_), pflags(pflags_) { }
  std::unique_ptr<Term> clone() const override;
};

struct RGet final : public Redux {
  // arg0 = object
  size_t index;
  void format(std::ostream &os, TermFormat &format) const override;
  RGet(size_t index_, size_t obj, const char *label_ = "") : Redux(label_, {obj}), index(index_) { }
  std::unique_ptr<Term> clone() const override;
};

struct RDes final : public Redux {
  // args = handlers for cases, then obj
  void format(std::ostream &os, TermFormat &format) const override;
  RDes(std::vector<size_t> &&args_, const char *label_ = "") : Redux(label_, std::move(args_)) { }
  std::unique_ptr<Term> clone() const override;
};

struct RCon final : public Redux {
  // arg0+ = tuple elements
  size_t kind;
  void format(std::ostream &os, TermFormat &format) const override;
  RCon(size_t kind_, std::vector<size_t> &&args_, const char *label_ = "")
   : Redux(label_, std::move(args_)), kind(kind_) { }
  std::unique_ptr<Term> clone() const override;
};

struct RFun final : public Term {
  size_t output; // output can refer to a non-member Term
  std::vector<std::unique_ptr<Term> > terms;
  void update(const std::vector<size_t> &map) final override;
  void format(std::ostream &os, TermFormat &format) const override;
  RFun(const char *label_, size_t output_ = Term::invalid)
   : Term(label_), output(output_) { }
  std::unique_ptr<Term> clone() const override; // shallow (terms uncopied)
};

struct CheckPoint {
  size_t terms;
  size_t map;
  CheckPoint(size_t terms_, size_t map_) : terms(terms_), map(map_) { }
};

struct TermRewriter {
  // Update references of an old Redux (must call this before replace)
  void update(Redux *redux) const;
  void update(Term *term) const;

  // Streaming manipulation of terms
  size_t replace(std::unique_ptr<Term> term); // new AST updates an old AST Term (refs get updated)
  size_t insert(std::unique_ptr<Term> term);  // insert a Term not present in old AST (no prior refs)
  size_t insert(Term *term) { return insert(std::unique_ptr<Term>(term)); }
  void remove(); // remove a Term which was in the old AST (dangling refs assert fail)

  Term *operator [] (size_t index); // inspect a Term in new AST

  // Save the state of the rewriter to mark a function start
  CheckPoint begin() const;
  // Restore the state of the rewriter, popping the function body
  std::vector<std::unique_ptr<Term> > end(CheckPoint p);

  // Finish the rewrite and claim term 0
  std::unique_ptr<Term> finish();

private:
  std::vector<size_t> map; // from old AST to new AST
  std::vector<std::unique_ptr<Term> > terms; // new AST
};

inline void TermRewriter::update(Redux *redux) const {
  redux->update(map); // faster (update is final)
}

inline void TermRewriter::update(Term *term) const {
  term->update(map);
}

inline size_t TermRewriter::replace(std::unique_ptr<Term> term) {
  size_t out = terms.size();
  map.push_back(out);
  terms.emplace_back(std::move(term));
  return out;
}

inline size_t TermRewriter::insert(std::unique_ptr<Term> term) {
  size_t out = terms.size();
  terms.emplace_back(std::move(term));
  return out;
}

inline void TermRewriter::remove() {
  map.push_back(Term::invalid);
}

inline Term *TermRewriter::operator [] (size_t index) {
  return terms[index].get();
}

inline CheckPoint TermRewriter::begin() const {
  return CheckPoint(terms.size(), map.size());
}

inline std::unique_ptr<Term> TermRewriter::finish() {
  std::unique_ptr<Term> out = std::move(terms[0]);
  terms.clear();
  map.clear();
  return out;
}

#endif
