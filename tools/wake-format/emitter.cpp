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

// Open Group Base Specifications Issue 7
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include "emitter.h"

#include <cassert>

#include "parser/parser.h"

#define ASSERT_TOKEN(node, token) \
  assert(!node.empty());          \
  assert(node.id() == token);

#define APPEND_OR_BUBBLE(builder, func, ctx, node) \
  {                                                \
    auto subtree = func(ctx, node);                \
    if (!subtree) {                                \
      return {};                                   \
    }                                              \
    builder.append(*subtree);                      \
  }

#define CONSUME_WS_NL(node)                                                     \
  {                                                                             \
    while (!node.empty() && (node.id() == TOKEN_WS || node.id() == TOKEN_NL)) { \
      node.nextSiblingElement();                                                \
    }                                                                           \
  }

wcl::doc Emitter::newline(ctx_t ctx) {
  wcl::doc_builder builder;

  builder.append("\n");
  for (size_t i = 0; i < ctx.nest_level; i++) {
    builder.append(space(space_per_indent));
  }

  return std::move(builder).build();
}

wcl::doc Emitter::space(uint8_t count) {
  wcl::doc_builder builder;
  for (uint8_t i = 0; i < count; i++) {
    builder.append(" ");
  }
  return std::move(builder).build();
}

wcl::optional<wcl::doc> Emitter::flat(ctx_t ctx, CSTElement node) {
  // Try to get the flat version of node
  auto flat_node = walk_node(ctx.flat(), node);
  if (!flat_node) {
    return {};
  }

  if (flat_node->last_width() > max_column_width) {
    return {};
  }

  return flat_node;
}

wcl::optional<wcl::doc> Emitter::flat_or(ctx_t ctx, CSTElement node) {
  // Try to get the flat version of node
  auto flat_node = flat(ctx, node);
  if (flat_node) {
    return flat_node;
  }

  // flat_or was called within a flat context, so the flat failure
  // needs to be bubbled up.
  if (ctx.is_flat) {
    return {};
  }

  // fallback to full node
  return walk_node(ctx, node);
}

wcl::doc Emitter::layout(CST cst) {
  ctx_t ctx;
  return walk(ctx, cst.root()).concat(newline(ctx));
}

wcl::doc Emitter::walk(ctx_t ctx, CSTElement node) {
  wcl::doc_builder builder;

  for (CSTElement child = node.firstChildElement(); !child.empty(); child.nextSiblingElement()) {
    // remove optional whitespace
    if (child.id() == TOKEN_WS) {
      continue;
    }

    if (child.id() == TOKEN_NL) {
      builder.append(newline(ctx));
      continue;
    }

    if (child.id() == TOKEN_COMMENT) {
      builder.append(walk_token(ctx, child));
      continue;
    }

    assert(child.isNode());

    auto r = flat_or(ctx, child);
    assert(r);
    builder.append(*r);
    builder.append(newline(ctx));
  }

  builder.undo();

  return std::move(builder).build();
}

wcl::optional<wcl::doc> Emitter::walk_node(ctx_t ctx, CSTElement node) {
  assert(node.isNode());

  wcl::doc_builder builder;

  switch (node.id()) {
    case CST_ARITY:
      APPEND_OR_BUBBLE(builder, walk_arity, ctx, node);
      break;
    case CST_APP:
      APPEND_OR_BUBBLE(builder, walk_apply, ctx, node);
      break;
    case CST_ASCRIBE:
      APPEND_OR_BUBBLE(builder, walk_ascribe, ctx, node);
      break;
    case CST_BINARY:
      APPEND_OR_BUBBLE(builder, walk_binary, ctx, node);
      break;
    case CST_BLOCK:
      APPEND_OR_BUBBLE(builder, walk_block, ctx, node);
      break;
    case CST_CASE:
      APPEND_OR_BUBBLE(builder, walk_case, ctx, node);
      break;
    case CST_DATA:
      APPEND_OR_BUBBLE(builder, walk_data, ctx, node);
      break;
    case CST_DEF:
      APPEND_OR_BUBBLE(builder, walk_def, ctx, node);
      break;
    case CST_EXPORT:
      APPEND_OR_BUBBLE(builder, walk_export, ctx, node);
      break;
    case CST_FLAG_EXPORT:
      APPEND_OR_BUBBLE(builder, walk_flag_export, ctx, node);
      break;
    case CST_FLAG_GLOBAL:
      APPEND_OR_BUBBLE(builder, walk_flag_global, ctx, node);
      break;
    case CST_GUARD:
      APPEND_OR_BUBBLE(builder, walk_guard, ctx, node);
      break;
    case CST_HOLE:
      APPEND_OR_BUBBLE(builder, walk_hole, ctx, node);
      break;
    case CST_ID:
      APPEND_OR_BUBBLE(builder, walk_identifier, ctx, node);
      break;
    case CST_IDEQ:
      APPEND_OR_BUBBLE(builder, walk_ideq, ctx, node);
      break;
    case CST_IF:
      APPEND_OR_BUBBLE(builder, walk_if, ctx, node);
      break;
    case CST_IMPORT:
      APPEND_OR_BUBBLE(builder, walk_import, ctx, node);
      break;
    case CST_INTERPOLATE:
      APPEND_OR_BUBBLE(builder, walk_interpolate, ctx, node);
      break;
    case CST_KIND:
      APPEND_OR_BUBBLE(builder, walk_kind, ctx, node);
      break;
    case CST_LAMBDA:
      APPEND_OR_BUBBLE(builder, walk_lambda, ctx, node);
      break;
    case CST_LITERAL:
      APPEND_OR_BUBBLE(builder, walk_literal, ctx, node);
      break;
    case CST_MATCH:
      APPEND_OR_BUBBLE(builder, walk_match, ctx, node);
      break;
    case CST_OP:
      APPEND_OR_BUBBLE(builder, walk_op, ctx, node);
      break;
    case CST_PACKAGE:
      APPEND_OR_BUBBLE(builder, walk_package, ctx, node);
      break;
    case CST_PAREN:
      APPEND_OR_BUBBLE(builder, walk_paren, ctx, node);
      break;
    case CST_PRIM:
      APPEND_OR_BUBBLE(builder, walk_prim, ctx, node);
      break;
    case CST_PUBLISH:
      APPEND_OR_BUBBLE(builder, walk_publish, ctx, node);
      break;
    case CST_REQUIRE:
      APPEND_OR_BUBBLE(builder, walk_require, ctx, node);
      break;
    case CST_REQ_ELSE:
      APPEND_OR_BUBBLE(builder, walk_req_else, ctx, node);
      break;
    case CST_SUBSCRIBE:
      APPEND_OR_BUBBLE(builder, walk_subscribe, ctx, node);
      break;
    case CST_TARGET:
      APPEND_OR_BUBBLE(builder, walk_target, ctx, node);
      break;
    case CST_TARGET_ARGS:
      APPEND_OR_BUBBLE(builder, walk_target_args, ctx, node);
      break;
    case CST_TOP:
      APPEND_OR_BUBBLE(builder, walk_top, ctx, node);
      break;
    case CST_TOPIC:
      APPEND_OR_BUBBLE(builder, walk_topic, ctx, node);
      break;
    case CST_TUPLE:
      APPEND_OR_BUBBLE(builder, walk_tuple, ctx, node);
      break;
    case CST_TUPLE_ELT:
      APPEND_OR_BUBBLE(builder, walk_tuple_elt, ctx, node);
      break;
    case CST_UNARY:
      APPEND_OR_BUBBLE(builder, walk_unary, ctx, node);
      break;
    case CST_ERROR:
      APPEND_OR_BUBBLE(builder, walk_error, ctx, node);
      break;
    default:
      assert(false);
  }

  return wcl::optional<wcl::doc>(wcl::in_place_t{}, std::move(builder).build());
}

wcl::optional<wcl::doc> Emitter::walk_placeholder(ctx_t ctx, CSTElement node) {
  assert(node.isNode());

  wcl::doc_builder builder;

  for (CSTElement child = node.firstChildElement(); !child.empty(); child.nextSiblingElement()) {
    if (child.isNode()) {
      auto full_child = walk_node(ctx, child);
      assert(full_child);
      builder.append(*full_child);
    } else {
      builder.append(walk_token(ctx, child));
    }
  }

  return wcl::optional<wcl::doc>(wcl::in_place_t{}, std::move(builder).build());
}

wcl::doc Emitter::walk_token(ctx_t ctx, CSTElement node) {
  assert(!node.isNode());
  switch (node.id()) {
    case TOKEN_KW_MACRO_HERE:
      return wcl::doc::lit("@here");
    case TOKEN_NL: {
      if (ctx.is_flat) {
        return wcl::doc::lit(" ");
      }
      return wcl::doc::lit("\n");
    }
    case TOKEN_WS: {
      return wcl::doc::lit(" ");
    }
    case TOKEN_COMMENT:
    case TOKEN_P_BOPEN:
    case TOKEN_P_BCLOSE:
    case TOKEN_P_SOPEN:
    case TOKEN_P_SCLOSE:
    case TOKEN_ID:
    case TOKEN_INDENT:
    case TOKEN_DEDENT:
    case TOKEN_KW_PACKAGE:
    case TOKEN_KW_FROM:
    case TOKEN_KW_IMPORT:
    case TOKEN_P_HOLE:
    case TOKEN_KW_EXPORT:
    case TOKEN_KW_DEF:
    case TOKEN_KW_TYPE:
    case TOKEN_KW_TOPIC:
    case TOKEN_KW_UNARY:
    case TOKEN_KW_BINARY:
    case TOKEN_P_EQUALS:
    case TOKEN_OP_DOT:
    case TOKEN_OP_QUANT:
    case TOKEN_OP_EXP:
    case TOKEN_OP_MULDIV:
    case TOKEN_OP_ADDSUB:
    case TOKEN_OP_COMPARE:
    case TOKEN_OP_INEQUAL:
    case TOKEN_OP_AND:
    case TOKEN_OP_OR:
    case TOKEN_OP_DOLLAR:
    case TOKEN_OP_ASSIGN:
    case TOKEN_OP_COMMA:
    case TOKEN_KW_GLOBAL:
    case TOKEN_P_ASCRIBE:
    case TOKEN_KW_PUBLISH:
    case TOKEN_KW_DATA:
    case TOKEN_KW_TUPLE:
    case TOKEN_KW_TARGET:
    case TOKEN_P_BSLASH:
    case TOKEN_P_POPEN:
    case TOKEN_P_PCLOSE:
    case TOKEN_STR_RAW:
    case TOKEN_STR_SINGLE:
    case TOKEN_STR_MID:
    case TOKEN_STR_OPEN:
    case TOKEN_STR_CLOSE:
    case TOKEN_MSTR_CONTINUE:
    case TOKEN_MSTR_BEGIN:
    case TOKEN_MSTR_END:
    case TOKEN_MSTR_PAUSE:
    case TOKEN_MSTR_MID:
    case TOKEN_MSTR_RESUME:
    case TOKEN_LSTR_CONTINUE:
    case TOKEN_LSTR_BEGIN:
    case TOKEN_LSTR_END:
    case TOKEN_LSTR_PAUSE:
    case TOKEN_LSTR_MID:
    case TOKEN_LSTR_RESUME:
    case TOKEN_REG_SINGLE:
    case TOKEN_REG_MID:
    case TOKEN_REG_OPEN:
    case TOKEN_REG_CLOSE:
    case TOKEN_DOUBLE:
    case TOKEN_INTEGER:
    case TOKEN_KW_MACRO_LINE:
    case TOKEN_KW_MACRO_FILE:
    case TOKEN_KW_MACRO_BANG:
    case TOKEN_KW_SUBSCRIBE:
    case TOKEN_KW_PRIM:
    case TOKEN_KW_MATCH:
    case TOKEN_KW_IF:
    case TOKEN_KW_THEN:
    case TOKEN_KW_ELSE:
    case TOKEN_KW_REQUIRE:
      return wcl::doc::lit(node.fragment().segment().str());
    default:
      assert(false);
  }
}

wcl::optional<wcl::doc> Emitter::walk_rhs(ctx_t ctx, CSTElement node) {
  return walk_placeholder(ctx, node);
}

wcl::optional<wcl::doc> Emitter::walk_apply(ctx_t ctx, CSTElement node) {
  return walk_placeholder(ctx, node);
}

wcl::optional<wcl::doc> Emitter::walk_arity(ctx_t ctx, CSTElement node) {
  return walk_placeholder(ctx, node);
}

wcl::optional<wcl::doc> Emitter::walk_ascribe(ctx_t ctx, CSTElement node) {
  return walk_placeholder(ctx, node);
}

wcl::optional<wcl::doc> Emitter::walk_binary(ctx_t ctx, CSTElement node) {
  return walk_placeholder(ctx, node);
}

wcl::optional<wcl::doc> Emitter::walk_block(ctx_t ctx, CSTElement node) {
  ASSERT_TOKEN(node, CST_BLOCK);
  wcl::doc_builder builder;

  ctx = ctx.nest();

  for (CSTElement child = node.firstChildElement(); !child.empty(); child.nextSiblingElement()) {
    // remove optional whitespace
    if (child.id() == TOKEN_WS || child.id() == TOKEN_NL) {
      continue;
    }

    // No non-whitespace nodes should be in a block
    assert(child.isNode());
    {
      auto new_child = walk_node(ctx.sub(builder), child);
      assert(new_child);
      builder.append(*new_child);
    }
    builder.append(newline(ctx));
  }

  builder.undo();

  return wcl::optional<wcl::doc>(wcl::in_place_t{}, std::move(builder).build());
}

wcl::optional<wcl::doc> Emitter::walk_case(ctx_t ctx, CSTElement node) {
  return walk_placeholder(ctx, node);
}

wcl::optional<wcl::doc> Emitter::walk_data(ctx_t ctx, CSTElement node) {
  return walk_placeholder(ctx, node);
}

wcl::optional<wcl::doc> Emitter::walk_def(ctx_t ctx, CSTElement node) {
  ASSERT_TOKEN(node, CST_DEF);
  wcl::doc_builder builder;
  CSTElement child = node.firstChildElement();

  // def shouldn't follow anything other than indentation.
  if (ctx.width > ctx.nest_level * space_per_indent) {
    builder.append(newline(ctx));
  }

  // def
  ASSERT_TOKEN(child, TOKEN_KW_DEF);
  builder.append("def");
  child.nextSiblingElement();

  // ws
  ASSERT_TOKEN(child, TOKEN_WS);
  builder.append(space());
  child.nextSiblingElement();

  // CST_ID, CST_APPLY, TODO: others?
  assert(!child.empty());
  assert(child.id() == CST_ID || child.id() == CST_APP);
  {
    auto new_child = walk_node(ctx.sub(builder), child);
    assert(new_child);
    builder.append(*new_child);
  }
  child.nextSiblingElement();

  // ws
  ASSERT_TOKEN(child, TOKEN_WS);
  builder.append(space());
  child.nextSiblingElement();

  //  =
  ASSERT_TOKEN(child, TOKEN_P_EQUALS);
  builder.append("=");
  child.nextSiblingElement();

  // optional ws/nl
  CONSUME_WS_NL(child);

  // rhs
  {
    auto new_child = walk_node(ctx.sub(builder), child);
    assert(new_child);
    wcl::doc new_doc = *new_child;
    new_doc = space().concat(new_doc);
    if (builder.last_width() + new_doc.first_width() + ctx.width <= max_column_width) {
      builder.append(new_doc);
    } else {
      ctx_t n_ctx = ctx.nest();
      builder.append(newline(n_ctx));
      auto full_child = walk_node(n_ctx.sub(builder), child);
      assert(full_child);
      builder.append(*full_child);
    }
  }
  child.nextSiblingElement();

  // optional ws/nl
  CONSUME_WS_NL(child);

  assert(child.empty());
  return wcl::optional<wcl::doc>(wcl::in_place_t{}, std::move(builder).build());
}

wcl::optional<wcl::doc> Emitter::walk_export(ctx_t ctx, CSTElement node) {
  return walk_placeholder(ctx, node);
}

wcl::optional<wcl::doc> Emitter::walk_flag_export(ctx_t ctx, CSTElement node) {
  return walk_placeholder(ctx, node);
}

wcl::optional<wcl::doc> Emitter::walk_flag_global(ctx_t ctx, CSTElement node) {
  return walk_placeholder(ctx, node);
}

wcl::optional<wcl::doc> Emitter::walk_guard(ctx_t ctx, CSTElement node) {
  return walk_placeholder(ctx, node);
}

wcl::optional<wcl::doc> Emitter::walk_hole(ctx_t ctx, CSTElement node) {
  return walk_placeholder(ctx, node);
}

wcl::optional<wcl::doc> Emitter::walk_identifier(ctx_t ctx, CSTElement node) {
  return walk_placeholder(ctx, node);
}

wcl::optional<wcl::doc> Emitter::walk_ideq(ctx_t ctx, CSTElement node) {
  return walk_placeholder(ctx, node);
}

wcl::optional<wcl::doc> Emitter::walk_if(ctx_t ctx, CSTElement node) {
  return walk_placeholder(ctx, node);
}

wcl::optional<wcl::doc> Emitter::walk_import(ctx_t ctx, CSTElement node) {
  return walk_placeholder(ctx, node);
}

wcl::optional<wcl::doc> Emitter::walk_interpolate(ctx_t ctx, CSTElement node) {
  return walk_placeholder(ctx, node);
}

wcl::optional<wcl::doc> Emitter::walk_kind(ctx_t ctx, CSTElement node) {
  return walk_placeholder(ctx, node);
}

wcl::optional<wcl::doc> Emitter::walk_lambda(ctx_t ctx, CSTElement node) {
  return walk_placeholder(ctx, node);
}

wcl::optional<wcl::doc> Emitter::walk_literal(ctx_t ctx, CSTElement node) {
  return walk_placeholder(ctx, node);
}

wcl::optional<wcl::doc> Emitter::walk_match(ctx_t ctx, CSTElement node) {
  return walk_placeholder(ctx, node);
}

wcl::optional<wcl::doc> Emitter::walk_op(ctx_t ctx, CSTElement node) {
  return walk_placeholder(ctx, node);
}

wcl::optional<wcl::doc> Emitter::walk_package(ctx_t ctx, CSTElement node) {
  return walk_placeholder(ctx, node);
}

wcl::optional<wcl::doc> Emitter::walk_paren(ctx_t ctx, CSTElement node) {
  return walk_placeholder(ctx, node);
}

wcl::optional<wcl::doc> Emitter::walk_prim(ctx_t ctx, CSTElement node) {
  return walk_placeholder(ctx, node);
}

wcl::optional<wcl::doc> Emitter::walk_publish(ctx_t ctx, CSTElement node) {
  return walk_placeholder(ctx, node);
}

wcl::optional<wcl::doc> Emitter::walk_require(ctx_t ctx, CSTElement node) {
  return walk_placeholder(ctx, node);
}

wcl::optional<wcl::doc> Emitter::walk_req_else(ctx_t ctx, CSTElement node) {
  return walk_placeholder(ctx, node);
}

wcl::optional<wcl::doc> Emitter::walk_subscribe(ctx_t ctx, CSTElement node) {
  return walk_placeholder(ctx, node);
}

wcl::optional<wcl::doc> Emitter::walk_target(ctx_t ctx, CSTElement node) {
  return walk_placeholder(ctx, node);
}

wcl::optional<wcl::doc> Emitter::walk_target_args(ctx_t ctx, CSTElement node) {
  return walk_placeholder(ctx, node);
}

wcl::optional<wcl::doc> Emitter::walk_top(ctx_t ctx, CSTElement node) {
  return walk_placeholder(ctx, node);
}

wcl::optional<wcl::doc> Emitter::walk_topic(ctx_t ctx, CSTElement node) {
  return walk_placeholder(ctx, node);
}

wcl::optional<wcl::doc> Emitter::walk_tuple(ctx_t ctx, CSTElement node) {
  return walk_placeholder(ctx, node);
}

wcl::optional<wcl::doc> Emitter::walk_tuple_elt(ctx_t ctx, CSTElement node) {
  return walk_placeholder(ctx, node);
}

wcl::optional<wcl::doc> Emitter::walk_unary(ctx_t ctx, CSTElement node) {
  return walk_placeholder(ctx, node);
}

wcl::optional<wcl::doc> Emitter::walk_error(ctx_t ctx, CSTElement node) {
  return walk_placeholder(ctx, node);
}

#undef CONSUME_WS_NL
#undef APPEND_OR_BUBBLE
#undef ASSERT_TOKEN
