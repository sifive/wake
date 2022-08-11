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

#define FORMAT_OFF_COMMENT "# wake-format off"

#define WALK_NODE [this](ctx_t ctx, CSTElement node) { return walk_node(ctx, node); }
#define WALK_TOKEN [this](ctx_t ctx, CSTElement node) { return walk_token(ctx, node); }

#define MEMO(ctx, node)                                                                       \
  static std::unordered_map<std::pair<CSTElement, ctx_t>, wcl::doc> __memo_map__ = {};        \
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

static bool requires_nl(cst_id_t type) { return type == CST_BLOCK || type == CST_REQUIRE; }

static bool is_expression(cst_id_t type) {
  return type == CST_ID || type == CST_APP || type == CST_LITERAL || type == CST_HOLE ||
         type == CST_BINARY;
}

auto Emitter::rhs_fmt() {
  auto rhs_fmt = fmt().walk(WALK_NODE);

  auto comment_fmt = fmt().nest(fmt().newline().token(TOKEN_COMMENT).consume_wsnl().join(rhs_fmt));
  auto nl_required_fmt = fmt().nest(rhs_fmt);
  auto flat_fmt = fmt().space().join(rhs_fmt);
  auto full_fmt = fmt().nest(fmt().newline().join(rhs_fmt));

  // clang-format off
  return fmt().match(
    pred(TOKEN_COMMENT, comment_fmt)
   .pred(requires_nl, nl_required_fmt)
   .pred_fits(flat_fmt)
   .otherwise(full_fmt));
  // clang-format on
}

wcl::doc Emitter::layout(CST cst) {
  ctx_t ctx;
  mark_no_format_nodes(cst.root());
  return walk(ctx, cst.root());
}

wcl::doc Emitter::walk(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);

  auto node_fmt = fmt().walk(WALK_NODE).newline();

  // clang-format off
  auto body_fmt = fmt().match(
    pred(TOKEN_WS, fmt().next())
   .pred(TOKEN_NL, fmt().next())
   .pred(TOKEN_COMMENT, fmt().walk(WALK_TOKEN).newline())
   .pred(CST_IMPORT, node_fmt)
   .pred(CST_DEF, node_fmt.join(fmt().newline().newline()))
   .otherwise(node_fmt.join(fmt().newline())));
  // clang-format on

  MEMO_RET(fmt().walk_children(body_fmt).format(ctx, node));
}

wcl::doc Emitter::walk_node(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  assert(node.isNode());

  wcl::doc_builder bdr;

  if (traits[node].format_off) {
    bdr.append(walk_no_edit(ctx, node));
    return std::move(bdr).build();
  }

  switch (node.id()) {
    case CST_ARITY:
      bdr.append(walk_arity(ctx, node));
      break;
    case CST_APP:
      bdr.append(walk_apply(ctx, node));
      break;
    case CST_ASCRIBE:
      bdr.append(walk_ascribe(ctx, node));
      break;
    case CST_BINARY:
      bdr.append(walk_binary(ctx, node));
      break;
    case CST_BLOCK:
      bdr.append(walk_block(ctx, node));
      break;
    case CST_CASE:
      bdr.append(walk_case(ctx, node));
      break;
    case CST_DATA:
      bdr.append(walk_data(ctx, node));
      break;
    case CST_DEF:
      bdr.append(walk_def(ctx, node));
      break;
    case CST_EXPORT:
      bdr.append(walk_export(ctx, node));
      break;
    case CST_FLAG_EXPORT:
      bdr.append(walk_flag_export(ctx, node));
      break;
    case CST_FLAG_GLOBAL:
      bdr.append(walk_flag_global(ctx, node));
      break;
    case CST_GUARD:
      bdr.append(walk_guard(ctx, node));
      break;
    case CST_HOLE:
      bdr.append(walk_hole(ctx, node));
      break;
    case CST_ID:
      bdr.append(walk_identifier(ctx, node));
      break;
    case CST_IDEQ:
      bdr.append(walk_ideq(ctx, node));
      break;
    case CST_IF:
      bdr.append(walk_if(ctx, node));
      break;
    case CST_IMPORT:
      bdr.append(walk_import(ctx, node));
      break;
    case CST_INTERPOLATE:
      bdr.append(walk_interpolate(ctx, node));
      break;
    case CST_KIND:
      bdr.append(walk_kind(ctx, node));
      break;
    case CST_LAMBDA:
      bdr.append(walk_lambda(ctx, node));
      break;
    case CST_LITERAL:
      bdr.append(walk_literal(ctx, node));
      break;
    case CST_MATCH:
      bdr.append(walk_match(ctx, node));
      break;
    case CST_OP:
      bdr.append(walk_op(ctx, node));
      break;
    case CST_PACKAGE:
      bdr.append(walk_package(ctx, node));
      break;
    case CST_PAREN:
      bdr.append(walk_paren(ctx, node));
      break;
    case CST_PRIM:
      bdr.append(walk_prim(ctx, node));
      break;
    case CST_PUBLISH:
      bdr.append(walk_publish(ctx, node));
      break;
    case CST_REQUIRE:
      bdr.append(walk_require(ctx, node));
      break;
    case CST_REQ_ELSE:
      bdr.append(walk_req_else(ctx, node));
      break;
    case CST_SUBSCRIBE:
      bdr.append(walk_subscribe(ctx, node));
      break;
    case CST_TARGET:
      bdr.append(walk_target(ctx, node));
      break;
    case CST_TARGET_ARGS:
      bdr.append(walk_target_args(ctx, node));
      break;
    case CST_TOP:
      bdr.append(walk_top(ctx, node));
      break;
    case CST_TOPIC:
      bdr.append(walk_topic(ctx, node));
      break;
    case CST_TUPLE:
      bdr.append(walk_tuple(ctx, node));
      break;
    case CST_TUPLE_ELT:
      bdr.append(walk_tuple_elt(ctx, node));
      break;
    case CST_UNARY:
      bdr.append(walk_unary(ctx, node));
      break;
    case CST_ERROR:
      bdr.append(walk_error(ctx, node));
      break;
    default:
      assert(false);
  }

  MEMO_RET(std::move(bdr).build())
}

wcl::doc Emitter::walk_placeholder(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  assert(node.isNode());

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

  if (!node.isNode()) {
    MEMO_RET(wcl::doc::lit(node.fragment().segment().str()));
  }

  wcl::doc_builder bdr;
  for (CSTElement child = node.firstChildElement(); !child.empty(); child.nextSiblingElement()) {
    bdr.append(walk_no_edit(ctx, child));
  }

  MEMO_RET(std::move(bdr).build());
}

void Emitter::mark_no_format_nodes(CSTElement node) {
  assert(node.isNode());

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
        assert(!child.empty());

        traits[block_item] = traits[block_item].turn_format_off();
        continue;
      }

      traits[child] = traits[child].turn_format_off();
    }
  }
}

wcl::doc Emitter::walk_token(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  assert(!node.isNode());

  switch (node.id()) {
    case TOKEN_KW_MACRO_HERE:
      MEMO_RET(wcl::doc::lit("@here"))
    case TOKEN_NL:
      MEMO_RET(wcl::doc::lit("\n"));
    case TOKEN_WS:
      MEMO_RET(wcl::doc::lit(" "));
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
      MEMO_RET(wcl::doc::lit(node.fragment().segment().str()));
    default:
      assert(false);
  }
}

wcl::doc Emitter::walk_apply(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  assert(node.id() == CST_APP);

  MEMO_RET(fmt()
               .walk(is_expression, WALK_NODE)
               .consume_wsnl()
               .space()
               .walk(WALK_NODE)
               .format(ctx, node.firstChildElement()));
}

wcl::doc Emitter::walk_arity(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  MEMO_RET(walk_placeholder(ctx, node));
}

wcl::doc Emitter::walk_ascribe(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  MEMO_RET(walk_placeholder(ctx, node));
}

wcl::doc Emitter::walk_binary(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  assert(node.id() == CST_BINARY);

  MEMO_RET(fmt()
               .walk(is_expression, WALK_NODE)
               .consume_wsnl()
               .space()
               .walk(CST_OP, WALK_NODE)
               .consume_wsnl()
               .space()
               .walk(is_expression, WALK_NODE)
               .format(ctx, node.firstChildElement()));
}

wcl::doc Emitter::walk_block(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  assert(node.id() == CST_BLOCK);

  // clang-format off
  auto body_fmt = fmt().match(
    pred(TOKEN_WS, fmt().next())
   .pred(TOKEN_NL, fmt().next())
   .pred(TOKEN_COMMENT, fmt().newline().token(TOKEN_COMMENT))
   .otherwise(fmt().newline().walk(WALK_NODE)));
  // clang-format on

  MEMO_RET(fmt().walk_children(body_fmt).consume_wsnl().format(ctx, node));
}

wcl::doc Emitter::walk_case(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  assert(node.id() == CST_CASE);

  MEMO_RET(fmt()
               .walk(WALK_NODE)
               .consume_wsnl()
               .space()
               .walk(CST_GUARD, WALK_NODE)
               .consume_wsnl()
               .join(rhs_fmt())
               .format(ctx, node.firstChildElement()));
}

wcl::doc Emitter::walk_data(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  MEMO_RET(walk_placeholder(ctx, node));
}

wcl::doc Emitter::walk_def(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  assert(node.id() == CST_DEF);

  MEMO_RET(fmt()
               .fmt_if(CST_FLAG_EXPORT, fmt().walk(WALK_NODE).ws())
               .token(TOKEN_KW_DEF)
               .ws()
               .walk({CST_ID, CST_APP, CST_ASCRIBE}, WALK_NODE)
               .ws()
               .token(TOKEN_P_EQUALS)
               .consume_wsnl()
               .join(rhs_fmt())
               .consume_wsnl()
               .format(ctx, node.firstChildElement()));
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
  assert(node.id() == CST_ID);

  MEMO_RET(fmt().token(TOKEN_ID).format(ctx, node.firstChildElement()));
}

wcl::doc Emitter::walk_ideq(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  MEMO_RET(walk_placeholder(ctx, node));
}

wcl::doc Emitter::walk_if(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  MEMO_RET(walk_placeholder(ctx, node));
}

wcl::doc Emitter::walk_import(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  assert(node.id() == CST_IMPORT);

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
          .consume_wsnl()
          .format(ctx, node.firstChildElement()));
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
  MEMO_RET(walk_placeholder(ctx, node));
}

wcl::doc Emitter::walk_literal(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  MEMO_RET(walk_placeholder(ctx, node));
}

wcl::doc Emitter::walk_match(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  assert(node.id() == CST_MATCH);

  MEMO_RET(
      fmt()
          .token(TOKEN_KW_MATCH)
          .ws()
          .walk(WALK_NODE)
          // clang-format off
               .nest(fmt()
                   .consume_wsnl()
                   .fmt_while(
                       {CST_CASE, TOKEN_COMMENT}, fmt()
                       .newline()
                       .fmt_if_else(
                         TOKEN_COMMENT,
                         fmt().token(TOKEN_COMMENT),
                         fmt().walk(WALK_NODE))
                       .consume_wsnl()))
          // clang-format on
          .format(ctx, node.firstChildElement()));
}

wcl::doc Emitter::walk_op(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  MEMO_RET(walk_placeholder(ctx, node));
}

wcl::doc Emitter::walk_package(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  assert(node.id() == CST_PACKAGE);

  MEMO_RET(fmt()
               .token(TOKEN_KW_PACKAGE)
               .ws()
               .walk(CST_ID, WALK_NODE)
               .consume_wsnl()
               .format(ctx, node.firstChildElement()));
}

wcl::doc Emitter::walk_paren(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  MEMO_RET(walk_placeholder(ctx, node));
}

wcl::doc Emitter::walk_prim(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  MEMO_RET(walk_placeholder(ctx, node));
}

wcl::doc Emitter::walk_publish(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  MEMO_RET(walk_placeholder(ctx, node));
}

wcl::doc Emitter::walk_require(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  assert(node.id() == CST_REQUIRE);

  MEMO_RET(fmt()
               .newline()
               .token(TOKEN_KW_REQUIRE)
               .ws()
               .walk(WALK_NODE)
               .consume_wsnl()
               .space()
               .token(TOKEN_P_EQUALS)
               .consume_wsnl()
               .join(rhs_fmt())
               .consume_wsnl()
               .newline()
               .walk(WALK_NODE)
               .consume_wsnl()
               .format(ctx, node.firstChildElement()));
}

wcl::doc Emitter::walk_req_else(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  MEMO_RET(walk_placeholder(ctx, node));
}

wcl::doc Emitter::walk_subscribe(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  assert(node.id() == CST_SUBSCRIBE);

  MEMO_RET(fmt()
               .token(TOKEN_KW_SUBSCRIBE)
               .ws()
               .walk(CST_ID, WALK_NODE)
               .format(ctx, node.firstChildElement()));
}

wcl::doc Emitter::walk_target(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  MEMO_RET(walk_placeholder(ctx, node));
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
  MEMO_RET(walk_placeholder(ctx, node));
}

wcl::doc Emitter::walk_tuple(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  MEMO_RET(walk_placeholder(ctx, node));
}

wcl::doc Emitter::walk_tuple_elt(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  MEMO_RET(walk_placeholder(ctx, node));
}

wcl::doc Emitter::walk_unary(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  MEMO_RET(walk_placeholder(ctx, node));
}

wcl::doc Emitter::walk_error(ctx_t ctx, CSTElement node) {
  MEMO(ctx, node);
  MEMO_RET(walk_placeholder(ctx, node));
}

#undef MEMO_RET
#undef MEMO
#undef WALK_TOKEN
#undef WALK_NODE
