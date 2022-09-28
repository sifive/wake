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

#include <iostream>

#include "parser/syntax.h"
#include "predicates.h"
#include "types.h"

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
      std::cerr << "Token mismatch! Expected <" << symbolName(token_id) << ">, Saw <"
                << symbolName(node.id()) << "> at " << node.location().filename << ":"
                << node.location().start.row << std::endl;
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
      std::cerr << "Token mismatch! Expected <" << symbolName(token_id) << ">, Saw <"
                << symbolName(node.id()) << "> at " << node.location().filename << ":"
                << node.location().start.row << std::endl;
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
      std::cerr << "Unexpected token <" << symbolName(node.id()) << "> at "
                << node.location().filename << ":" << node.location().start.row << std::endl;
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
