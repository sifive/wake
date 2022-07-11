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

Emitter::nest_t Emitter::nest(ctx_t ctx) { return nest_t(&ctx); }

wcl::rope Emitter::newline(ctx_t ctx) { return wcl::rope::lit("\n"); }

wcl::rope Emitter::space(ctx_t ctx, uint8_t count) {
  std::string spaces = "";
  wcl::rope_builder builder;
  for (uint8_t i = 0; i < count; i++) {
    builder.append(" ");
  }
  return std::move(builder).build();
}

wcl::optional<wcl::rope> Emitter::flat(ctx_t ctx, CSTElement node) {
  return walk_top(ctx.flat(), node);
}

void Emitter::layout(CST cst) {
  ctx_t ctx;
  wcl::optional<wcl::rope> r = walk_top(ctx, cst.root());
  r->write(*ostream);
}

wcl::optional<wcl::rope> Emitter::walk_top(ctx_t ctx, CSTElement node) {
  wcl::rope_builder builder;
  for (CSTElement child = node.firstChildElement(); !child.empty(); child.nextSiblingElement()) {
    switch (child.id()) {
      case CST_CASE: {
        auto flat_child = flat(ctx, child);
        if (flat_child && flat_child->size() < 120) {
          builder.append(*flat_child);
        } else {
          auto full_child = walk_top(ctx, child);
          assert(full_child);
          builder.append(*full_child);
        }
        break;
      }
      case TOKEN_KW_MACRO_HERE:
        builder.append("@here");
        break;
      case TOKEN_NL: {
        if (ctx.is_flat) {
          builder.append(" ");
        } else {
          builder.append("\n");
        }
        break;
      }
      case TOKEN_WS: {
        if (ctx.is_flat) {
          builder.append(" ");
        } else {
          builder.append(child.fragment().segment().str());
        }
        break;
      }
      default: {
        if (child.isNode()) {
          auto full_child = walk_top(ctx, child);
          assert(full_child);
          builder.append(*full_child);
        } else {
          builder.append(child.fragment().segment().str());
        }
        break;
      }
    }
  }
  return wcl::optional<wcl::rope>(wcl::in_place_t{}, std::move(builder).build());
}
