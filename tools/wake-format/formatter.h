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

#include <cassert>

#include "parser/cst.h"
#include "parser/parser.h"

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

// TODO: Mark all runs as "always inline"
struct ConsumeWhitespaceAction {
  void run(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node) {
    while (!node.empty() && (node.id() == TOKEN_WS || node.id() == TOKEN_NL)) {
      node.nextSiblingElement();
    }
  }
};

struct SpaceAction {
  uint8_t count;
  SpaceAction(uint8_t count) : count(count) {}
  void run(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node) { space(builder, count); }
};

struct NewlineAction {
  void run(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node) {
    builder.append("\n");
    space(builder, 4 * ctx.nest_level);
  }
};

struct TokenAction {
  uint8_t token_id;
  TokenAction(uint8_t token_id) : token_id(token_id) {}
  void run(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node) {
    assert(node.id() == token_id);
    builder.append(node.fragment().segment().str());
    node.nextSiblingElement();
  }
};

struct TokenReplaceAction {
  uint8_t token_id;
  const char* str;

  TokenReplaceAction(uint8_t token_id, const char* str) : token_id(token_id), str(str) {}

  void run(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node) {
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

  void run(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node) {
    action1.run(builder, ctx, node);
    action2.run(builder, ctx, node);
  }
};

template <class F>
struct WalkAction {
  F walker;
  uint8_t node_type;

  WalkAction(F walk, uint8_t node_type) : walker(walk), node_type(node_type) {}

  void run(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node) {
    // assert(node.id() == node_type);
    auto doc = walker(ctx, const_cast<const CSTElement&>(node));
    builder.append(doc);
    node.nextSiblingElement();
  }
};

// template <class F, class P>
// struct WalkPredicateAction {
//   F walker;
//   P predicate;

//   WalkPredicateAction(F walk, P p) : walker(walk), predicate(p) {}

//   void run(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node) {
//     auto pass = predicate(node);
//     std::cout << pass;
//     auto doc = walker(ctx, const_cast<const CSTElement&>(node));
//     builder.append(doc);
//     node.nextSiblingElement();
//   }
// };

// This is the escape hatch to implement something else.
// NOTE: You're responsible for advancing the node!!!
template <class F>
struct EscapeAction {
  F f;
  EscapeAction(F f) : f(f) {}

  void run(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node) {
    // You have to do everything yourself here, that's the price of an escape hatch
    // we can build up enough of these that it shouldn't be an issue however.
    f(builder, ctx, node);
  }
};

// This does nothing, good for kicking off a chain of formatters
struct EpsilonAction {
  void run(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node) {}
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

  Formatter<SeqAction<Action, NewlineAction>> newline() {
    return {SeqAction<Action, NewlineAction>(action, NewlineAction{})};
  }

  Formatter<SeqAction<Action, TokenAction>> token(uint8_t id) {
    return {SeqAction<Action, TokenAction>(action, TokenAction{id})};
  }

  Formatter<SeqAction<Action, TokenReplaceAction>> token(uint8_t id, const char* str) {
    return {SeqAction<Action, TokenReplaceAction>(action, TokenReplaceAction{id, str})};
  }

  template <class Walker>
  Formatter<SeqAction<Action, WalkAction<Walker>>> walk(uint8_t node_type, Walker texas_ranger) {
    return {
        SeqAction<Action, WalkAction<Walker>>(action, WalkAction<Walker>{texas_ranger, node_type})};
  }

  //   template <class Predicate, class Walker>
  //   Formatter<SeqAction<Action, WalkPredicateAction<Walker, Predicate>>> walk(Predicate
  //   predicate, Walker texas_ranger) {
  //     return {
  //         SeqAction<Action, WalkPredicateAction<Walker, Predicate>>(action,
  //         WalkPredicateAction<Walker, Predicate>{texas_ranger, predicate})};
  //   }

  template <class F>
  Formatter<SeqAction<Action, EscapeAction<F>>> escape(F f) {
    return {SeqAction<Action, EscapeAction<F>>(action, EscapeAction<F>{f})};
  }

  wcl::doc format(ctx_t ctx, CSTElement node) {
    wcl::doc_builder builder;
    action.run(builder, ctx, node);
    assert(node.empty());
    return std::move(builder).build();
  }
};

inline Formatter<EpsilonAction> formatter() { return Formatter<EpsilonAction>({}); }
