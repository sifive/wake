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
#include <wcl/optional.h>

#include <cassert>
#include <iostream>
#include <vector>

#include "parser/cst.h"

class Emitter {
 public:
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

  // Walks the CST, formats it, and returns the representative doc
  wcl::doc layout(CST cst);

 private:
  // Top level tree walk. Dispatches out the calls for various nodes
  wcl::doc walk(ctx_t ctx, CSTElement node);

  wcl::doc walk_node(ctx_t ctx, CSTElement node);
  wcl::doc walk_token(ctx_t ctx, CSTElement node);
  wcl::doc walk_rhs(ctx_t ctx, CSTElement node);

  wcl::doc walk_apply(ctx_t ctx, CSTElement node);
  wcl::doc walk_arity(ctx_t ctx, CSTElement node);
  wcl::doc walk_ascribe(ctx_t ctx, CSTElement node);
  wcl::doc walk_binary(ctx_t ctx, CSTElement node);
  wcl::doc walk_block(ctx_t ctx, CSTElement node);
  wcl::doc walk_case(ctx_t ctx, CSTElement node);
  wcl::doc walk_data(ctx_t ctx, CSTElement node);
  wcl::doc walk_def(ctx_t ctx, CSTElement node);
  wcl::doc walk_export(ctx_t ctx, CSTElement node);
  wcl::doc walk_flag_export(ctx_t ctx, CSTElement node);
  wcl::doc walk_flag_global(ctx_t ctx, CSTElement node);
  wcl::doc walk_guard(ctx_t ctx, CSTElement node);
  wcl::doc walk_hole(ctx_t ctx, CSTElement node);
  wcl::doc walk_identifier(ctx_t ctx, CSTElement node);
  wcl::doc walk_ideq(ctx_t ctx, CSTElement node);
  wcl::doc walk_if(ctx_t ctx, CSTElement node);
  wcl::doc walk_import(ctx_t ctx, CSTElement node);
  wcl::doc walk_interpolate(ctx_t ctx, CSTElement node);
  wcl::doc walk_kind(ctx_t ctx, CSTElement node);
  wcl::doc walk_lambda(ctx_t ctx, CSTElement node);
  wcl::doc walk_literal(ctx_t ctx, CSTElement node);
  wcl::doc walk_match(ctx_t ctx, CSTElement node);
  wcl::doc walk_op(ctx_t ctx, CSTElement node);
  wcl::doc walk_package(ctx_t ctx, CSTElement node);
  wcl::doc walk_paren(ctx_t ctx, CSTElement node);
  wcl::doc walk_prim(ctx_t ctx, CSTElement node);
  wcl::doc walk_publish(ctx_t ctx, CSTElement node);
  wcl::doc walk_require(ctx_t ctx, CSTElement node);
  wcl::doc walk_req_else(ctx_t ctx, CSTElement node);
  wcl::doc walk_subscribe(ctx_t ctx, CSTElement node);
  wcl::doc walk_target(ctx_t ctx, CSTElement node);
  wcl::doc walk_target_args(ctx_t ctx, CSTElement node);
  wcl::doc walk_top(ctx_t ctx, CSTElement node);
  wcl::doc walk_topic(ctx_t ctx, CSTElement node);
  wcl::doc walk_tuple(ctx_t ctx, CSTElement node);
  wcl::doc walk_tuple_elt(ctx_t ctx, CSTElement node);
  wcl::doc walk_unary(ctx_t ctx, CSTElement node);
  wcl::doc walk_error(ctx_t ctx, CSTElement node);

  wcl::doc walk_placeholder(ctx_t ctx, CSTElement node);

  // Detemines if a given doc fits in the current doc
  bool fits(const wcl::doc_builder& bdr, ctx_t ctx, wcl::doc new_doc);

  // Determines if a given node must be started on a newline
  bool requires_nl(ctx_t ctx, CSTElement node);

  // Emits a newline and any indentation needed
  wcl::doc newline(ctx_t ctx);

  // Emits a space character `count` times
  wcl::doc space(uint8_t count = 1);

  const uint8_t space_per_indent = 4;
  const uint8_t max_column_width = 100;
};
