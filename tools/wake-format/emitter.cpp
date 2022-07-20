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

wcl::rope Emitter::newline(ctx_t ctx) {
  wcl::rope_builder builder;

  builder.append("⏎\n");
  for (int i = 0; i < ctx.nest_level; i++) {
    builder.append(space(space_per_indent));
  }

  return std::move(builder).build();
}

wcl::rope Emitter::space(uint8_t count) {
  wcl::rope_builder builder;
  for (uint8_t i = 0; i < count; i++) {
    builder.append("·");
  }
  return std::move(builder).build();
}

wcl::optional<wcl::rope> Emitter::flat(ctx_t ctx, CSTElement node) {
  return walk_node(ctx.flat(), node);
}

wcl::rope Emitter::try_flat(ctx_t ctx, CSTElement node) {
  auto flat_node = flat(ctx, node);
  if (flat_node && flat_node->column() <= max_column_width) {
    return *flat_node;
  }
  auto full_node = walk_node(ctx, node);
  assert(full_node);
  return *full_node;
}

#define ASSERT_TOKEN(node, token) \
  assert(!node.empty());          \
  assert(node.id() == token);

wcl::rope Emitter::layout(CST cst) {
  ctx_t ctx;
  return walk_top(ctx, cst.root()).concat(newline(ctx));
}

wcl::rope Emitter::walk_top(ctx_t ctx, CSTElement node) {
  wcl::rope_builder builder;

  for (CSTElement child = node.firstChildElement(); !child.empty(); child.nextSiblingElement()) {
    if (child.isNode()) {
      builder.append(try_flat(ctx, child));
      builder.append(newline(ctx));
    } else {
      builder.append(walk_token(ctx, child));
    }
  }

  // probably shouldn't do this unconditionally
  builder.undo();

  return std::move(builder).build();
}

wcl::rope Emitter::walk_block(ctx_t ctx, CSTElement node) {
  ASSERT_TOKEN(node, CST_BLOCK);
  return walk_top(ctx.nest(), node);
}

wcl::rope Emitter::walk_apply(ctx_t ctx, CSTElement node) {
  ASSERT_TOKEN(node, CST_APP);

  wcl::rope_builder builder;

  CSTElement child = node.firstChildElement();
  ASSERT_TOKEN(child, CST_ID);
  {
    auto node = walk_node(ctx, child);
    assert(node);
    builder.append(*node);
  }

  child.nextSiblingElement();
  ASSERT_TOKEN(child, TOKEN_WS);
  builder.append(space());

  child.nextSiblingElement();
  ASSERT_TOKEN(child, CST_ID);
  {
    auto node = walk_node(ctx, child);
    assert(node);
    builder.append(*node);
  }

  return std::move(builder).build();
}

wcl::rope Emitter::walk_def(ctx_t ctx, CSTElement node) {
  ASSERT_TOKEN(node, CST_DEF);

  wcl::rope_builder builder;

  CSTElement child = node.firstChildElement();
  ASSERT_TOKEN(child, TOKEN_KW_DEF);
  builder.append("def");

  child.nextSiblingElement();
  ASSERT_TOKEN(child, TOKEN_WS);
  builder.append(space());

  // This is the id/function name + signature
  child.nextSiblingElement();
  assert(child.isNode());
  builder.append(try_flat(ctx, child));

  child.nextSiblingElement();
  ASSERT_TOKEN(child, TOKEN_WS);
  builder.append(space());

  child.nextSiblingElement();
  ASSERT_TOKEN(child, TOKEN_P_EQUALS);
  builder.append("=");

  child.nextSiblingElement();
  while (!child.empty() && (child.id() == TOKEN_WS || child.id() == TOKEN_NL)) {
    child.nextSiblingElement();
  }

  if (ctx.is_flat) {
    builder.append(space());
  } else {
    builder.append(newline(ctx.nest()));
  }

  builder.append(try_flat(ctx, child));

  return std::move(builder).build();
}

wcl::optional<wcl::rope> Emitter::walk_node(ctx_t ctx, CSTElement node) {
  assert(node.isNode());

  wcl::rope_builder builder;

  switch (node.id()) {
    case CST_DEF:
      builder.append(walk_def(ctx, node));
      break;
    case CST_BLOCK:
      builder.append(walk_block(ctx, node));
      break;
    case CST_APP:
      builder.append(walk_apply(ctx, node));
      break;
    default: {
      for (CSTElement child = node.firstChildElement(); !child.empty();
           child.nextSiblingElement()) {
        if (child.isNode()) {
          auto full_child = walk_node(ctx, child);
          assert(full_child);
          builder.append(*full_child);
        } else {
          builder.append(walk_token(ctx, child));
        }
      }
      break;
    }
  }
  return wcl::optional<wcl::rope>(wcl::in_place_t{}, std::move(builder).build());
}

wcl::rope Emitter::walk_token(ctx_t ctx, CSTElement node) {
  assert(!node.isNode());
  switch (node.id()) {
    case TOKEN_KW_MACRO_HERE:
      return wcl::rope::lit("@here");
    case TOKEN_NL: {
      if (ctx.is_flat) {
        return space();
      }
      return newline(ctx);
    }
    case TOKEN_WS: {
      return wcl::rope::lit("");
      if (ctx.is_flat) {
        return space();
      }
      return wcl::rope::lit(node.fragment().segment().str());
    }
    default: {
      return wcl::rope::lit(node.fragment().segment().str());
    }
  }
}
