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

#include <wcl/optional.h>
#include <wcl/rope.h>

#include <cassert>
#include <iostream>
#include <vector>

#include "parser/cst.h"

class Emitter {
 public:
  struct ctx_t {
    int nest_level = 0;
    bool is_flat = false;

    ctx_t flat() {
      ctx_t copy = *this;
      copy.is_flat = true;
      return copy;
    }

    ctx_t nest() {
      ctx_t copy = *this;
      copy.nest_level++;
      return copy;
    }
  };

  // Walks the CST, formats it, and returns the representative rope
  wcl::rope layout(CST cst);

 private:
  // Top level tree walk. Dispatches out the calls for various nodes
  wcl::rope walk(ctx_t ctx, CSTElement node);

  wcl::optional<wcl::rope> walk_node(ctx_t ctx, CSTElement node);
  wcl::rope walk_token(ctx_t ctx, CSTElement node);
  wcl::optional<wcl::rope> walk_rhs(ctx_t ctx, CSTElement node);

  wcl::optional<wcl::rope> walk_apply(ctx_t ctx, CSTElement node);
  wcl::optional<wcl::rope> walk_arity(ctx_t ctx, CSTElement node);
  wcl::optional<wcl::rope> walk_ascribe(ctx_t ctx, CSTElement node);
  wcl::optional<wcl::rope> walk_binary(ctx_t ctx, CSTElement node);
  wcl::optional<wcl::rope> walk_block(ctx_t ctx, CSTElement node);
  wcl::optional<wcl::rope> walk_case(ctx_t ctx, CSTElement node);
  wcl::optional<wcl::rope> walk_data(ctx_t ctx, CSTElement node);
  wcl::optional<wcl::rope> walk_def(ctx_t ctx, CSTElement node);
  wcl::optional<wcl::rope> walk_export(ctx_t ctx, CSTElement node);
  wcl::optional<wcl::rope> walk_flag_export(ctx_t ctx, CSTElement node);
  wcl::optional<wcl::rope> walk_flag_global(ctx_t ctx, CSTElement node);
  wcl::optional<wcl::rope> walk_guard(ctx_t ctx, CSTElement node);
  wcl::optional<wcl::rope> walk_hole(ctx_t ctx, CSTElement node);
  wcl::optional<wcl::rope> walk_identifier(ctx_t ctx, CSTElement node);
  wcl::optional<wcl::rope> walk_ideq(ctx_t ctx, CSTElement node);
  wcl::optional<wcl::rope> walk_if(ctx_t ctx, CSTElement node);
  wcl::optional<wcl::rope> walk_import(ctx_t ctx, CSTElement node);
  wcl::optional<wcl::rope> walk_interpolate(ctx_t ctx, CSTElement node);
  wcl::optional<wcl::rope> walk_kind(ctx_t ctx, CSTElement node);
  wcl::optional<wcl::rope> walk_lambda(ctx_t ctx, CSTElement node);
  wcl::optional<wcl::rope> walk_literal(ctx_t ctx, CSTElement node);
  wcl::optional<wcl::rope> walk_match(ctx_t ctx, CSTElement node);
  wcl::optional<wcl::rope> walk_op(ctx_t ctx, CSTElement node);
  wcl::optional<wcl::rope> walk_package(ctx_t ctx, CSTElement node);
  wcl::optional<wcl::rope> walk_paren(ctx_t ctx, CSTElement node);
  wcl::optional<wcl::rope> walk_prim(ctx_t ctx, CSTElement node);
  wcl::optional<wcl::rope> walk_publish(ctx_t ctx, CSTElement node);
  wcl::optional<wcl::rope> walk_require(ctx_t ctx, CSTElement node);
  wcl::optional<wcl::rope> walk_req_else(ctx_t ctx, CSTElement node);
  wcl::optional<wcl::rope> walk_subscribe(ctx_t ctx, CSTElement node);
  wcl::optional<wcl::rope> walk_target(ctx_t ctx, CSTElement node);
  wcl::optional<wcl::rope> walk_target_args(ctx_t ctx, CSTElement node);
  wcl::optional<wcl::rope> walk_top(ctx_t ctx, CSTElement node);
  wcl::optional<wcl::rope> walk_topic(ctx_t ctx, CSTElement node);
  wcl::optional<wcl::rope> walk_tuple(ctx_t ctx, CSTElement node);
  wcl::optional<wcl::rope> walk_tuple_elt(ctx_t ctx, CSTElement node);
  wcl::optional<wcl::rope> walk_unary(ctx_t ctx, CSTElement node);
  wcl::optional<wcl::rope> walk_error(ctx_t ctx, CSTElement node);

  wcl::optional<wcl::rope> walk_placeholder(ctx_t ctx, CSTElement node);

  // Emits a newline and any indentation needed
  wcl::rope newline(ctx_t ctx);

  // Emits a space character `count` times
  wcl::rope space(uint8_t count = 1);

  // tries to format the subree in a flat context,
  // returns None if it fails
  wcl::optional<wcl::rope> flat(ctx_t ctx, CSTElement node);

  // TODO: memoize this function
  // tries to format the subtree in a flat context, if it fails
  // then None is returned when calling from a flat context and
  // full is returned otherwise.
  wcl::optional<wcl::rope> flat_or(ctx_t ctx, CSTElement node);

  const uint8_t space_per_indent = 4;
  const uint8_t max_column_width = 100;
};
