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

#include <algorithm>
#include <cassert>

#include "parser/parser.h"

#define FORMAT_OFF_COMMENT "# wake-format off"

#define DISPATCH(func)                                                                \
  [this](ctx_t ctx, CSTElement node) {                                                \
    return dispatch(ctx, node, [this](ctx_t c, CSTElement n) { return func(c, n); }); \
  }
#define WALK_NODE DISPATCH(walk_node)
#define WALK_TOKEN [this](ctx_t ctx, CSTElement node) { return walk_token(ctx, node); }

using memo_map_t = std::unordered_map<std::pair<CSTElement, ctx_t>, wcl::doc>;
static std::set<memo_map_t*> __memo_maps__ = {};

#define MEMO(ctx, node)                                                                       \
  static memo_map_t __memo_map__ = {};                                                        \
  __memo_maps__.insert(&__memo_map__);                                                        \
  auto __memoize_input__ = [node, ctx]() { return std::pair<CSTElement, ctx_t>(node, ctx); }; \
  {                                                                                           \
    auto value = __memo_map__.find(__memoize_input__());                                      \
    if (value != __memo_map__.end()) {                                                        \
      return wcl::doc(value->second);                                                         \
    }                                                                                         \
  }

#define MEMO_RET(value)                            \
  {                                                \
    wcl::doc v = (value);                          \
    __memo_map__.insert({__memoize_input__(), v}); \
    return v;                                      \
  }

#define MEMO_RESET()                         \
  {                                          \
    for (memo_map_t * map : __memo_maps__) { \
      map->clear();                          \
    }                                        \
    __memo_maps__.clear();                   \
  }

static inline bool requires_nl(cst_id_t type) { return type == CST_BLOCK || type == CST_REQUIRE; }
static inline bool requires_fits_all(cst_id_t type) {
  return type == CST_APP || type == CST_BINARY || type == CST_LITERAL || type == CST_INTERPOLATE ||
         type == CST_IF;
}

static inline bool is_expression(cst_id_t type) {
  return type == CST_ID || type == CST_APP || type == CST_LITERAL || type == CST_HOLE ||
         type == CST_BINARY || type == CST_PAREN || type == CST_ASCRIBE || type == CST_SUBSCRIBE ||
         type == CST_LAMBDA || type == CST_UNARY || type == CST_BLOCK || type == CST_IF ||
         type == CST_INTERPOLATE || type == CST_MATCH || type == CST_REQUIRE || type == CST_PRIM;
}

// a floating comment is a comment bound to another comment
static inline bool is_floating_comment(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node,
                                       const token_traits_map_t& traits) {
  if (node.id() != TOKEN_COMMENT) {
    return false;
  }

  auto it = traits.find(node);
  if (it == traits.end()) {
    return false;
  }

  return it->second.bound_to.id() == TOKEN_COMMENT;
}

// determines if the pointed to node is simple enough to be flattened
static inline bool is_simple_literal(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node,
                                     const token_traits_map_t& traits) {
  if (node.id() != CST_UNARY) {
    return node.id() == CST_LITERAL || node.id() == CST_ID || node.id() == CST_OP;
  }

  CSTElement part = node.firstChildNode();
  if (!is_simple_literal(builder, ctx, part, traits)) {
    return false;
  }

  part.nextSiblingNode();
  if (!is_simple_literal(builder, ctx, part, traits)) {
    return false;
  }

  part.nextSiblingNode();
  return part.empty();
}

static inline bool contains_req_else(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node,
                                     const token_traits_map_t& traits) {
  CSTElement copy = node;
  while (!copy.empty()) {
    if (copy.id() == CST_REQ_ELSE) {
      return true;
    }
    copy.nextSiblingElement();
  }
  return false;
}

static bool compare_doc_height(const wcl::doc& lhs, const wcl::doc& rhs) {
  return lhs->height() < rhs->height();
}

static bool compare_doc_width(const wcl::doc& lhs, const wcl::doc& rhs) {
  return lhs->max_width() < rhs->max_width();
}

static bool is_op_left_assoc(const CSTElement& op) {
  switch (op.id()) {
    case TOKEN_OP_DOT:
    case TOKEN_OP_QUANT:
    case TOKEN_OP_MULDIV:
    case TOKEN_OP_ADDSUB:
    case TOKEN_OP_COMPARE:
    case TOKEN_OP_AND:
    case TOKEN_OP_OR:
      return true;

    default:
      return false;
  }
}

static bool is_op_suffix(const CSTElement& op) {
  switch (op.id()) {
    case TOKEN_OP_COMMA:
      return true;

    default:
      return false;
  }
}

// determines if a given binop matches a given type and string literal
static inline bool is_binop_matching_str(const CSTElement& op, cst_id_t type, std::string lit) {
  return op.id() == type && op.fragment().segment().str() == lit;
}

static size_t count_leading_newlines(const token_traits_map_t& traits, const CSTElement& node) {
  CSTElement token = node;
  while (token.isNode()) {
    token = token.firstChildElement();
  }

  auto it = traits.find(token);
  if (it == traits.end()) {
    return 0;
  }
  return it->second.before_bound.size();
}

static size_t count_trailing_newlines(const token_traits_map_t& traits, const CSTElement& node) {
  CSTElement token;
  {
    IsWSNLCPredicate is_wsnlc;
    CSTElement curr_rhs = node.firstChildElement();
    CSTElement next_rhs = curr_rhs;
    next_rhs.nextSiblingElement();

    while (!next_rhs.empty()) {
      while (!next_rhs.empty() && is_wsnlc(next_rhs)) {
        next_rhs.nextSiblingElement();
      }
      if (next_rhs.empty()) {
        token = curr_rhs;
      } else {
        curr_rhs = next_rhs;
        next_rhs.nextSiblingElement();
      }
    }
    token = curr_rhs;
  }

  auto it = traits.find(token);
  if (it == traits.end()) {
    return 0;
  }

  return it->second.after_bound.size();
}

static size_t count_allowed_newlines(const token_traits_map_t& traits, const CSTElement& node) {
  FMT_ASSERT(node.isNode(), node,
             "Expected node, Saw <" + std::string(symbolName(node.id())) + ">");
  return count_leading_newlines(traits, node) + count_trailing_newlines(traits, node);
}

static size_t count_allowed_newlines(const token_traits_map_t& traits,
                                     const std::vector<CSTElement>& parts) {
  assert(parts.size() >= 2);
  return count_leading_newlines(traits, parts[0]) + count_trailing_newlines(traits, parts.back());
}

// Assumes that at least one of the choices is viable. Will assert otherwise
static wcl::doc select_best_choice(std::vector<wcl::optional<wcl::doc>> choices) {
  std::vector<wcl::doc> lte_fmt = {};
  std::vector<wcl::doc> gt_fmt = {};
  int i = 0;

  for (auto choice_opt : choices) {
    i++;
    if (!choice_opt) {
      continue;
    }
    auto choice = *choice_opt;

    if (choice->max_width() > MAX_COLUMN_WIDTH) {
      gt_fmt.push_back(std::move(choice));
    } else {
      lte_fmt.push_back(std::move(choice));
    }
  }

  if (lte_fmt.size() > 0) {
    auto min_height = std::min_element(lte_fmt.begin(), lte_fmt.end(), compare_doc_height);
    return *min_height;
  }

  // If lte_fmt doesn't have any viable then gt_fmt must have at least one
  assert(gt_fmt.size() > 0);

  auto min_width = std::min_element(gt_fmt.begin(), gt_fmt.end(), compare_doc_width);
  return *min_width;
}

static wcl::doc binop_lhs_separator(const CSTElement& op) {
  switch (op.id()) {
    case TOKEN_OP_DOT:
    case TOKEN_OP_COMMA:
      return wcl::doc::lit("");
    default:
      return wcl::doc::lit(" ");
  }
}

static wcl::doc binop_rhs_separator(const CSTElement& op) {
  switch (op.id()) {
    case TOKEN_OP_DOT:
      return wcl::doc::lit("");
    default:
      return wcl::doc::lit(" ");
  }
}

static std::vector<CSTElement> collect_left_binary(CSTElement collect_over, CSTElement node) {
  if (node.id() != CST_BINARY) {
    return {node};
  }

  // NOTE: The 'node' variant functions are being used here which is differnt than everywhere else
  // This is fine since COMMENTS are bound to the nodes and this func only needs to process nodes
  CSTElement left = node.firstChildNode();
  CSTElement op = left;
  op.nextSiblingNode();
  CSTElement right = op;
  right.nextSiblingNode();

  if (!(op.id() == CST_OP && op.firstChildElement().id() == collect_over.id() &&
        op.firstChildElement().fragment().segment().str() ==
            collect_over.fragment().segment().str())) {
    return {node};
  }

  auto collect = collect_left_binary(collect_over, left);
  collect.push_back(right);

  return collect;
}

static std::vector<CSTElement> collect_right_binary(CSTElement collect_over, CSTElement node) {
  if (node.id() != CST_BINARY) {
    return {node};
  }

  // NOTE: The 'node' variant functions are being used here which is differnt than everywhere else
  // This is fine since COMMENTS are bound to the nodes and this func only needs to process nodes
  CSTElement left = node.firstChildNode();
  CSTElement op = left;
  op.nextSiblingNode();
  CSTElement right = op;
  right.nextSiblingNode();

  if (!(op.id() == CST_OP && op.firstChildElement().id() == collect_over.id() &&
        op.firstChildElement().fragment().segment().str() ==
            collect_over.fragment().segment().str())) {
    return {node};
  }

  std::vector<CSTElement> collect = {left};
  auto right_collect = collect_right_binary(collect_over, right);

  collect.insert(collect.end(), right_collect.begin(), right_collect.end());

  return collect;
}

static std::vector<CSTElement> collect_apply_parts(CSTElement node) {
  if (node.id() != CST_APP) {
    return {node};
  }

  // NOTE: The 'node' variant functions are being used here which is differnt than everywhere else
  // This is fine since COMMENTS are bound to the nodes and this func only needs to process nodes
  CSTElement lhs = node.firstChildNode();
  CSTElement rhs = lhs;
  rhs.nextSiblingNode();

  auto collect = collect_apply_parts(lhs);
  collect.push_back(rhs);

  return collect;
}

static inline bool is_simple_binop(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node,
                                   const token_traits_map_t& traits) {
  if (node.id() != CST_BINARY) {
    return is_simple_literal(builder, ctx, node, traits);
  }

  // NOTE: The 'node' variant functions are being used here which is differnt than everywhere else
  // This is fine since COMMENTS are bound to the nodes and this func only needs to process nodes
  CSTElement lhs = node.firstChildNode();
  CSTElement op = lhs;
  op.nextSiblingNode();
  CSTElement rhs = op;
  rhs.nextSiblingNode();

  FMT_ASSERT(op.id() == CST_OP, op, "Expected CST_OP for operator");
  CSTElement op_token = op.firstChildElement();

  std::vector<CSTElement> parts = {};
  if (is_op_left_assoc(op_token)) {
    parts = collect_left_binary(op_token, node);
  } else {
    parts = collect_right_binary(op_token, node);
  }

  if (parts.size() != 2) {
    return false;
  }

  return is_simple_literal(builder, ctx, parts[0], traits) &&
         is_simple_literal(builder, ctx, parts[1], traits);
}

static inline bool is_simple_apply(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node,
                                   const token_traits_map_t& traits) {
  if (node.id() != CST_APP) {
    return is_simple_literal(builder, ctx, node, traits);
  }

  auto parts = collect_apply_parts(node);

  if (parts.size() != 2) {
    return false;
  }

  return is_simple_literal(builder, ctx, parts[0], traits) &&
         is_simple_literal(builder, ctx, parts[1], traits);
}

// determines if the rest of the node
// - only has one sibling node
// - and that node is a "simple" thing as defined by is_simple_apply
static inline bool is_single_apply(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node,
                                   const token_traits_map_t& traits) {
  CSTElement copy = node;
  if (!copy.isNode()) {
    copy.nextSiblingNode();
  }

  size_t node_count = 0;
  while (!copy.empty()) {
    node_count++;

    if (!is_simple_apply(builder, ctx, node, traits)) {
      return false;
    }

    copy.nextSiblingNode();
  }

  return node_count == 1;
}

Emitter::~Emitter() { MEMO_RESET(); }

auto Emitter::rhs_fmt(bool always_newline) {
  auto rhs_fmt = fmt().walk(WALK_NODE);

  auto flat_fmt = fmt().space().join(rhs_fmt);
  auto full_fmt = fmt().nest(fmt().freshline().join(rhs_fmt));

  // clang-format off
  return fmt().match(
    // if the subtree requires a newline then our hand is forced
    pred(requires_nl, full_fmt)
    // If for some reason (probably a comment) there is a newline after the '=' then we have to
    // use the full_fmt
   .pred([](const wcl::doc_builder& builder, ctx_t ctx, const CSTElement& node,
            const token_traits_map_t& traits) {
      ctx_t c = ctx.sub(builder);
      return c->has_newline() && c->last_width() == 0;
   }, full_fmt)
    // If the RHS has a leading comment then we must use the full_fmt
   .pred([this](const wcl::doc_builder& builder, ctx_t ctx, const CSTElement& node,
            const token_traits_map_t& traits) {
      return count_leading_newlines(token_traits, node) > 0;
   }, full_fmt)
    // Always newline when requested unless the thing to be formatted is a "single literal".
    // Used for top-level defs and top-level "constant" defs
   .pred(ConstPredicate(always_newline), fmt().fmt_if_else(is_single_apply, flat_fmt, full_fmt))

    // if our hand hand hasn't yet been forced then decide based on how well RHS fits
   .pred(requires_fits_all, fmt().fmt_if_fits_all(flat_fmt, full_fmt))
   .pred_fits_first(flat_fmt)
   .otherwise(full_fmt));
  // clang-format on
}

auto Emitter::pattern_fmt(cst_id_t stop_at) {
  auto part_fmt = fmt().walk(is_expression, WALK_NODE).consume_wsnlc();
  auto all_flat = fmt().join(part_fmt).fmt_while([stop_at](cst_id_t id) { return id != stop_at; },
                                                 fmt().space().join(part_fmt));
  auto all_explode =
      fmt()
          .lit(wcl::doc::lit("("))
          .nest(fmt().freshline().join(part_fmt).fmt_while(
              [stop_at](cst_id_t id) { return id != stop_at; }, fmt().freshline().join(part_fmt)))
          .freshline()
          .lit(wcl::doc::lit(")"));
  // 4 Cases
  // 1) The "flat format" doesn't have a newline and fits()
  // 2) The "flat format" doesn't have a newline and doesn't fits()
  // 3) The "flat format" has a NL and fits()
  // 4) The "flat format" has a NL and doesn't fits()
  //
  // #1 is the ideal and most common use, so flat is returned
  // #2 is a somewhat rare case where the pattern is very wide without the influence of comments.
  //    return the explode representation
  // #3/#4 when flat has a newline, it must have been grown from a comment. At that point there
  //    isn't a good way to determine if it fits since the NL can be in many places. Instead
  //    just return flat, accepting in some very rare cases the output will exceed the max width

  return fmt().fmt_try_else(
      [](const wcl::doc_builder& builder, ctx_t ctx, wcl::doc doc) { return doc->has_newline(); },
      all_flat, fmt().fmt_if_fits_all(all_flat, all_explode));
}

wcl::doc Emitter::layout(CST cst) {
  ctx_t ctx;
  bind_comments(cst.root());
  mark_no_format_nodes(cst.root());
  mark_top_level_nodes(cst.root());
  return walk(ctx, cst.root());
}

template <class Func>
wcl::doc Emitter::dispatch(ctx_t ctx, CSTElement node, Func func) {
  MEMO(ctx, node);
  FMT_ASSERT(node.isNode(), node,
             "Expected node, Saw <" + std::string(symbolName(node.id())) + ">");

  if (node_traits[node].format_off) {
    MEMO_RET(walk_no_edit(ctx, node));
  }

  MEMO_RET(func(ctx, node));
}

wcl::doc Emitter::walk(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);

  auto node_fmt = fmt().walk(WALK_NODE).freshline();

  auto consume_wsnl = fmt().fmt_while({TOKEN_WS, TOKEN_NL}, fmt().next());
  auto floating_comment_fmt = fmt().fmt_if_else(
      is_floating_comment,
      fmt()
          .fmt_while(TOKEN_COMMENT,
                     fmt().token(TOKEN_COMMENT).freshline().fmt_if(TOKEN_NL, fmt().next()))
          .freshline()
          .newline()
          .join(consume_wsnl),
      fmt().consume_wsnlc());

  // clang-format off
  auto body_fmt = fmt()
    .fmt_while(
      {TOKEN_WS, TOKEN_NL, TOKEN_COMMENT},
      fmt().match(
        pred(TOKEN_COMMENT, floating_comment_fmt)
        .pred({TOKEN_WS, TOKEN_NL}, fmt().next())
      )
    ).match(
       pred(IsNodeEmptyPredicate(), fmt())
      // Nodes that should group together instead of being newlined
      .pred(CST_IMPORT, fmt().fmt_while(CST_IMPORT, node_fmt))
      .pred(CST_EXPORT, fmt().fmt_while(CST_EXPORT, node_fmt))
      .otherwise(node_fmt)
    );
  // clang-format on

  MEMO_RET(fmt()
               .join(body_fmt)
               .walk_all(fmt().newline().join(body_fmt))
               .format(ctx, node.firstChildElement(), token_traits));
}

wcl::doc Emitter::walk_node(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  FMT_ASSERT(node.isNode(), node, "Expected node");

  switch (node.id()) {
    case CST_ARITY:
      MEMO_RET(walk_arity(ctx, node));
    case CST_APP:
      MEMO_RET(walk_apply(ctx, node));
    case CST_ASCRIBE:
      MEMO_RET(walk_ascribe(ctx, node));
    case CST_BINARY:
      MEMO_RET(walk_binary(ctx, node));
    case CST_BLOCK:
      MEMO_RET(walk_block(ctx, node));
    case CST_CASE:
      MEMO_RET(walk_case(ctx, node));
    case CST_DATA:
      MEMO_RET(walk_data(ctx, node));
    case CST_DEF:
      MEMO_RET(walk_def(ctx, node));
    case CST_EXPORT:
      MEMO_RET(walk_export(ctx, node));
    case CST_FLAG_EXPORT:
      MEMO_RET(walk_flag_export(ctx, node));
    case CST_FLAG_GLOBAL:
      MEMO_RET(walk_flag_global(ctx, node));
    case CST_GUARD:
      MEMO_RET(walk_guard(ctx, node));
    case CST_HOLE:
      MEMO_RET(walk_hole(ctx, node));
    case CST_ID:
      MEMO_RET(walk_identifier(ctx, node));
    case CST_IDEQ:
      MEMO_RET(walk_ideq(ctx, node));
    case CST_IF:
      MEMO_RET(walk_if(ctx, node));
    case CST_IMPORT:
      MEMO_RET(walk_import(ctx, node));
    case CST_INTERPOLATE:
      MEMO_RET(walk_interpolate(ctx, node));
    case CST_KIND:
      MEMO_RET(walk_kind(ctx, node));
    case CST_LAMBDA:
      MEMO_RET(walk_lambda(ctx, node));
    case CST_LITERAL:
      MEMO_RET(walk_literal(ctx, node));
    case CST_MATCH:
      MEMO_RET(walk_match(ctx, node));
    case CST_OP:
      MEMO_RET(walk_op(ctx, node));
    case CST_PACKAGE:
      MEMO_RET(walk_package(ctx, node));
    case CST_PAREN:
      MEMO_RET(walk_paren(ctx, node));
    case CST_PRIM:
      MEMO_RET(walk_prim(ctx, node));
    case CST_PUBLISH:
      MEMO_RET(walk_publish(ctx, node));
    case CST_REQUIRE:
      MEMO_RET(walk_require(ctx, node));
    case CST_REQ_ELSE:
      MEMO_RET(walk_req_else(ctx, node));
    case CST_SUBSCRIBE:
      MEMO_RET(walk_subscribe(ctx, node));
    case CST_TARGET:
      MEMO_RET(walk_target(ctx, node));
    case CST_TARGET_ARGS:
      MEMO_RET(walk_target_args(ctx, node));
    case CST_TOP:
      MEMO_RET(walk_top(ctx, node));
    case CST_TOPIC:
      MEMO_RET(walk_topic(ctx, node));
    case CST_TUPLE:
      MEMO_RET(walk_tuple(ctx, node));
    case CST_TUPLE_ELT:
      MEMO_RET(walk_tuple_elt(ctx, node));
    case CST_UNARY:
      MEMO_RET(walk_unary(ctx, node));
    case CST_ERROR:
      MEMO_RET(walk_error(ctx, node));
    default:
      assert(false);
  }
}

wcl::doc Emitter::walk_placeholder(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  FMT_ASSERT(node.isNode(), node, "Expected node");

  wcl::doc_builder bdr;

  for (CSTElement child = node.firstChildElement(); !child.empty(); child.nextSiblingElement()) {
    if (child.isNode()) {
      bdr.append(walk_node(ctx, child));
    } else {
      bdr.append(walk_token(ctx, child));
    }
  }

  MEMO_RET(std::move(bdr).build());
}

wcl::doc Emitter::walk_no_edit(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);

  // The very first token emitted needs to be checked for 'before bound' comments
  // These comments are outside of the no_edit walk and need to be emitted.
  // All other comments are captured by the recursive walk.

  CSTElement first = node;
  while (first.isNode()) {
    first = first.firstChildElement();
  }

  wcl::doc_builder bdr;
  for (auto node : token_traits[first].before_bound) {
    bdr.append(fmt().walk(WALK_TOKEN).freshline().compose(ctx, node, token_traits));
  }

  bdr.append(walk_no_edit_acc(ctx.sub(bdr), node));
  MEMO_RET(std::move(bdr).build());
}

wcl::doc Emitter::walk_no_edit_acc(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);

  if (!node.isNode()) {
    MEMO_RET(wcl::doc::lit(node.fragment().segment().str()));
  }

  wcl::doc_builder bdr;
  for (CSTElement child = node.firstChildElement(); !child.empty(); child.nextSiblingElement()) {
    // The last nl of a *tagged* "no format" CST_DEF node shouldn't be emitted.
    // The nominal formtting for the larger program structure will ensure the correct NLs are
    // emitted.
    if (node.id() == CST_DEF && child.id() == TOKEN_NL && node_traits[node].format_off) {
      CSTElement next = child;
      next.nextSiblingElement();
      if (next.empty()) {
        continue;
      }
    }

    bdr.append(walk_no_edit_acc(ctx, child));
  }

  MEMO_RET(std::move(bdr).build());
}

void inorder_collect_tokens(CSTElement node, std::vector<CSTElement>& items) {
  for (CSTElement child = node.firstChildElement(); !child.empty(); child.nextSiblingElement()) {
    if (child.isNode()) {
      inorder_collect_tokens(child, items);
      continue;
    }

    items.push_back(child);
  }
}

// This function is responsible for exploring *only* the top level of the source file
// to identify and tag comments which are "floating block comments". To be a floating block comment
// the following must hold
//   - The comment is top level
//   - The comment has two newlines between it and the next element
//   - Multiple comments in a row are considered to be in the same block
//
// Ex:
//
// # floating 1a
//
// # floating 2a
// # floating 2b
// # floating 2c
//
// # not-floating
// def x = 5
// # floating 3a
// # floating 3b
//
// A good rule of thumb is that a floating comment is one that a human wouldn't consider as "bound"
// to some other token.
//
// Input: The top level CST node
// Output: token_traits[t].bound_to == t' forall t where t is a floating block comment and t' is the
// *first* comment token in that floating block.
//
// Ex:
//  token_traits[1a].bount_to == 1a
//  token_traits[2a].bount_to == 2a
//  token_traits[2b].bount_to == 2a
//  token_traits[2c].bount_to == 2a
void Emitter::bind_top_level_comments(CSTElement node) {
  // Stack Invariants
  //   Comments & the associated newline are stored/built up on the stack
  //   Two newlines in a row (one current, one on the stack) signals a floating comment block
  //   Whitespace is ignored
  //   Anything else signals that the stack isn't a floating comment block and should be cleared
  std::vector<CSTElement> stack = {};

  for (CSTElement child = node.firstChildElement(); !child.empty(); child.nextSiblingElement()) {
    if (child.id() == TOKEN_COMMENT) {
      stack.push_back(child);
      continue;
    }

    if (stack.empty()) {
      continue;
    }

    if (child.id() == TOKEN_NL) {
      CSTElement top = stack.back();

      if (top.id() == TOKEN_NL) {
        CSTElement first = stack.front();
        for (CSTElement s : stack) {
          if (s.id() != TOKEN_COMMENT) {
            continue;
          }
          token_traits[s].set_bound_to(first);
        }

        stack = {};
        continue;
      }

      if (top.id() == TOKEN_COMMENT) {
        stack.push_back(child);
        continue;
      }

      FMT_ASSERT(false, top, "Expected comment or newline on stack");
    }

    if (child.id() == TOKEN_WS) {
      continue;
    }

    stack = {};
  }

  if (!stack.empty()) {
    CSTElement first = stack.front();
    for (CSTElement s : stack) {
      if (s.id() != TOKEN_COMMENT) {
        continue;
      }
      token_traits[s].set_bound_to(first);
    }
  }
}

void Emitter::bind_nested_comments(CSTElement node) {
  std::vector<CSTElement> items;
  inorder_collect_tokens(node, items);

  IsWSNLCPredicate is_wsnlc;

  for (size_t i = 0; i < items.size(); i++) {
    CSTElement item = items[i];
    if (is_wsnlc(item)) {
      continue;
    }

    // bind after
    for (size_t j = i + 1; j < items.size(); j++) {
      CSTElement target = items[j];
      if (target.id() != TOKEN_WS && target.id() != TOKEN_COMMENT) {
        break;
      }
      // Stop binding if we find a target already bound
      if (!token_traits[target].bound_to.empty()) {
        break;
      }
      token_traits[item].bind_after(target);
      token_traits[target].set_bound_to(item);
    }

    // bind before
    for (size_t j = i; j > 0; j--) {
      CSTElement target = items[j - 1];
      if (!is_wsnlc(target)) {
        break;
      }
      // Stop binding if we find a target already bound
      if (!token_traits[target].bound_to.empty()) {
        break;
      }
      token_traits[item].bind_before(target);
      token_traits[target].set_bound_to(item);
    }
  }

  // At this point, all comments should be bound to something.
  // Assert that is actually the case or alert the user otherwise
  for (size_t i = 0; i < items.size(); i++) {
    CSTElement item = items[i];
    if (item.id() != TOKEN_COMMENT) {
      continue;
    }

    FMT_ASSERT(!token_traits[item].bound_to.empty(), item,
               "There is a unbound comment, which is an unexpected error case. Please report this "
               "to the wake-format team.");
  }
}

void Emitter::bind_comments(CSTElement node) {
  bind_top_level_comments(node);
  bind_nested_comments(node);
}

void Emitter::mark_top_level_nodes(CSTElement node) {
  FMT_ASSERT(node.isNode(), node, "Expected node");

  // Note: we are iterating over nodes here rather than the more common element
  for (CSTElement child = node.firstChildNode(); !child.empty(); child.nextSiblingNode()) {
    node_traits[child].set_top_level();
  }
}

void Emitter::mark_no_format_nodes(CSTElement node) {
  FMT_ASSERT(node.isNode(), node, "Expected node");

  for (CSTElement child = node.firstChildElement(); !child.empty(); child.nextSiblingElement()) {
    if (child.isNode()) {
      mark_no_format_nodes(child);
      continue;
    }

    if (child.id() == TOKEN_COMMENT && child.fragment().segment().str() == FORMAT_OFF_COMMENT) {
      while (!child.empty() && !child.isNode()) child.nextSiblingElement();
      if (child.empty()) {
        continue;
      }

      // Instead of marking the entire block as format off
      // only the first non-token child should be marked as format off
      // this allows turing off formatting for the first block item, otherwise
      // you can never format *just* the first block item
      if (child.id() == CST_BLOCK) {
        CSTElement block_item = child.firstChildElement();
        while (!(child.empty() || child.isNode())) {
          child.nextSiblingElement();
        }

        // This shouldn't be possible, but assert anyways just in case
        FMT_ASSERT(!child.empty(), child, "Expected non-empty child");

        node_traits[block_item].turn_format_off();
        continue;
      }

      node_traits[child].turn_format_off();
    }
  }
}

wcl::doc Emitter::walk_token(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  FMT_ASSERT(!node.isNode(), node, "Expected node");

  wcl::doc_builder builder;

  for (auto node : token_traits[node].before_bound) {
    builder.append(fmt().walk(WALK_TOKEN).freshline().compose(ctx, node, token_traits));
  }

  switch (node.id()) {
    case TOKEN_KW_MACRO_HERE:
      builder.append(wcl::doc::lit("@here"));
      break;
    case TOKEN_NL:
      builder.append(wcl::doc::lit("\n"));
      break;
    case TOKEN_WS:
      builder.append(wcl::doc::lit(" "));
      break;
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
      builder.append(wcl::doc::lit(node.fragment().segment().str()));
      break;
    default:
      assert(false);
  }

  for (auto node : token_traits[node].after_bound) {
    builder.append(fmt().space().walk(WALK_TOKEN).newline().compose(ctx, node, token_traits));
  }

  MEMO_RET(std::move(builder).build());
}

wcl::optional<wcl::doc> Emitter::combine_apply_flat(ctx_t ctx,
                                                    const std::vector<CSTElement>& parts) {
  wcl::doc_builder builder;
  for (size_t i = 0; i < parts.size() - 1; i++) {
    CSTElement part = parts[i];
    builder.append(fmt().walk(WALK_NODE).space().compose(ctx.sub(builder), part, token_traits));
  }

  builder.append(walk_node(ctx.sub(builder), parts.back()));

  wcl::doc doc = std::move(builder).build();
  if (doc->newline_count() != count_allowed_newlines(token_traits, parts)) {
    return {};
  }
  return {wcl::in_place_t{}, std::move(doc)};
}

wcl::optional<wcl::doc> Emitter::combine_apply_constructor(ctx_t ctx,
                                                           const std::vector<CSTElement>& parts) {
  if (parts.size() != 2) {
    return {};
  }

  wcl::doc_builder builder;

  CSTElement lhs = parts[0];
  CSTElement rhs = parts[1];

  if (count_leading_newlines(token_traits, rhs) != 0) {
    return {};
  }

  wcl::doc lhs_fmt = fmt().walk(WALK_NODE).space().compose(ctx.sub(builder), lhs, token_traits);
  if (lhs_fmt->newline_count() != count_leading_newlines(token_traits, lhs)) {
    return {};
  }

  builder.append(lhs_fmt);
  builder.append(walk_node(ctx.sub(builder).explode(), rhs));

  return {wcl::in_place_t{}, std::move(builder).build()};
}

wcl::optional<wcl::doc> Emitter::combine_apply_explode_all(ctx_t ctx,
                                                           const std::vector<CSTElement>& parts) {
  wcl::doc_builder builder;
  for (size_t i = 0; i < parts.size() - 1; i++) {
    CSTElement part = parts[i];
    builder.append(
        fmt().walk(WALK_NODE).freshline().compose(ctx.sub(builder).explode(), part, token_traits));
  }

  builder.append(walk_node(ctx.sub(builder).explode(), parts.back()));

  return {wcl::in_place_t{}, std::move(builder).build()};
}

wcl::doc Emitter::walk_apply(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  FMT_ASSERT(node.id() == CST_APP, node, "Expected CST_APP");

  auto parts = collect_apply_parts(node);

  std::vector<wcl::optional<wcl::doc>> choices = {
      combine_apply_flat(ctx, parts),
      combine_apply_constructor(ctx, parts),
      combine_apply_explode_all(ctx, parts),
  };

  MEMO_RET(select_best_choice(choices));
}

wcl::doc Emitter::walk_arity(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  MEMO_RET(walk_placeholder(ctx, node));
}

wcl::doc Emitter::walk_ascribe(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  MEMO_RET(walk_placeholder(ctx, node));
}

wcl::optional<wcl::doc> Emitter::combine_flat(CSTElement over, ctx_t ctx,
                                              const std::vector<CSTElement>& parts) {
  wcl::doc_builder builder;
  for (size_t i = 0; i < parts.size() - 1; i++) {
    CSTElement part = parts[i];
    builder.append(walk_node(ctx, part));
    builder.append(place_binop(over, true, ctx.sub(builder)));
  }

  builder.append(walk_node(ctx.sub(builder), parts.back()));

  wcl::doc doc = std::move(builder).build();
  if (doc->newline_count() != count_allowed_newlines(token_traits, parts)) {
    return {};
  }
  return {wcl::in_place_t{}, std::move(doc)};
}

wcl::optional<wcl::doc> Emitter::combine_explode_first(CSTElement over, ctx_t ctx,
                                                       const std::vector<CSTElement>& parts) {
  wcl::doc_builder builder;

  CSTElement part = parts[0];
  builder.append(walk_node(ctx.explode(), part));
  builder.append(place_binop(over, false, ctx.sub(builder)));

  for (size_t i = 1; i < parts.size() - 1; i++) {
    part = parts[i];
    builder.append(walk_node(ctx.sub(builder), part));
    builder.append(place_binop(over, false, ctx.sub(builder)));
  }

  builder.append(walk_node(ctx.sub(builder), parts.back()));
  return {wcl::in_place_t{}, std::move(builder).build()};
}

wcl::optional<wcl::doc> Emitter::combine_explode_last(CSTElement over, ctx_t ctx,
                                                      const std::vector<CSTElement>& parts) {
  wcl::doc_builder builder;

  for (size_t i = 0; i < parts.size() - 1; i++) {
    CSTElement part = parts[i];
    builder.append(walk_node(ctx.sub(builder), part));
    builder.append(place_binop(over, false, ctx.sub(builder)));
  }

  builder.append(walk_node(ctx.sub(builder).explode(), parts.back()));
  return {wcl::in_place_t{}, std::move(builder).build()};
}

wcl::optional<wcl::doc> Emitter::combine_explode_all(CSTElement over, ctx_t ctx,
                                                     const std::vector<CSTElement>& parts) {
  wcl::doc_builder builder;

  for (size_t i = 0; i < parts.size() - 1; i++) {
    CSTElement part = parts[i];
    builder.append(walk_node(ctx.sub(builder).explode(), part));
    builder.append(place_binop(over, false, ctx.sub(builder)));
  }

  builder.append(walk_node(ctx.sub(builder).explode(), parts.back()));
  return {wcl::in_place_t{}, std::move(builder).build()};
}

wcl::optional<wcl::doc> Emitter::combine_explode_first_compress(
    CSTElement over, ctx_t ctx, const std::vector<CSTElement>& parts) {
  wcl::doc_builder builder;

  builder.append(walk_node(ctx.sub(builder).explode(), parts[0]));
  builder.append(place_binop(over, true, ctx.sub(builder)));

  for (size_t i = 1; i < parts.size() - 1; i++) {
    CSTElement part = parts[i];
    builder.append(walk_node(ctx.sub(builder), part));
    builder.append(place_binop(over, true, ctx.sub(builder)));
  }

  builder.append(walk_node(ctx.sub(builder), parts.back()));

  wcl::doc doc = std::move(builder).build();
  if (doc->newline_count() != count_allowed_newlines(token_traits, parts)) {
    return {};
  }
  return {wcl::in_place_t{}, std::move(doc)};
}

wcl::optional<wcl::doc> Emitter::combine_explode_last_compress(
    CSTElement over, ctx_t ctx, const std::vector<CSTElement>& parts) {
  wcl::doc_builder builder;

  for (size_t i = 0; i < parts.size() - 1; i++) {
    CSTElement part = parts[i];
    builder.append(walk_node(ctx.sub(builder), part));
    builder.append(place_binop(over, true, ctx.sub(builder)));
  }

  builder.append(walk_node(ctx.sub(builder).explode(), parts.back()));

  wcl::doc doc = std::move(builder).build();
  if (doc->newline_count() != count_allowed_newlines(token_traits, parts)) {
    return {};
  }
  return {wcl::in_place_t{}, std::move(doc)};
}

wcl::doc Emitter::walk_binary(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  FMT_ASSERT(node.id() == CST_BINARY, node, "Expected CST_BINARY");

  // NOTE: The 'node' variant functions are being used here which is differnt than everywhere else
  // This is fine since COMMENTS are bound to the nodes and this func only needs to process nodes
  CSTElement lhs = node.firstChildNode();
  CSTElement op = lhs;
  op.nextSiblingNode();
  CSTElement rhs = op;
  rhs.nextSiblingNode();

  FMT_ASSERT(op.id() == CST_OP, op, "Expected CST_OP for operator");
  CSTElement op_token = op.firstChildElement();

  std::vector<CSTElement> parts = {};
  if (is_op_left_assoc(op_token)) {
    parts = collect_left_binary(op_token, node);
  } else {
    parts = collect_right_binary(op_token, node);
  }

  if (!ctx.nested_binop && (is_binop_matching_str(op_token, TOKEN_OP_DOLLAR, "$") ||
                            is_binop_matching_str(op_token, TOKEN_OP_OR, "|"))) {
    MEMO_RET(select_best_choice({
        combine_explode_first(op_token, ctx.binop(), parts),
        combine_explode_last(op_token, ctx.binop(), parts),
        combine_explode_all(op_token, ctx.binop(), parts),
    }));
  }

  MEMO_RET(select_best_choice({
      // 1
      combine_flat(op_token, ctx.binop(), parts),
      // 2
      combine_explode_first(op_token, ctx.binop(), parts),
      // 3
      combine_explode_last(op_token, ctx.binop(), parts),
      // 4
      combine_explode_all(op_token, ctx.binop(), parts),
      // 5
      combine_explode_first_compress(op_token, ctx.binop(), parts),
      // 6
      combine_explode_last_compress(op_token, ctx.binop(), parts),
  }));
}

wcl::doc Emitter::walk_block(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  FMT_ASSERT(node.id() == CST_BLOCK, node, "Expected CST_BLOCK");

  // clang-format off
  auto body_fmt = fmt().match(
    // Block level newlines should be re-emitted as these are the
    // user creating 'pseudo-blocks'
    pred(TOKEN_NL, fmt().newline().newline().next())
   .pred(TOKEN_WS, fmt().next())
   // Comments are followed by ws/nl/c that have already been tagged and will be
   // handled later. Skip them for now.
   .pred(TOKEN_COMMENT, fmt().consume_wsnlc())
   .otherwise(fmt().freshline().walk(WALK_NODE)));
  // clang-format on

  MEMO_RET(fmt().walk_all(body_fmt).format(ctx, node.firstChildElement(), token_traits));
}

wcl::doc Emitter::walk_case(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  FMT_ASSERT(node.id() == CST_CASE, node, "Expected CST_CASE");

  size_t leading_count = count_leading_newlines(token_traits, node);

  MEMO_RET(fmt()
               .join(pattern_fmt(CST_GUARD))
               .consume_wsnlc()
               // emit a freshline if the previous walk emitted a NL
               .fmt_if_else(
                   [leading_count](wcl::doc_builder& builder, ctx_t ctx, CSTElement& node,
                                   const token_traits_map_t& traits) {
                     return builder->newline_count() > leading_count;
                   },
                   fmt().freshline(), fmt().space())
               .walk(CST_GUARD, WALK_NODE)
               .consume_wsnlc()
               .join(rhs_fmt())
               .format(ctx, node.firstChildElement(), token_traits));
}

wcl::doc Emitter::walk_data(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  FMT_ASSERT(node.id() == CST_DATA, node, "Expected CST_DATA");

  auto fmt_members = fmt().walk(WALK_NODE).consume_wsnlc().walk_all(
      fmt().freshline().walk(WALK_NODE).consume_wsnlc());

  auto no_nl = fmt()
                   .fmt_if(CST_FLAG_GLOBAL, fmt().walk(WALK_NODE).ws())
                   .fmt_if(CST_FLAG_EXPORT, fmt().walk(WALK_NODE).ws())
                   .token(TOKEN_KW_DATA)
                   .ws()
                   .walk(is_expression, WALK_NODE)
                   .ws()
                   .token(TOKEN_P_EQUALS)
                   .consume_wsnlc()
                   .space()
                   .join(fmt_members)
                   .format(ctx, node.firstChildElement(), token_traits);

  if (no_nl->newline_count() == count_leading_newlines(token_traits, node)) {
    MEMO_RET(no_nl);
  }

  MEMO_RET(fmt()
               .fmt_if(CST_FLAG_GLOBAL, fmt().walk(WALK_NODE).ws())
               .fmt_if(CST_FLAG_EXPORT, fmt().walk(WALK_NODE).ws())
               .token(TOKEN_KW_DATA)
               .ws()
               .walk(is_expression, WALK_NODE)
               .ws()
               .token(TOKEN_P_EQUALS)
               .consume_wsnlc()
               .nest(fmt().freshline().join(fmt_members))
               .format(ctx, node.firstChildElement(), token_traits));
}

wcl::doc Emitter::walk_def(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  FMT_ASSERT(node.id() == CST_DEF, node, "Expected CST_DEF");

  bool is_top_level = node_traits[node].top_level;

  MEMO_RET(fmt()
               .fmt_if(CST_FLAG_GLOBAL, fmt().walk(WALK_NODE).ws())
               .fmt_if(CST_FLAG_EXPORT, fmt().walk(WALK_NODE).ws())
               .token(TOKEN_KW_DEF)
               .ws()
               .walk(is_expression, DISPATCH(walk_no_edit))
               .consume_wsnlc()
               .space()
               .token(TOKEN_P_EQUALS)
               .consume_wsnlc()
               .join(rhs_fmt(is_top_level))
               .consume_wsnlc()
               .format(ctx, node.firstChildElement(), token_traits));
}

wcl::doc Emitter::walk_export(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  MEMO_RET(walk_placeholder(ctx, node));
}

wcl::doc Emitter::walk_flag_export(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  MEMO_RET(walk_placeholder(ctx, node));
}

wcl::doc Emitter::walk_flag_global(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  MEMO_RET(walk_placeholder(ctx, node));
}

wcl::doc Emitter::walk_guard(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  MEMO_RET(walk_placeholder(ctx, node));
}

wcl::doc Emitter::walk_hole(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  MEMO_RET(walk_placeholder(ctx, node));
}

wcl::doc Emitter::walk_identifier(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  FMT_ASSERT(node.id() == CST_ID, node, "Expected CST_ID");

  MEMO_RET(fmt().token(TOKEN_ID).format(ctx, node.firstChildElement(), token_traits));
}

wcl::doc Emitter::walk_ideq(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  MEMO_RET(walk_placeholder(ctx, node));
}

wcl::doc Emitter::walk_if(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  FMT_ASSERT(node.id() == CST_IF, node, "Expected CST_IF");

  auto fits_no_nl =
      fmt()
          .fmt_if_fits_all(
              fmt()
                  .token(TOKEN_KW_IF)
                  .consume_wsnlc()
                  .space()
                  .fmt_if_else(is_simple_binop, fmt().walk(WALK_NODE),
                               fmt().next().newline())  // if cond
                  .consume_wsnlc()
                  .space()
                  .token(TOKEN_KW_THEN)
                  .consume_wsnlc()
                  .space()
                  .fmt_if_else(is_simple_apply, fmt().walk(WALK_NODE),
                               fmt().next().newline())  // true body
                  .consume_wsnlc()
                  .space()
                  .token(TOKEN_KW_ELSE)
                  .consume_wsnlc()
                  .space()
                  .fmt_if_else(is_simple_apply, fmt().walk(WALK_NODE),
                               fmt().next().newline()),  // false body
              fmt().walk_all(fmt().next()).newline())    // garbage format to fail NL check
          .format(ctx, node.firstChildElement(), token_traits);

  if (!fits_no_nl->has_newline() && !ctx.prefer_explode) {
    MEMO_RET(fits_no_nl);
  }

  MEMO_RET(fmt()
               .token(TOKEN_KW_IF)
               .consume_wsnlc()
               .space()
               .walk(is_expression, WALK_NODE)  // if cond
               .consume_wsnlc()
               .space()
               .token(TOKEN_KW_THEN)
               .consume_wsnlc()
               .nest(fmt().freshline().walk(is_expression, WALK_NODE))  // true body
               .consume_wsnlc()
               .freshline()
               .token(TOKEN_KW_ELSE)
               .consume_wsnlc()
               // clang-format off
               // False body
               .match(
                 pred(ConstPredicate(false), fmt())
                 // For an 'else if' block, we explode in the explode case to prevent partial flat emission
                .pred({CST_IF, CST_MATCH}, fmt().space().explode(fmt().walk(WALK_NODE)))
                .pred(is_expression, fmt().nest(fmt().freshline().walk(WALK_NODE)))
                // fallthrough is fail
               )
               // clang-format on
               .format(ctx, node.firstChildElement(), token_traits));
}

wcl::doc Emitter::walk_import(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  FMT_ASSERT(node.id() == CST_IMPORT, node, "Expected CST_IMPORT");

  auto id_list_fmt = fmt().walk(WALK_NODE).fmt_if(TOKEN_WS, fmt().ws());

  MEMO_RET(
      fmt()
          .token(TOKEN_KW_FROM)
          .ws()
          .walk(CST_ID, WALK_NODE)
          .ws()
          .token(TOKEN_KW_IMPORT)
          .ws()
          .fmt_if(CST_KIND, fmt().walk(WALK_NODE).ws())
          .fmt_if(CST_ARITY, fmt().walk(WALK_NODE).ws())
          // clang-format off
          .fmt_if_else(
              TOKEN_P_HOLE,
              fmt().walk(WALK_TOKEN),
              fmt().fmt_while(
                  CST_IDEQ,
                  id_list_fmt))
          // clang-format on
          .consume_wsnlc()
          .format(ctx, node.firstChildElement(), token_traits));
}

wcl::doc Emitter::walk_interpolate(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  MEMO_RET(walk_placeholder(ctx, node));
}

wcl::doc Emitter::walk_kind(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  MEMO_RET(walk_placeholder(ctx, node));
}

wcl::doc Emitter::walk_lambda(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  FMT_ASSERT(node.id() == CST_LAMBDA, node, "Expected CST_LAMBDA");

  MEMO_RET(fmt()
               .token(TOKEN_P_BSLASH)
               .consume_wsnlc()
               .walk(is_expression, WALK_NODE)
               .consume_wsnlc()
               .space()
               .walk(is_expression, WALK_NODE)
               .format(ctx, node.firstChildElement(), token_traits));
}

wcl::doc Emitter::walk_literal(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  FMT_ASSERT(node.id() == CST_LITERAL, node, "Expected CST_LITERAL");

  // clang-format off
  auto mstr_fmt = fmt()
    .match(
      pred(TOKEN_MSTR_BEGIN, fmt().token(TOKEN_MSTR_BEGIN))
     .pred(TOKEN_MSTR_RESUME, fmt().token(TOKEN_MSTR_RESUME))
     // No otherwise, this should fail if neither are true
    )
    .fmt_while(
      {TOKEN_NL, TOKEN_WS, TOKEN_MSTR_CONTINUE},
      fmt().match(
        pred(TOKEN_WS, fmt().token(TOKEN_WS))
       .pred(TOKEN_NL, fmt().token(TOKEN_NL))
       .pred(TOKEN_MSTR_CONTINUE, fmt().token(TOKEN_MSTR_CONTINUE))
      ))
    .match(
      pred(TOKEN_MSTR_PAUSE, fmt().token(TOKEN_MSTR_PAUSE))
     .pred(TOKEN_MSTR_END, fmt().token(TOKEN_MSTR_END))
     // No otherwise, this should fail if neither are true
    );

  auto lstr_fmt = fmt()
    .match(
      pred(TOKEN_LSTR_BEGIN, fmt().token(TOKEN_LSTR_BEGIN))
     .pred(TOKEN_LSTR_RESUME, fmt().token(TOKEN_LSTR_RESUME))
     // No otherwise, this should fail if neither are true
    )
    .fmt_while(
      {TOKEN_NL, TOKEN_WS, TOKEN_LSTR_CONTINUE, TOKEN_LSTR_MID},
      fmt().match(
        pred(TOKEN_WS, fmt().token(TOKEN_WS))
       .pred(TOKEN_NL, fmt().token(TOKEN_NL))
       .pred(TOKEN_LSTR_CONTINUE, fmt().token(TOKEN_LSTR_CONTINUE))
       .pred(TOKEN_LSTR_MID, fmt().token(TOKEN_LSTR_MID))
      ))
    .match(
      pred(TOKEN_LSTR_PAUSE, fmt().token(TOKEN_LSTR_PAUSE))
     .pred(TOKEN_LSTR_END, fmt().token(TOKEN_LSTR_END))
     // No otherwise, this should fail if neither are true
    );
  // clang-format on

  auto node_fmt = fmt().walk(DISPATCH(walk_placeholder));
  auto token_fmt = fmt().walk(WALK_TOKEN);

  // clang-format off
  MEMO_RET(fmt().match(
    // TODO: starting 'pred()' function doesn't allow init lists
    pred(ConstPredicate(false), fmt())
   .pred({TOKEN_MSTR_BEGIN, TOKEN_MSTR_RESUME}, mstr_fmt)
   .pred({TOKEN_LSTR_BEGIN, TOKEN_LSTR_RESUME}, lstr_fmt)
   .pred([](wcl::doc_builder&, ctx_t, CSTElement& node,
                  const token_traits_map_t&){ return node.isNode(); }, node_fmt)
   .otherwise(token_fmt))
   .format(ctx, node.firstChildElement(), token_traits));
  // clang-format on
}

wcl::doc Emitter::walk_match(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  FMT_ASSERT(node.id() == CST_MATCH, node, "Expected CST_MATCH");

  MEMO_RET(fmt()
               .token(TOKEN_KW_MATCH)
               .ws()
               .join(pattern_fmt(CST_CASE))
               // clang-format off
               .nest(fmt()
                   .consume_wsnlc()
                   .fmt_while(
                       {CST_CASE}, fmt()
                       .freshline()
                       .walk(WALK_NODE)
                       .consume_wsnlc()))
               // clang-format on
               .format(ctx, node.firstChildElement(), token_traits));
}

wcl::doc Emitter::walk_op(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  MEMO_RET(walk_placeholder(ctx, node));
}

wcl::doc Emitter::walk_package(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  FMT_ASSERT(node.id() == CST_PACKAGE, node, "Expected CST_PACKAGE");

  MEMO_RET(fmt()
               .token(TOKEN_KW_PACKAGE)
               .ws()
               .walk(CST_ID, WALK_NODE)
               .consume_wsnlc()
               .format(ctx, node.firstChildElement(), token_traits));
}

wcl::doc Emitter::walk_paren(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  FMT_ASSERT(node.id() == CST_PAREN, node, "Expected CST_PAREN");

  ctx = ctx.binop();

  auto no_nl = fmt()
                   .token(TOKEN_P_POPEN)
                   .consume_wsnlc()
                   .walk(is_expression, WALK_NODE)
                   .consume_wsnlc()
                   .token(TOKEN_P_PCLOSE)
                   .format(ctx, node.firstChildElement(), token_traits);

  if (no_nl->newline_count() == count_allowed_newlines(token_traits, node)) {
    MEMO_RET(no_nl);
  }

  MEMO_RET(
      fmt()
          .token(TOKEN_P_POPEN)
          .nest(fmt().consume_wsnlc().freshline().walk(is_expression, WALK_NODE).consume_wsnlc())
          .freshline()
          .token(TOKEN_P_PCLOSE)
          .format(ctx, node.firstChildElement(), token_traits));
}

wcl::doc Emitter::walk_prim(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  MEMO_RET(walk_placeholder(ctx, node));
}

wcl::doc Emitter::walk_publish(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  FMT_ASSERT(node.id() == CST_PUBLISH, node, "Expected CST_PUBLISH");

  MEMO_RET(fmt()
               .token(TOKEN_KW_PUBLISH)
               .ws()
               .walk(WALK_NODE)  // identifier
               .consume_wsnlc()
               .space()
               .token(TOKEN_P_EQUALS)
               .consume_wsnlc()
               .join(rhs_fmt())
               .consume_wsnlc()
               .format(ctx, node.firstChildElement(), token_traits));
}

wcl::doc Emitter::walk_require(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  FMT_ASSERT(node.id() == CST_REQUIRE, node, "Expected CST_REQUIRE");

  auto else_fmt =
      fmt()
          .token(TOKEN_KW_ELSE)
          .fmt_if_fits_all(fmt().space().consume_wsnlc().walk(WALK_NODE),
                           fmt().nest(fmt().freshline().consume_wsnlc().walk(WALK_NODE)))
          // if the author added extra newlines, re-emit them
          .fmt_if(TOKEN_NL, fmt().next().fmt_while(TOKEN_NL, fmt().next().newline()))
          .consume_wsnlc()
          .newline()
          .freshline();

  MEMO_RET(fmt()
               .freshline()
               .token(TOKEN_KW_REQUIRE)
               .ws()
               .walk(WALK_NODE)
               .consume_wsnlc()
               .space()
               .token(TOKEN_P_EQUALS)
               .consume_wsnlc()
               .join(rhs_fmt())
               .fmt_if_else(
                   contains_req_else,
                   fmt(),  // Do nothing if true, else re-emit author added newlines
                   fmt().fmt_if(TOKEN_NL, fmt().next().fmt_while(TOKEN_NL, fmt().next().newline())))
               .consume_wsnlc()
               .newline()
               .freshline()
               .fmt_if(TOKEN_KW_ELSE, else_fmt)
               .walk(WALK_NODE)
               .consume_wsnlc()
               .format(ctx, node.firstChildElement(), token_traits));
}

wcl::doc Emitter::walk_req_else(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  MEMO_RET(walk_placeholder(ctx, node));
}

wcl::doc Emitter::walk_subscribe(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  FMT_ASSERT(node.id() == CST_SUBSCRIBE, node, "Expected CST_SUBSCRIBE");

  MEMO_RET(fmt()
               .token(TOKEN_KW_SUBSCRIBE)
               .ws()
               .walk(CST_ID, WALK_NODE)
               .format(ctx, node.firstChildElement(), token_traits));
}

wcl::doc Emitter::walk_target(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  FMT_ASSERT(node.id() == CST_TARGET, node, "Expected CST_TARGET");

  MEMO_RET(fmt()
               .fmt_if(CST_FLAG_GLOBAL, fmt().walk(WALK_NODE).ws())
               .fmt_if(CST_FLAG_EXPORT, fmt().walk(WALK_NODE).ws())
               .token(TOKEN_KW_TARGET)
               .ws()
               .walk(is_expression, WALK_NODE)
               .consume_wsnlc()
               .space()
               .fmt_if(TOKEN_P_BSLASH,
                       fmt().token(TOKEN_P_BSLASH).ws().walk(WALK_NODE).space().consume_wsnlc())
               .token(TOKEN_P_EQUALS)
               .consume_wsnlc()
               .join(rhs_fmt())
               .consume_wsnlc()
               .format(ctx, node.firstChildElement(), token_traits));
}

wcl::doc Emitter::walk_target_args(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  MEMO_RET(walk_placeholder(ctx, node));
}

wcl::doc Emitter::walk_top(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  MEMO_RET(walk_placeholder(ctx, node));
}

wcl::doc Emitter::walk_topic(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  FMT_ASSERT(node.id() == CST_TOPIC, node, "Expected CST_TOPIC");

  MEMO_RET(fmt()
               .fmt_if(CST_FLAG_GLOBAL, fmt().walk(WALK_NODE).ws())
               .fmt_if(CST_FLAG_EXPORT, fmt().walk(WALK_NODE).ws())
               .token(TOKEN_KW_TOPIC)
               .ws()
               .walk({CST_ID}, WALK_NODE)
               .consume_wsnlc()
               .token(TOKEN_P_ASCRIBE)
               .consume_wsnlc()
               .space()
               .walk(is_expression, DISPATCH(walk_type))
               .consume_wsnlc()
               .format(ctx, node.firstChildElement(), token_traits));
}

wcl::doc Emitter::walk_tuple(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  FMT_ASSERT(node.id() == CST_TUPLE, node, "Expected CST_TUPLE");

  MEMO_RET(
      fmt()
          .fmt_if(CST_FLAG_GLOBAL, fmt().walk(WALK_NODE).ws())
          .fmt_if(CST_FLAG_EXPORT, fmt().walk(WALK_NODE).ws())
          .token(TOKEN_KW_TUPLE)
          .ws()
          .walk(is_expression, WALK_NODE)
          .consume_wsnlc()
          .space()
          .token(TOKEN_P_EQUALS)
          .consume_wsnlc()
          // clang-format off
          .nest(fmt().fmt_while({CST_TUPLE_ELT}, fmt()
                  .freshline()
                  .walk(WALK_NODE)
                  .consume_wsnlc()))
          // clang-format on
          .consume_wsnlc()
          .format(ctx, node.firstChildElement(), token_traits));
}

wcl::doc Emitter::walk_tuple_elt(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  MEMO_RET(walk_placeholder(ctx, node));
}

wcl::doc Emitter::walk_type(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);

  auto no_nl = walk_node(ctx, node);

  if (!no_nl->has_newline() || node.id() == CST_PAREN) {
    MEMO_RET(no_nl);
  }

  MEMO_RET(cat()
               .lit(wcl::doc::lit("("))
               .nest(cat().fmt(node, token_traits, fmt().freshline().walk(WALK_NODE)))
               .freshline()
               .lit(wcl::doc::lit(")"))
               .concat(ctx));
}

wcl::doc Emitter::walk_unary(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  FMT_ASSERT(node.id() == CST_UNARY, node, "Expected CST_UNARY");

  // clang-format off
  MEMO_RET(fmt()
               .walk(WALK_NODE)
               .consume_wsnlc()
               .walk(WALK_NODE)
               .consume_wsnlc()
               .format(ctx, node.firstChildElement(), token_traits));
  // clang-format on
}

wcl::doc Emitter::walk_error(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  MEMO_RET(walk_placeholder(ctx, node));
}

wcl::doc Emitter::place_binop(CSTElement op, bool is_flat, ctx_t ctx) {
  FMT_ASSERT(!op.isNode(), op, "Expected operator token");

  // lsep = operator defined lhs separator
  // rsep = operator defined rhs separator
  // OP = string of the op (+, -, *)
  // FR = freshline()

  // lsep OP rsep
  //   ' + '
  //   '.'
  //   ', '
  if (is_flat || op.id() == TOKEN_OP_ASSIGN) {
    return fmt()
        .lit(binop_lhs_separator(op))
        .walk(WALK_TOKEN)
        .lit(binop_rhs_separator(op))
        .compose(ctx, op, token_traits);
  }

  wcl::doc binop = walk_token(ctx, op);
  if (binop->has_newline()) {
    // OP FR
    // '''
    // , # a comment
    //
    // '''
    if (is_op_suffix(op)) {
      return fmt().walk(WALK_TOKEN).freshline().compose(ctx, op, token_traits);
    }

    // FR OP FR
    // '''
    //
    // + # a comment
    //
    // '''
    return fmt().freshline().walk(WALK_TOKEN).freshline().compose(ctx, op, token_traits);
  }

  // OP FR
  // '''
  // ,
  //
  // '''
  if (is_op_suffix(op)) {
    return fmt().walk(WALK_TOKEN).freshline().compose(ctx, op, token_traits);
  }

  // FR OP rsep
  // '''
  // + '''
  // '''
  // .'''
  return fmt()
      .freshline()
      .walk(WALK_TOKEN)
      .lit(binop_rhs_separator(op))
      .compose(ctx, op, token_traits);
}

#undef MEMO_RET
#undef MEMO
#undef WALK_TOKEN
#undef WALK_NODE
