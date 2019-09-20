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
  TermFormat() : depth(0) { }
};

struct Term {
  static const size_t invalid = ~static_cast<size_t>(0);

  std::string label; // not unique
  uintptr_t meta;    // passes can stash info here
  virtual void update(const std::vector<size_t> &map) = 0;
  virtual void format(std::ostream &os, TermFormat &format) const = 0;
  virtual ~Term();
  Term(const char *label_) : label(label_) { }

  static std::unique_ptr<Term> fromExpr(Expr *expr);
};

struct Leaf : public Term {
  void update(const std::vector<size_t> &map) final override;
  Leaf(const char *label_) : Term(label_) { }
};

struct Redux : public Term {
  std::vector<size_t> args;
  void update(const std::vector<size_t> &map) final override;
  void format_args(std::ostream &os, TermFormat &format) const;
  Redux(const char *label_) : Term(label_) { }
};

struct RArg final : public Leaf {
  void format(std::ostream &os, TermFormat &format) const override;
  RArg(const char *label_ = "") : Leaf(label_) { }
};

struct RLit final : public Leaf {
  std::shared_ptr<RootPointer<Value> > value;
  void format(std::ostream &os, TermFormat &format) const override;
  RLit(std::shared_ptr<RootPointer<Value> > value_, const char *label_ = "") : Leaf(label_), value(std::move(value_)) { }
};

struct RApp final : public Redux {
  // arg0 = fn, arg1+ = arguments
  void format(std::ostream &os, TermFormat &format) const override;
  RApp(const char *label_ = "") : Redux(label_) { }
};

struct RPrim final : public Redux {
  std::string name;
  PrimFn fn;
  void *data;
  int pflags;
  void format(std::ostream &os, TermFormat &format) const override;
  RPrim(const char *name_, PrimFn fn_, void *data_, int pflags_, const char *label_ = "")
   : Redux(label_), name(name_), fn(fn_), data(data_), pflags(pflags_) { }
};

struct RGet final : public Redux {
  // arg0 = object
  size_t index;
  void format(std::ostream &os, TermFormat &format) const override;
  RGet(size_t index_, const char *label_ = "") : Redux(label_), index(index_) { }
};

struct RDes final : public Redux {
  // arg0 = object, args1+ = handlers for cases
  void format(std::ostream &os, TermFormat &format) const override;
  RDes(const char *label_ = "") : Redux(label_) { }
};

struct RCon final : public Redux {
  // arg0+ = tuple elements
  size_t kind;
  void format(std::ostream &os, TermFormat &format) const override;
  RCon(size_t kind_, const char *label_ = "") : Redux(label_), kind(kind_) { }
};

struct RFun final : public Term {
  size_t start, output; // output can refer to a non-member Term
  std::vector<std::unique_ptr<Term> > terms;
  void update(const std::vector<size_t> &map) final override;
  void format(std::ostream &os, TermFormat &format) const override;
  RFun(const char *label_, size_t start_, size_t output_ = Term::invalid)
   : Term(label_), start(start_), output(output_) { }
};

struct TermRewriter {
  // Update references of an old Redux (must call this before replace)
  void update(Redux *redux) const;
  void update(Term *term) const;

  // Streaming manipulation of terms
  size_t replace(std::unique_ptr<Term> term); // new AST updates an old AST Term (refs get updated)
  size_t insert(std::unique_ptr<Term> term);  // insert a Term not present in old AST (no prior refs)
  void remove(); // remove a Term which was in the old AST (dangling refs assert fail)

  Term *operator [] (size_t index); // inspect a Term in new AST

  // Create a new function with the same label+meta as base
  // Until exit, terms is empty and output is Term::invalid
  size_t enter_replace(Term *base); // record start, copy label+meta
  size_t enter_insert(Term *base); // record start, copy label+meta
  // Complete a function, filling terms
  size_t exit(size_t output);

  // Finish the rewite; call after exit of the top function
  std::unique_ptr<Term> finish();

private:
  std::unique_ptr<Term> enter(Term *base);
  std::vector<size_t> stack; // location of incomplete function starts
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

#endif
