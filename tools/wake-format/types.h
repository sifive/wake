/*
 * Copyright 2022 SiFive, Inc.
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

#pragma once

#include <wcl/doc.h>
#include <wcl/hash.h>

#include <set>

#include "parser/cst.h"
#include "parser/parser.h"

#define ALWAYS_INLINE inline __attribute__((always_inline))

#define SPACE_STR " "
#define NL_STR "\n"

// #define SPACE_STR "·"
// #define NL_STR "⏎\n"

#define SPACE_PER_INDENT 4
#define MAX_COLUMN_WIDTH 100

enum class ExplodeOption {
  // Explode must not be done
  Prevent,
  // Explode is allowed, but should be avoided
  Allow,
  // Explode must be done if possible
  Prefer
};

struct ctx_t {
  size_t nest_level = 0;
  wcl::doc_state state = wcl::doc_state::identity();

  ExplodeOption explode_option = ExplodeOption::Allow;
  bool nested_binop = false;

  ctx_t nest() const {
    ctx_t copy = *this;
    copy.nest_level++;
    return copy;
  }

  ctx_t prevent_explode() {
    ctx_t copy = *this;
    copy.explode_option = ExplodeOption::Prevent;
    return copy;
  }

  ctx_t allow_explode() {
    ctx_t copy = *this;
    copy.explode_option = ExplodeOption::Allow;
    return copy;
  }

  ctx_t prefer_explode() {
    ctx_t copy = *this;
    copy.explode_option = ExplodeOption::Prefer;
    return copy;
  }

  ctx_t binop() {
    ctx_t copy = *this;
    copy.nested_binop = true;
    return copy;
  }

  ctx_t sub(const wcl::doc_builder& builder) const {
    ctx_t copy = *this;
    copy.state = state + *builder;
    return copy;
  }

  const wcl::doc_state& operator*() const { return state; }
  const wcl::doc_state& operator->() const { return state; }

  bool operator==(const ctx_t& other) const {
    return state == other.state && nest_level == other.nest_level &&
           explode_option == other.explode_option && nested_binop == other.nested_binop;
  }
};

struct token_traits_t {
  // Tokens bound to this token 'before' this token
  // in source order
  std::set<CSTElement, CSTElementCompare> before_bound = {};
  std::vector<CSTElement> before_nls = {};

  // Tokens bound to this token 'after' this token
  // in source order
  std::set<CSTElement, CSTElementCompare> after_bound = {};

  // The token this token is bound to
  // inverse of before/after_bound
  CSTElement bound_to;

  // bind_before captures comments and a single newline
  // for each internal and independent newline.
  //
  // internal: a newline that would not be removed by a "trim" call
  // independent: a newline that isn't strictly there for syntax purposes
  //   such as the newline that must always follow a comment
  //
  // Ex:
  //
  // <nl 1>
  // # comment1 <nl 2>
  // <nl 3>
  // <nl 4>
  // # comment2 <nl 5>
  // <nl 6>
  // def a = 5
  //
  // Captures comment1, nl3, nl4, comment2, nl6, def. Trailing NLs are not possible since the def is
  // always the last captured item
  //
  void bind_before(CSTElement e) {
    if (e.id() == TOKEN_NL) {
      before_nls.push_back(e);
      return;
    }

    if (e.id() != TOKEN_COMMENT) {
      return;
    }

    if (before_nls.size() > 1) {
      for (size_t i = 1; i < before_nls.size(); i++) {
        before_bound.insert(before_nls[i]);
      }
    }

    before_nls = {};
    before_bound.insert(e);
  }

  void bind_after(CSTElement e) {
    if (e.id() != TOKEN_COMMENT) {
      return;
    }

    after_bound.insert(e);
  }

  void set_bound_to(CSTElement e) { bound_to = e; }
};

using token_traits_map_t = std::unordered_map<CSTElement, token_traits_t>;

template <>
struct std::hash<ctx_t> {
  size_t operator()(ctx_t const& ctx) const noexcept {
    auto hash = wcl::hash_combine(std::hash<wcl::doc_state>{}(ctx.state),
                                  std::hash<size_t>{}(ctx.nest_level));
    return wcl::hash_combine(hash, std::hash<ExplodeOption>{}(ctx.explode_option));
  }
};
