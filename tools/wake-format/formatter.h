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

#include <bitset>
#include <cassert>

#include "parser/cst.h"
#include "parser/parser.h"

#define ALWAYS_INLINE inline __attribute__((always_inline))

struct ctx_t {
  size_t width = 0;
  size_t nest_level = 0;

  ctx_t nest() {
    ctx_t copy = *this;
    copy.nest_level++;
    return copy;
  }

  ctx_t sub(const wcl::doc_builder& builder) {
    ctx_t copy = *this;
    if (builder.has_newline()) {
      copy.width = builder.last_width();
    } else {
      copy.width += builder.last_width();
    }
    return copy;
  }
};

inline void space(wcl::doc_builder& builder, uint8_t count) {
  for (uint8_t i = 0; i < count; i++) {
    builder.append(" ");
  }
}

struct ConsumeWhitespaceAction {
  ALWAYS_INLINE void run(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node) {
    while (!node.empty() && (node.id() == TOKEN_WS || node.id() == TOKEN_NL)) {
      node.nextSiblingElement();
    }
  }
};

struct SpaceAction {
  uint8_t count;
  SpaceAction(uint8_t count) : count(count) {}
  ALWAYS_INLINE void run(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node) {
    space(builder, count);
  }
};

struct NewlineAction {
  uint8_t space_per_indent;

  NewlineAction(uint8_t space_per_indent) : space_per_indent(space_per_indent) {}

  ALWAYS_INLINE void run(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node) {
    builder.append("\n");
    space(builder, space_per_indent * ctx.nest_level);
  }
};

struct TokenAction {
  uint8_t token_id;
  TokenAction(uint8_t token_id) : token_id(token_id) {}
  ALWAYS_INLINE void run(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node) {
    assert(node.id() == token_id);
    builder.append(node.fragment().segment().str());
    node.nextSiblingElement();
  }
};

struct TokenReplaceAction {
  uint8_t token_id;
  const char* str;

  TokenReplaceAction(uint8_t token_id, const char* str) : token_id(token_id), str(str) {}

  ALWAYS_INLINE void run(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node) {
    assert(node.id() == token_id);
    builder.append(str);
    node.nextSiblingElement();
  }
};

struct WhitespaceTokenAction : public TokenReplaceAction {
  WhitespaceTokenAction() : TokenReplaceAction(TOKEN_WS, " ") {}
};

template <class Action1, class Action2>
struct SeqAction {
  Action1 action1;
  Action2 action2;
  SeqAction(Action1 a1, Action2 a2) : action1(a1), action2(a2) {}

  ALWAYS_INLINE void run(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node) {
    action1.run(builder, ctx, node);
    action2.run(builder, ctx, node);
  }
};

template <class F, class P>
struct WalkPredicateAction {
  F walker;
  P predicate;

  WalkPredicateAction(F walk, P p) : walker(walk), predicate(p) {}

  ALWAYS_INLINE void run(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node) {
    bool result = predicate(node.id());
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

  ALWAYS_INLINE void run(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node) {
    builder.append(formatter.compose(ctx.nest(), node));
  }
};

template <class IFMT, class EFMT>
struct IfElseAction {
  IFMT if_formatter;
  EFMT else_formatter;
  uint8_t node_type;

  IfElseAction(IFMT if_formatter, EFMT else_formatter, uint8_t node_type)
      : if_formatter(if_formatter), else_formatter(else_formatter), node_type(node_type) {}

  ALWAYS_INLINE void run(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node) {
    if (node.id() == node_type) {
      builder.append(if_formatter.compose(ctx, node));
    } else {
      builder.append(else_formatter.compose(ctx, node));
    }
  }
};

template <class FMT>
struct WhileAction {
  FMT while_formatter;
  uint8_t node_type;

  WhileAction(FMT while_formatter, uint8_t node_type)
      : while_formatter(while_formatter), node_type(node_type) {}

  ALWAYS_INLINE void run(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node) {
    while (node.id() == node_type) {
      builder.append(while_formatter.compose(ctx.sub(builder), node));
    }
  }
};

template <class FMT>
struct WalkChildrenAction {
  FMT formatter;

  WalkChildrenAction(FMT formatter) : formatter(formatter) {}

  ALWAYS_INLINE void run(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node) {
    for (CSTElement child = node.firstChildElement(); !child.empty(); child.nextSiblingElement()) {
      // Here we can't assert that child is empty since we are processing
      // each child element in parts.
      builder.append(formatter.format(ctx.sub(builder), child, false));
    }
    node.nextSiblingElement();
  }
};

// This is the escape hatch to implement something else.
// NOTE: You're responsible for advancing the node!!!
template <class F>
struct EscapeAction {
  F f;
  EscapeAction(F f) : f(f) {}

  ALWAYS_INLINE void run(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node) {
    // You have to do everything yourself here, that's the price of an escape hatch
    // we can build up enough of these that it shouldn't be an issue however.
    f(builder, ctx, node);
  }
};

template <class FMT>
struct JoinAction {
  FMT formatter;

  JoinAction(FMT formatter) : formatter(formatter) {}

  ALWAYS_INLINE void run(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node) {
    builder.append(formatter.compose(ctx, node));
  }
};

// This does nothing, good for kicking off a chain of formatters
struct EpsilonAction {
  ALWAYS_INLINE void run(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node) {}
};

template <class Action>
struct Formatter {
  Action action;
  Formatter(Action a) : action(a) {}

  Formatter<SeqAction<Action, ConsumeWhitespaceAction>> consume_wsnl() {
    return {SeqAction<Action, ConsumeWhitespaceAction>(action, ConsumeWhitespaceAction{})};
  }

  Formatter<SeqAction<Action, WhitespaceTokenAction>> ws() {
    return {SeqAction<Action, WhitespaceTokenAction>(action, WhitespaceTokenAction{})};
  }

  Formatter<SeqAction<Action, SpaceAction>> space(uint8_t count = 1) {
    return {SeqAction<Action, SpaceAction>(action, SpaceAction{count})};
  }

  Formatter<SeqAction<Action, NewlineAction>> newline(uint8_t space_per_indent) {
    return {SeqAction<Action, NewlineAction>(action, NewlineAction{space_per_indent})};
  }

  Formatter<SeqAction<Action, TokenAction>> token(uint8_t id) {
    return {SeqAction<Action, TokenAction>(action, TokenAction{id})};
  }

  Formatter<SeqAction<Action, TokenReplaceAction>> token(uint8_t id, const char* str) {
    return {SeqAction<Action, TokenReplaceAction>(action, TokenReplaceAction{id, str})};
  }

  template <class FMT>
  Formatter<SeqAction<Action, NestAction<FMT>>> nest(FMT formatter) {
    return {SeqAction<Action, NestAction<FMT>>(action, NestAction<FMT>{formatter})};
  }

  template <class FMT>
  Formatter<SeqAction<Action, IfElseAction<FMT, Formatter<EpsilonAction>>>> fmt_if(
      uint8_t node_type, FMT formatter) {
    return {SeqAction<Action, IfElseAction<FMT, Formatter<EpsilonAction>>>(
        action, IfElseAction<FMT, Formatter<EpsilonAction>>{formatter, Formatter<EpsilonAction>({}),
                                                            node_type})};
  }

  template <class IFMT, class EFMT>
  Formatter<SeqAction<Action, IfElseAction<IFMT, EFMT>>> fmt_if_else(uint8_t node_type,
                                                                     IFMT if_formatter,
                                                                     EFMT else_formatter) {
    return {SeqAction<Action, IfElseAction<IFMT, EFMT>>(
        action, IfElseAction<IFMT, EFMT>{if_formatter, else_formatter, node_type})};
  }

  template <class FMT>
  Formatter<SeqAction<Action, WhileAction<FMT>>> fmt_while(uint8_t node_type, FMT formatter) {
    return {SeqAction<Action, WhileAction<FMT>>(action, WhileAction<FMT>{formatter, node_type})};
  }

  class TruePredicate {
   public:
    bool operator()(uint8_t t) { return true; }
  };

  template <class Walker>
  Formatter<SeqAction<Action, WalkPredicateAction<Walker, TruePredicate>>> walk(
      Walker texas_ranger) {
    return walk(TruePredicate(), texas_ranger);
  }

  class InitListMembershipPredicate {
   private:
    std::bitset<256> type_bits;

   public:
    InitListMembershipPredicate(std::initializer_list<uint8_t> types) {
      // TODO: static assert(sizeof(token type) == 1)
      for (uint8_t t : types) {
        type_bits[t] = true;
      }
    }

    bool operator()(uint8_t type) { return type_bits[type]; }
  };

  template <class Walker>
  Formatter<SeqAction<Action, WalkPredicateAction<Walker, InitListMembershipPredicate>>> walk(
      uint8_t type, Walker texas_ranger) {
    return walk(InitListMembershipPredicate({type}), texas_ranger);
  }

  template <class Walker>
  Formatter<SeqAction<Action, WalkPredicateAction<Walker, InitListMembershipPredicate>>> walk(
      std::initializer_list<uint8_t> types, Walker texas_ranger) {
    return walk(InitListMembershipPredicate(types), texas_ranger);
  }

  template <
      class Predicate, class Walker,
      std::enable_if_t<std::is_same<bool, decltype(std::declval<Predicate>()(uint8_t()))>::value,
                       bool> = true>
  Formatter<SeqAction<Action, WalkPredicateAction<Walker, Predicate>>> walk(Predicate predicate,
                                                                            Walker texas_ranger) {
    return {SeqAction<Action, WalkPredicateAction<Walker, Predicate>>(
        action, WalkPredicateAction<Walker, Predicate>(texas_ranger, predicate))};
  }

  template <class FMT>
  Formatter<SeqAction<Action, WalkChildrenAction<FMT>>> walk_children(FMT formatter) {
    return {SeqAction<Action, WalkChildrenAction<FMT>>(action, WalkChildrenAction<FMT>{formatter})};
  }

  template <class FMT>
  Formatter<SeqAction<Action, JoinAction<FMT>>> join(FMT formatter) {
    return {SeqAction<Action, JoinAction<FMT>>(action, JoinAction<FMT>{formatter})};
  }

  template <class F>
  Formatter<SeqAction<Action, EscapeAction<F>>> escape(F f) {
    return {SeqAction<Action, EscapeAction<F>>(action, EscapeAction<F>{f})};
  }

  wcl::doc format(ctx_t ctx, CSTElement node, bool assert_empty = true) {
    wcl::doc_builder builder;
    action.run(builder, ctx, node);
    if (assert_empty) {
      if (!node.empty()) {
        std::cerr << "Not empty: " << +node.id() << std::endl;
        std::cerr << "Failed at: " << std::move(builder).build().as_string() << std::endl;
      }
      assert(node.empty());
    }
    return std::move(builder).build();
  }

  wcl::doc compose(ctx_t ctx, CSTElement& node) {
    wcl::doc_builder builder;
    action.run(builder, ctx, node);
    return std::move(builder).build();
  }
};

inline Formatter<EpsilonAction> formatter() { return Formatter<EpsilonAction>({}); }

#undef ALWAYS_INLINE
