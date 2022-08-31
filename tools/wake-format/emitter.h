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
#include <wcl/optional.h>

#include <cassert>
#include <iostream>
#include <unordered_map>
#include <vector>

#include "formatter.h"
#include "parser/cst.h"

template <>
struct std::hash<std::pair<CSTElement, ctx_t>> {
  size_t operator()(std::pair<CSTElement, ctx_t> const& pair) const noexcept {
    return wcl::hash_combine(std::hash<CSTElement>{}(pair.first), std::hash<ctx_t>{}(pair.second));
  }
};

struct node_traits_t {
  bool format_off = false;

  void turn_format_off() { format_off = true; }
};

class Emitter {
 public:
  // Walks the CST, formats it, and returns the representative doc
  wcl::doc layout(CST cst);

 private:
  std::unordered_map<CSTElement, node_traits_t> node_traits = {};
  token_traits_map_t token_traits = {};

  // Top level tree walk. Dispatches out the calls for various nodes
  wcl::doc walk(ctx_t ctx, CSTElement node);

  wcl::doc walk_node(ctx_t ctx, CSTElement node);
  wcl::doc walk_token(ctx_t ctx, CSTElement node);

  // Walks a node and emits it without any formatting.
  wcl::doc walk_no_edit(ctx_t ctx, CSTElement node);
  wcl::doc walk_no_edit_acc(ctx_t ctx, CSTElement node);

  // Marks all node elements that have had their formatting disabled
  void mark_no_format_nodes(CSTElement node);

  // Binds all comments in the tree to their associated token as a human would consider it.
  //
  // A comment following a token without a newline is bound to that token
  // Ex:
  //   'def x = 5 # a number'
  //   will bind '# a number' to '5'
  //
  // A comment with a newline between it and the previous token gets bound to the first
  // non-whitespace, non-newline, non-comment following the it.
  // Ex:
  //   '''
  //   def x = 5
  //   # comment1
  //   # comment2
  //   def y = 4
  //   '''
  //   will bind '# comment1' and '# comment2' to the second 'def'
  void bind_comments(CSTElement node);

  // Returns a formatter that inserts the next node
  // on the current line if it fits, or on a new nested line
  auto rhs_fmt();

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
};
