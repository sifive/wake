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

#include <bitset>
#include <cassert>
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

struct ctx_t {
  size_t nest_level = 0;
  wcl::doc_state state = wcl::doc_state::identity();

  // Expectation from a subtree
  // 1: By default, don't explode unless you have to
  // 2: if prefer_explode, explode if you can
  bool prefer_explode = false;

  ctx_t nest() const {
    ctx_t copy = *this;
    copy.nest_level++;
    return copy;
  }

  ctx_t explode() {
    ctx_t copy = *this;
    copy.prefer_explode = true;
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
           prefer_explode == other.prefer_explode;
  }
};

struct token_traits_t {
  // Tokens bound to this token 'before' this token
  // in source order
  std::set<CSTElement, CSTElementCompare> before_bound = {};

  // Tokens bound to this token 'after' this token
  // in source order
  std::set<CSTElement, CSTElementCompare> after_bound = {};

  // The token this token is bound to
  // inverse of before/after_bound
  CSTElement bound_to;

  void bind_before(CSTElement e) {
    // TODO: delete this after handling captured
    // NLs and WSes
    if (e.id() != TOKEN_COMMENT) {
      return;
    }
    before_bound.insert(e);
  }

  void bind_after(CSTElement e) {
    // TODO: delete this after handling captured
    // NLs and WSes
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
    return wcl::hash_combine(hash, std::hash<bool>{}(ctx.prefer_explode));
  }
};

inline void space(wcl::doc_builder& builder, uint8_t count) {
  for (uint8_t i = 0; i < count; i++) {
    builder.append(SPACE_STR);
  }
}

inline void newline(wcl::doc_builder& builder, uint8_t space_count) {
  builder.append(NL_STR);
  space(builder, space_count);
}

inline void freshline(wcl::doc_builder& builder, ctx_t ctx) {
  auto goal_width = SPACE_PER_INDENT * ctx.nest_level;
  auto merged = ctx.sub(builder);

  // there are non-ws characters on the line, thus a nl is required
  if (merged->last_width() > merged->last_ws_count()) {
    newline(builder, goal_width);
    return;
  }

  // This is a fresh line, but without the right amount of spaces
  if (merged->last_width() < goal_width) {
    space(builder, goal_width - merged->last_width());
    return;
  }

  // If there are too many spaces, then a freshline() was used instead
  // of newline(). Assert to ensure it is fixed.
  if (merged->last_width() > goal_width) {
    assert(false);
  }
}

class IsWSNLCPredicate {
 public:
  bool operator()(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node,
                  const token_traits_map_t& traits) const {
    return operator()(node);
  }

  bool operator()(const CSTElement& node) const {
    return node.id() == TOKEN_WS || node.id() == TOKEN_NL || node.id() == TOKEN_COMMENT;
  }
};

struct ConsumeWhitespaceAction {
  ALWAYS_INLINE void run(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node,
                         const token_traits_map_t& traits) {
    IsWSNLCPredicate predicate;
    while (!node.empty() && predicate(builder, ctx, node, traits)) {
      node.nextSiblingElement();
    }
  }
};

struct SpaceAction {
  uint8_t count;
  SpaceAction(uint8_t count) : count(count) {}
  ALWAYS_INLINE void run(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node,
                         const token_traits_map_t& traits) {
    space(builder, count);
  }
};

struct NewlineAction {
  ALWAYS_INLINE void run(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node,
                         const token_traits_map_t& traits) {
    newline(builder, 0);
  }
};

struct FreshlineAction {
  ALWAYS_INLINE void run(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node,
                         const token_traits_map_t& traits) {
    freshline(builder, ctx);
  }
};

struct LiteralAction {
  wcl::doc lit;
  LiteralAction(wcl::doc lit) : lit(lit) {}

  ALWAYS_INLINE void run(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node,
                         const token_traits_map_t& traits) {
    builder.append(std::move(lit));
  }
};

struct TokenAction {
  cst_id_t token_id;
  TokenAction(cst_id_t token_id) : token_id(token_id) {}
  ALWAYS_INLINE void run(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node,
                         const token_traits_map_t& traits) {
    if (node.id() != token_id) {
      // TODO: see about adding a token -> token name function
      std::cerr << "Unexpected token: " << +token_id << std::endl;
    }
    assert(node.id() == token_id);

    auto it = traits.find(node);
    if (it != traits.end()) {
      for (auto n : it->second.before_bound) {
        builder.append(n.fragment().segment().str());
        freshline(builder, ctx);
      }
    }

    builder.append(node.fragment().segment().str());

    if (it != traits.end()) {
      for (auto n : it->second.after_bound) {
        space(builder, 1);
        builder.append(n.fragment().segment().str());
        newline(builder, 0);
      }
    }

    node.nextSiblingElement();
  }
};

struct TokenReplaceAction {
  cst_id_t token_id;
  const char* str;

  TokenReplaceAction(cst_id_t token_id, const char* str) : token_id(token_id), str(str) {}

  ALWAYS_INLINE void run(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node,
                         const token_traits_map_t& traits) {
    if (node.id() != token_id) {
      // TODO: see about adding a token -> token name function
      std::cerr << "Unexpected token: " << +token_id << std::endl;
    }
    assert(node.id() == token_id);

    auto it = traits.find(node);
    if (it != traits.end()) {
      for (auto n : it->second.before_bound) {
        builder.append(n.fragment().segment().str());
        freshline(builder, ctx);
      }
    }

    builder.append(str);

    if (it != traits.end()) {
      for (auto n : it->second.after_bound) {
        space(builder, 1);
        builder.append(n.fragment().segment().str());
        newline(builder, 0);
      }
    }

    node.nextSiblingElement();
  }
};

struct WhitespaceTokenAction : public TokenReplaceAction {
  WhitespaceTokenAction() : TokenReplaceAction(TOKEN_WS, SPACE_STR) {}
};

template <class Action1, class Action2>
struct SeqAction {
  Action1 action1;
  Action2 action2;
  SeqAction(Action1 a1, Action2 a2) : action1(a1), action2(a2) {}

  ALWAYS_INLINE void run(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node,
                         const token_traits_map_t& traits) {
    action1.run(builder, ctx, node, traits);
    action2.run(builder, ctx, node, traits);
  }
};

template <class Predicate, class Function>
struct WalkPredicateAction {
  Predicate predicate;
  Function walker;

  WalkPredicateAction(Predicate predicate, Function walker)
      : predicate(predicate), walker(walker) {}

  ALWAYS_INLINE void run(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node,
                         const token_traits_map_t& traits) {
    bool result = predicate(builder, ctx, node, traits);
    if (!result) {
      // TODO: see about adding a token -> token name function
      std::cerr << "Unexpected token: " << +node.id() << std::endl;
    }
    assert(result);
    auto doc = walker(ctx.sub(builder), const_cast<const CSTElement&>(node));
    builder.append(doc);
    node.nextSiblingElement();
  }
};

template <class FMT>
struct NestAction {
  FMT formatter;

  NestAction(FMT formatter) : formatter(formatter) {}

  ALWAYS_INLINE void run(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node,
                         const token_traits_map_t& traits) {
    builder.append(formatter.compose(ctx.nest().sub(builder), node, traits));
  }
};

template <class Predicate, class IFMT, class EFMT>
struct IfElseAction {
  Predicate predicate;
  IFMT if_formatter;
  EFMT else_formatter;

  IfElseAction(Predicate predicate, IFMT if_formatter, EFMT else_formatter)
      : predicate(predicate), if_formatter(if_formatter), else_formatter(else_formatter) {}

  ALWAYS_INLINE void run(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node,
                         const token_traits_map_t& traits) {
    if (predicate(builder, ctx, node, traits)) {
      builder.append(if_formatter.compose(ctx.sub(builder), node, traits));
    } else {
      builder.append(else_formatter.compose(ctx.sub(builder), node, traits));
    }
  }
};

template <class Predicate, class FMT>
struct WhileAction {
  Predicate predicate;
  FMT while_formatter;

  WhileAction(Predicate predicate, FMT while_formatter)
      : predicate(predicate), while_formatter(while_formatter) {}

  ALWAYS_INLINE void run(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node,
                         const token_traits_map_t& traits) {
    while (predicate(builder, ctx, node, traits)) {
      builder.append(while_formatter.compose(ctx.sub(builder), node, traits));
    }
  }
};

template <class FMT>
struct WalkAllAction {
  FMT formatter;

  WalkAllAction(FMT formatter) : formatter(formatter) {}

  ALWAYS_INLINE void run(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node,
                         const token_traits_map_t& traits) {
    while (!node.empty()) {
      builder.append(formatter.compose(ctx.sub(builder), node, traits));
    }
  }
};

// This is the escape hatch to implement something else.
// NOTE: You're responsible for advancing the node!!!
template <class F>
struct EscapeAction {
  F f;
  EscapeAction(F f) : f(f) {}

  ALWAYS_INLINE void run(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node,
                         const token_traits_map_t& traits) {
    // You have to do everything yourself here, that's the price of an escape hatch
    // we can build up enough of these that it shouldn't be an issue however.
    f(builder, ctx, node);
  }
};

template <class FMT>
struct JoinAction {
  FMT formatter;

  JoinAction(FMT formatter) : formatter(formatter) {}

  ALWAYS_INLINE void run(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node,
                         const token_traits_map_t& traits) {
    builder.append(formatter.compose(ctx.sub(builder), node, traits));
  }
};

struct NextAction {
  ALWAYS_INLINE void run(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node,
                         const token_traits_map_t& traits) {
    node.nextSiblingElement();
  }
};

// This does nothing, good for kicking off a chain of formatters
struct EpsilonAction {
  ALWAYS_INLINE void run(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node,
                         const token_traits_map_t& traits) {}
};

class ConstPredicate {
 private:
  bool result;

 public:
  ConstPredicate(bool result) : result(result) {}
  bool operator()(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node,
                  const token_traits_map_t& traits) {
    return result;
  }
};

template <class FMT>
class FitsFirstPredicate {
 private:
  FMT formatter;

 public:
  FitsFirstPredicate(FMT formatter) : formatter(formatter) {}

  bool operator()(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node,
                  const token_traits_map_t& traits) {
    CSTElement copy = node;
    wcl::doc doc = formatter.compose(ctx.sub(builder), copy, traits);
    return ctx.sub(builder)->last_width() + doc->first_width() <= MAX_COLUMN_WIDTH;
  }
};

template <class FMT>
class FitsAllPredicate {
 private:
  FMT formatter;

 public:
  FitsAllPredicate(FMT formatter) : formatter(formatter) {}

  bool operator()(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node,
                  const token_traits_map_t& traits) {
    CSTElement copy = node;
    wcl::doc doc = formatter.compose(ctx.sub(builder), copy, traits);
    return ctx.sub(builder)->last_width() + doc->first_width() <= MAX_COLUMN_WIDTH &&
           !doc->has_newline();
  }
};

template <class A, class B>
struct DepDeclType {
  using type = A;
};

template <class B>
struct DepDeclType<ctx_t, B> {
  using type = B;
};

template <class Predicate>
struct FmtPredicate {
  Predicate predicate;
  FmtPredicate(Predicate predicate) : predicate(predicate) {}

  template <
      class CTX,
      std::enable_if_t<
          std::is_same<bool, decltype(std::declval<typename DepDeclType<CTX, Predicate>::type>()(
                                 cst_id_t()))>::value,
          bool> = true>
  bool operator()(wcl::doc_builder& builder, CTX ctx, CSTElement& node,
                  const token_traits_map_t& traits) {
    return predicate(node.id());
  }

  template <
      class CTX,
      std::enable_if_t<
          std::is_same<bool,
                       decltype(std::declval<typename DepDeclType<CTX, Predicate>::type>()(
                           std::declval<wcl::doc_builder&>(), ctx_t(), std::declval<CSTElement&>(),
                           std::declval<const token_traits_map_t&>()))>::value,
          bool> = true>
  bool operator()(wcl::doc_builder& builder, CTX ctx, CSTElement& node,
                  const token_traits_map_t& traits) {
    return predicate(builder, ctx, node, traits);
  }
};

template <>
struct FmtPredicate<cst_id_t> {
  cst_id_t id;
  FmtPredicate(cst_id_t id) : id(id) {}
  bool operator()(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node,
                  const token_traits_map_t& traits) {
    return node.id() == id;
  }
};

template <>
struct FmtPredicate<int> {
  int id;
  FmtPredicate(int id) : id(id) {}
  bool operator()(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node,
                  const token_traits_map_t& traits) {
    return node.id() == id;
  }
};

// if sizeof(cst_id_t) is increased then
// std::bitset<256> set from FmtPredicate needs to be increased
static_assert(sizeof(cst_id_t) == 1, "bitset size must match type size");

template <class T>
struct FmtPredicate<std::initializer_list<T>> {
  std::bitset<256> set;
  FmtPredicate(std::initializer_list<T> ids) {
    for (T id : ids) {
      set[id] = true;
    }
  }

  bool operator()(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node,
                  const token_traits_map_t& traits) {
    return set[node.id()];
  }
};

template <class Predicate, class FMT>
struct PredicateCase {
  Predicate predicate;
  FMT formatter;

  PredicateCase(Predicate predicate, FMT formatter) : predicate(predicate), formatter(formatter) {}

  ALWAYS_INLINE bool run(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node,
                         const token_traits_map_t& traits) {
    if (!predicate(builder, ctx, node, traits)) {
      return false;
    }
    builder.append(formatter.compose(ctx.sub(builder), node, traits));
    return true;
  }
};

template <class FMT>
struct OtherwiseCase {
  FMT formatter;

  OtherwiseCase(FMT formatter) : formatter(formatter) {}

  ALWAYS_INLINE bool run(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node,
                         const token_traits_map_t& traits) {
    builder.append(formatter.compose(ctx.sub(builder), node, traits));
    return true;
  }
};

template <class Case1, class Case2>
struct MatchSeq {
  Case1 case1;
  Case2 case2;
  MatchSeq(Case1 c1, Case2 c2) : case1(c1), case2(c2) {}

  ALWAYS_INLINE bool run(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node,
                         const token_traits_map_t& traits) {
    if (case1.run(builder, ctx, node, traits)) {
      return true;
    }
    return case2.run(builder, ctx, node, traits);
  }
};

template <class Case>
struct MatchAction {
  Case c;

  MatchAction(Case c) : c(c) {}

  // Predicate case that is accepted if FMT passes the FitsFirstPredicate
  template <class FMT>
  MatchAction<MatchSeq<Case, PredicateCase<FitsFirstPredicate<FMT>, FMT>>> pred_fits_first(
      FMT formatter) {
    return {{c, {FitsFirstPredicate<FMT>(formatter), formatter}}};
  }

  // Predicate case that is accepted if FMT passes the FitsAllPredicate
  template <class FMT>
  MatchAction<MatchSeq<Case, PredicateCase<FitsAllPredicate<FMT>, FMT>>> pred_fits_all(
      FMT formatter) {
    return {{c, {FitsAllPredicate<FMT>(formatter), formatter}}};
  }

  template <class FMT>
  MatchAction<MatchSeq<Case, PredicateCase<FmtPredicate<std::initializer_list<uint8_t>>, FMT>>>
  pred(std::initializer_list<uint8_t> ids, FMT formatter) {
    return {{c, {ids, formatter}}};
  }

  template <class Predicate, class FMT>
  MatchAction<MatchSeq<Case, PredicateCase<FmtPredicate<Predicate>, FMT>>> pred(Predicate predicate,
                                                                                FMT formatter) {
    return {{c, {predicate, formatter}}};
  }

  template <class FMT>
  MatchAction<MatchSeq<Case, OtherwiseCase<FMT>>> otherwise(FMT formatter) {
    return {{c, {formatter}}};
  }

  ALWAYS_INLINE void run(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node,
                         const token_traits_map_t& traits) {
    assert(c.run(builder, ctx, node, traits));
  }
};

template <class Action>
struct Formatter {
  Action action;

  Formatter(Action a) : action(a) {}

  Formatter<SeqAction<Action, ConsumeWhitespaceAction>> consume_wsnlc() { return {{action, {}}}; }

  Formatter<SeqAction<Action, WhitespaceTokenAction>> ws() { return {{action, {}}}; }

  Formatter<SeqAction<Action, SpaceAction>> space(uint8_t count = 1) { return {{action, {count}}}; }

  Formatter<SeqAction<Action, NewlineAction>> newline() { return {{action, {}}}; }

  Formatter<SeqAction<Action, FreshlineAction>> freshline() { return {{action, {}}}; }

  Formatter<SeqAction<Action, LiteralAction>> lit(wcl::doc lit) { return {{action, {lit}}}; }

  Formatter<SeqAction<Action, TokenAction>> token(cst_id_t id) { return {{action, {id}}}; }

  Formatter<SeqAction<Action, TokenReplaceAction>> token(cst_id_t id, const char* str) {
    return {{action, {id, str}}};
  }

  template <class FMT>
  Formatter<SeqAction<Action, NestAction<FMT>>> nest(FMT formatter) {
    return {{action, {formatter}}};
  }

  Formatter<SeqAction<Action, NextAction>> next() { return {{action, {}}}; }

  template <class IFMT, class EFMT>
  Formatter<SeqAction<Action, IfElseAction<FmtPredicate<FitsFirstPredicate<IFMT>>, IFMT, EFMT>>>
  fmt_if_fits_first(IFMT fits_formatter, EFMT else_formatter) {
    return fmt_if_else(FitsFirstPredicate<IFMT>(fits_formatter), fits_formatter, else_formatter);
  }

  template <class IFMT, class EFMT>
  Formatter<SeqAction<Action, IfElseAction<FmtPredicate<FitsAllPredicate<IFMT>>, IFMT, EFMT>>>
  fmt_if_fits_all(IFMT fits_formatter, EFMT else_formatter) {
    return fmt_if_else(FitsAllPredicate<IFMT>(fits_formatter), fits_formatter, else_formatter);
  }

  template <class FMT>
  Formatter<SeqAction<Action, IfElseAction<FmtPredicate<std::initializer_list<cst_id_t>>, FMT,
                                           Formatter<EpsilonAction>>>>
  fmt_if(std::initializer_list<cst_id_t> ids, FMT formatter) {
    return fmt_if_else(ids, formatter, Formatter<EpsilonAction>({}));
  }

  template <class FMT, class Predicate>
  Formatter<SeqAction<Action, IfElseAction<FmtPredicate<Predicate>, FMT, Formatter<EpsilonAction>>>>
  fmt_if(Predicate predicate, FMT formatter) {
    return fmt_if_else(predicate, formatter, Formatter<EpsilonAction>({}));
  }

  template <class IFMT, class EFMT>
  Formatter<
      SeqAction<Action, IfElseAction<FmtPredicate<std::initializer_list<cst_id_t>>, IFMT, EFMT>>>
  fmt_if_else(std::initializer_list<cst_id_t> ids, IFMT if_formatter, EFMT else_formatter) {
    return {{action, {ids, if_formatter, else_formatter}}};
  }

  template <class IFMT, class EFMT, class Predicate>
  Formatter<SeqAction<Action, IfElseAction<FmtPredicate<Predicate>, IFMT, EFMT>>> fmt_if_else(
      Predicate predicate, IFMT if_formatter, EFMT else_formatter) {
    return {{action, {predicate, if_formatter, else_formatter}}};
  }

  template <class FMT>
  Formatter<SeqAction<Action, WhileAction<FmtPredicate<std::initializer_list<cst_id_t>>, FMT>>>
  fmt_while(std::initializer_list<cst_id_t> ids, FMT formatter) {
    return {{action, {ids, formatter}}};
  }

  template <class FMT, class Predicate>
  Formatter<SeqAction<Action, WhileAction<FmtPredicate<Predicate>, FMT>>> fmt_while(
      Predicate predicate, FMT formatter) {
    return {{action, {predicate, formatter}}};
  }

  template <class Case>
  Formatter<SeqAction<Action, MatchAction<Case>>> match(MatchAction<Case> match_action) {
    return {{action, match_action}};
  }

  template <class Walker>
  Formatter<SeqAction<Action, WalkPredicateAction<FmtPredicate<ConstPredicate>, Walker>>> walk(
      Walker texas_ranger) {
    return walk(ConstPredicate(true), texas_ranger);
  }

  template <class Walker>
  Formatter<
      SeqAction<Action, WalkPredicateAction<FmtPredicate<std::initializer_list<cst_id_t>>, Walker>>>
  walk(std::initializer_list<cst_id_t> ids, Walker texas_ranger) {
    return {{action, {ids, texas_ranger}}};
  }

  template <class Predicate, class Walker>
  Formatter<SeqAction<Action, WalkPredicateAction<FmtPredicate<Predicate>, Walker>>> walk(
      Predicate predicate, Walker texas_ranger) {
    return {{action, {predicate, texas_ranger}}};
  }

  template <class FMT>
  Formatter<SeqAction<Action, WalkAllAction<FMT>>> walk_all(FMT formatter) {
    return {{action, {formatter}}};
  }

  template <class FMT>
  Formatter<SeqAction<Action, JoinAction<FMT>>> join(FMT formatter) {
    return {{action, {formatter}}};
  }

  template <class F>
  Formatter<SeqAction<Action, EscapeAction<F>>> escape(F f) {
    return {{action, {f}}};
  }

  wcl::doc format(ctx_t ctx, CSTElement node, const token_traits_map_t& traits) {
    wcl::doc_builder builder;
    action.run(builder, ctx, node, traits);
    if (!node.empty()) {
      std::cerr << "Not empty: " << +node.id() << std::endl;
      std::cerr << "Failed at: " << std::move(builder).build().as_string() << std::endl;
    }
    assert(node.empty());
    return std::move(builder).build();
  }

  wcl::doc compose(ctx_t ctx, CSTElement& node, const token_traits_map_t& traits) {
    wcl::doc_builder builder;
    action.run(builder, ctx, node, traits);
    return std::move(builder).build();
  }
};

inline Formatter<EpsilonAction> fmt() { return {{}}; }

template <class Predicate, class FMT>
inline MatchAction<PredicateCase<FmtPredicate<Predicate>, FMT>> pred(Predicate predicate,
                                                                     FMT formatter) {
  return {{predicate, formatter}};
}

#undef ALWAYS_INLINE
