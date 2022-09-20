/*
 * Copyright 2019 SiFive, Inc.
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

#include "syntax.h"

#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "cst.h"
#include "lexer.h"
#include "parser.h"
#include "util/diagnostic.h"
#include "util/file.h"
#include "util/segment.h"

#define STATE_IDLE 0
#define STATE_NL 1
#define STATE_NL_WS 2

void parseWake(ParseInfo pi) {
  StringSegment tinfo, tnl;

  std::vector<int> indent_stack;
  std::string indent;

  // Processing whitespace needs some state
  LexerOutput token, nl, ws;
  int state = STATE_IDLE;
  bool in_multiline_string = false;
  bool in_legacy_string = false;

  // Prepare for parsing a new file
  pi.fcontent->clearNewLines();

  // Silence a warning on older compilers
  nl.end = ws.end = nullptr;

  void *parser = ParseAlloc(malloc);
  // ParseTrace(stderr, "");

  token.end = pi.fcontent->segment().start;
  do {
    tinfo.start = token.end;

    // Grab the next token from the input file.

    // Check to see if we're still inside a multiline string
    if (in_multiline_string) in_multiline_string = ParseShifts(parser, TOKEN_MSTR_CONTINUE);
    if (in_legacy_string) in_legacy_string = ParseShifts(parser, TOKEN_LSTR_CONTINUE);

    if (in_multiline_string) {
      // Proceed lexing in multiline string context
      token = lex_mstr_continue(token.end, pi.fcontent->segment().end);
    } else if (in_legacy_string) {
      // Proceed lexing in legacy multiline string context
      token = lex_lstr_continue(token.end, pi.fcontent->segment().end);
    } else if (*token.end == '}') {
      // A '}' might signal resuming either a String, a RegExp, or an {} expression.
      // This sort of parser-context aware lexing is supported by fancier parser generators.
      // However, it's easy enough to do here by peeking into lemon's state.
      if (ParseShifts(parser, TOKEN_STR_CLOSE)) {
        token = lex_dstr(token.end, pi.fcontent->segment().end);
      } else if (ParseShifts(parser, TOKEN_REG_CLOSE)) {
        token = lex_rstr(token.end, pi.fcontent->segment().end);
      } else if (ParseShifts(parser, TOKEN_MSTR_RESUME)) {
        token = lex_mstr_resume(token.end, pi.fcontent->segment().end);
      } else if (ParseShifts(parser, TOKEN_LSTR_RESUME)) {
        token = lex_lstr_resume(token.end, pi.fcontent->segment().end);
      } else {
        token = lex_wake(token.end, pi.fcontent->segment().end);
      }
    } else {
      token = lex_wake(token.end, pi.fcontent->segment().end);
    }

    // Record this token in the CST
    tinfo.end = token.end;
    pi.cst->addToken(token.id, tinfo);

    // Whitespace-induced lexical scope is inherently not context-free.
    // We need to post-process these NL WS sequences for a CFG parser generator.
    // The basic scheme is to inject INDENT/DEDENT tokens at the first WS after a NL.
    // However, we don't want to treat empty or comment-only lines as indent changes.
    switch (state) {
      case STATE_IDLE:
        if (token.id == TOKEN_WS || token.id == TOKEN_COMMENT) {
          // Do not attempt to parse whitespace or comments; discard it.
          // Whitespace wastes the lookahead token, making the grammar LR(2).
          continue;
        } else if (token.id == TOKEN_NL) {
          pi.fcontent->addNewline(token.end);
          if (in_multiline_string || in_legacy_string) {
            break;
          } else {
            // Enter indent processing state machine
            nl = token;
            state = STATE_NL;
            // We only record+report the token info for the FIRST newline.
            // Thus, blocks own their same-line comments, but not comments on the next line.
            tnl = tinfo;
            continue;
          }
        } else {
          break;
        }
      case STATE_NL:
        if (token.id == TOKEN_WS) {
          // Record the whitespace to process later.
          ws = token;
          state = STATE_NL_WS;
          continue;
        } else {
          ws = nl;
        }
        // no break here; fall through in the no whitespace case
      case STATE_NL_WS:
        switch (token.id) {
          case TOKEN_COMMENT:
            // We just processed a comment-only line. Do not adjust indentation level!
            // Discard the comment and treat like an empty line when we hit the next NL.
            continue;

          case TOKEN_NL:
            // We just processed a completely empty line. Do not adjust indentation level!
            // Discard prior NL WS? sequence, and restart indentation processing at this NL.
            pi.fcontent->addNewline(token.end);
            nl = token;
            state = STATE_NL;
            continue;

          default:
            // Process the whitespace for a change in indentation.
            state = STATE_IDLE;
            std::string newdent(reinterpret_cast<const char *>(nl.end), ws.end - nl.end);

            if (newdent.compare(0, indent.size(), indent) != 0) {
              // Pop indent scope until indent is a prefix of newdent
              do {
                // During error recovery, if we cannot accept a DEDENT, push an NL first.
                if (!ParseShifts(parser, TOKEN_DEDENT)) Parse(parser, TOKEN_NL, tnl, pi);
                Parse(parser, TOKEN_DEDENT, tnl, pi);
                indent.resize(indent_stack.back());
                indent_stack.pop_back();
              } while (newdent.compare(0, indent.size(), indent) != 0);

              if (newdent.size() > indent.size()) {
                std::stringstream ss;
                StringSegment tws;
                tws.start = nl.end;
                tws.end = ws.end;
                ss << "syntax error; whitespace neither indents the previous line nor matches a "
                      "prior indentation level";
                pi.reporter->reportError(FileFragment(pi.fcontent, tws).location(), ss.str());
              }
            }

            if (newdent.size() > indent.size()) {
              // If newdent is longer, insert an INDENT token.
              // During error recovery, if we cannot accept an INDENT, push an NL first.
              if (!ParseShifts(parser, TOKEN_INDENT)) Parse(parser, TOKEN_NL, tnl, pi);
              Parse(parser, TOKEN_INDENT, tnl, pi);
              indent_stack.push_back(indent.size());
              std::swap(indent, newdent);
            }

            if (ParseShifts(parser, TOKEN_NL) || !ParseShifts(parser, token.id)) {
              // Newlines are whitespace (and thus a pain to parse in LR(1)).
              // However, some constructs in wake are terminated by a newline.
              // Check if the parser can shift a newline. If so, provide it.
              // If the next token is not legal in this location, force the NL.
              // This helps, because the NL often ends an erroneous statement.
              Parse(parser, TOKEN_NL, tnl, pi);
            }

            // Fall through to normal handling of the token.
            break;
        }
        break;
    }

    if (token.id == TOKEN_EOF) {
      while (!indent_stack.empty()) {
        if (!ParseShifts(parser, TOKEN_DEDENT)) Parse(parser, TOKEN_NL, tinfo, pi);
        Parse(parser, TOKEN_DEDENT, tinfo, pi);
        indent_stack.pop_back();
      }
      if (ParseShifts(parser, TOKEN_NL)) {
        Parse(parser, TOKEN_NL, tinfo, pi);
      }
    }

    if (token.id == TOKEN_MSTR_BEGIN || token.id == TOKEN_MSTR_RESUME) {
      in_multiline_string = true;
    }

    if (token.id == TOKEN_LSTR_BEGIN || token.id == TOKEN_LSTR_RESUME) {
      in_legacy_string = true;
    }

    if (!token.ok && ParseShifts(parser, token.id)) {
      // Complain about illegal token
      std::stringstream ss;
      ss << "syntax error; found illegal token " << tinfo << ", but handling it like '"
         << symbolExample(token.id) << "'";
      pi.reporter->reportError(FileFragment(pi.fcontent, tinfo).location(), ss.str());
    }

    Parse(parser, token.id, tinfo, pi);
  } while (token.id != TOKEN_EOF);

  ParseFree(parser, free);
}

const char *symbolExample(int symbol) {
  switch (symbol) {
    case TOKEN_WS:
      return "whitespace";
    case TOKEN_COMMENT:
      return "#-comment";
    case TOKEN_P_BOPEN:
      return "{";
    case TOKEN_P_BCLOSE:
      return "}";
    case TOKEN_P_SOPEN:
      return "[";
    case TOKEN_P_SCLOSE:
      return "]";
    case TOKEN_KW_PACKAGE:
      return "package";
    case TOKEN_ID:
      return "identifier";
    case TOKEN_NL:
      return "newline";
    case TOKEN_KW_FROM:
      return "from";
    case TOKEN_KW_IMPORT:
      return "import";
    case TOKEN_P_HOLE:
      return "_";
    case TOKEN_KW_EXPORT:
      return "export";
    case TOKEN_KW_DEF:
      return "def";
    case TOKEN_KW_TYPE:
      return "type";
    case TOKEN_KW_TOPIC:
      return "topic";
    case TOKEN_KW_UNARY:
      return "unary";
    case TOKEN_KW_BINARY:
      return "binary";
    case TOKEN_P_EQUALS:
      return "=";
    case TOKEN_OP_DOT:
      return ".";
    case TOKEN_OP_QUANT:
      return "quantifier";
    case TOKEN_OP_EXP:
      return "^";
    case TOKEN_OP_MULDIV:
      return "*/%";
    case TOKEN_OP_ADDSUB:
      return "+-~";
    case TOKEN_OP_COMPARE:
      return "<>";
    case TOKEN_OP_INEQUAL:
      return "!=";
    case TOKEN_OP_AND:
      return "&";
    case TOKEN_OP_OR:
      return "|";
    case TOKEN_OP_DOLLAR:
      return "$";
    case TOKEN_OP_ASSIGN:
      return ":=";
    case TOKEN_OP_COMMA:
      return ",;";
    case TOKEN_KW_GLOBAL:
      return "global";
    case TOKEN_P_ASCRIBE:
      return ":";
    case TOKEN_KW_PUBLISH:
      return "publish";
    case TOKEN_KW_DATA:
      return "data";
    case TOKEN_INDENT:
      return "increased-indentation";
    case TOKEN_DEDENT:
      return "decreased-indentation";
    case TOKEN_KW_TUPLE:
      return "tuple";
    case TOKEN_KW_TARGET:
      return "target";
    case TOKEN_P_POPEN:
      return "(";
    case TOKEN_P_PCLOSE:
      return ")";
    case TOKEN_STR_RAW:
      return "'string'";
    case TOKEN_STR_SINGLE:
      return "\"string\"";
    case TOKEN_STR_OPEN:
      return "\"string{";
    case TOKEN_STR_CLOSE:
      return "}string\"";
    case TOKEN_STR_MID:
      return "}string{";
    case TOKEN_REG_SINGLE:
      return "`regexp`";
    case TOKEN_REG_OPEN:
      return "`regexp${";
    case TOKEN_REG_CLOSE:
      return "}regexp`";
    case TOKEN_REG_MID:
      return "}regexp{";
    case TOKEN_MSTR_BEGIN:
      return "\"\"\"";
    case TOKEN_MSTR_END:
      return "\"\"\"";
    case TOKEN_MSTR_CONTINUE:
      return "string\\n";
    case TOKEN_MSTR_PAUSE:
      return "string%{";
    case TOKEN_MSTR_RESUME:
      return "}string\\n";
    case TOKEN_MSTR_MID:
      return "}string%{";
    case TOKEN_LSTR_BEGIN:
      return "\"%";
    case TOKEN_LSTR_END:
      return "%\"";
    case TOKEN_LSTR_CONTINUE:
      return "string\\n";
    case TOKEN_LSTR_PAUSE:
      return "string%{";
    case TOKEN_LSTR_RESUME:
      return "}string\\n";
    case TOKEN_LSTR_MID:
      return "}string%{";
    case TOKEN_DOUBLE:
      return "3.1415";
    case TOKEN_INTEGER:
      return "42";
    case TOKEN_KW_MACRO_HERE:
      return "@here";
    case TOKEN_KW_MACRO_LINE:
      return "@line";
    case TOKEN_KW_MACRO_FILE:
      return "@file";
    case TOKEN_KW_MACRO_BANG:
      return "@!";
    case TOKEN_KW_SUBSCRIBE:
      return "subscribe";
    case TOKEN_KW_PRIM:
      return "prim";
    case TOKEN_KW_MATCH:
      return "match";
    case TOKEN_KW_IF:
      return "if";
    case TOKEN_P_BSLASH:
      return "\\";
    case TOKEN_KW_THEN:
      return "then";
    case TOKEN_KW_ELSE:
      return "else";
    case TOKEN_KW_REQUIRE:
      return "require";
    case CST_APP:
      return "apply";
    case CST_ARITY:
      return "arity";
    case CST_BINARY:
      return "binary-op";
    case CST_BLOCK:
      return "block";
    case CST_CASE:
      return "case";
    case CST_ASCRIBE:
      return "binary-:";
    case CST_DATA:
      return "data";
    case CST_DEF:
      return "def";
    case CST_EXPORT:
      return "export";
    case CST_FLAG_EXPORT:
      return "export-flag";
    case CST_FLAG_GLOBAL:
      return "global-flag";
    case CST_GUARD:
      return "guard";
    case CST_HOLE:
      return "hole";
    case CST_ID:
      return "identifier";
    case CST_IDEQ:
      return "ideq";
    case CST_IF:
      return "if";
    case CST_IMPORT:
      return "import";
    case CST_INTERPOLATE:
      return "interpolate";
    case CST_KIND:
      return "kind";
    case CST_LAMBDA:
      return "lambda";
    case CST_LITERAL:
      return "literal";
    case CST_MATCH:
      return "match";
    case CST_OP:
      return "operator";
    case CST_PACKAGE:
      return "package";
    case CST_PAREN:
      return "paren";
    case CST_PRIM:
      return "prim";
    case CST_PUBLISH:
      return "publish";
    case CST_REQUIRE:
      return "require";
    case CST_REQ_ELSE:
      return "require-else";
    case CST_SUBSCRIBE:
      return "subscribe";
    case CST_TARGET:
      return "target";
    case CST_TARGET_ARGS:
      return "target-args";
    case CST_TOP:
      return "top";
    case CST_TOPIC:
      return "topic";
    case CST_TUPLE:
      return "tuple";
    case CST_TUPLE_ELT:
      return "element";
    case CST_UNARY:
      return "unary";
    case CST_ERROR:
      return "error";
    default:
      return "???";
  }
}

const char *symbolName(int symbol) {
  switch (symbol) {
    case TOKEN_WS:
      return "TOKEN_WS";
    case TOKEN_COMMENT:
      return "TOKEN_COMMENT";
    case TOKEN_P_BOPEN:
      return "TOKEN_P_BOPEN";
    case TOKEN_P_BCLOSE:
      return "TOKEN_P_BCLOSE";
    case TOKEN_P_SOPEN:
      return "TOKEN_P_SOPEN";
    case TOKEN_P_SCLOSE:
      return "TOKEN_P_SCLOSE";
    case TOKEN_KW_PACKAGE:
      return "TOKEN_KW_PACKAGE";
    case TOKEN_ID:
      return "TOKEN_ID";
    case TOKEN_NL:
      return "TOKEN_NL";
    case TOKEN_KW_FROM:
      return "TOKEN_KW_FROM";
    case TOKEN_KW_IMPORT:
      return "TOKEN_KW_IMPORT";
    case TOKEN_P_HOLE:
      return "TOKEN_P_HOLE";
    case TOKEN_KW_EXPORT:
      return "TOKEN_KW_EXPORT";
    case TOKEN_KW_DEF:
      return "TOKEN_KW_DEF";
    case TOKEN_KW_TYPE:
      return "TOKEN_KW_TYPE";
    case TOKEN_KW_TOPIC:
      return "TOKEN_KW_TOPIC";
    case TOKEN_KW_UNARY:
      return "TOKEN_KW_UNARY";
    case TOKEN_KW_BINARY:
      return "TOKEN_KW_BINARY";
    case TOKEN_P_EQUALS:
      return "TOKEN_P_EQUALS";
    case TOKEN_OP_DOT:
      return "TOKEN_OP_DOT";
    case TOKEN_OP_QUANT:
      return "TOKEN_OP_QUANT";
    case TOKEN_OP_EXP:
      return "TOKEN_OP_EXP";
    case TOKEN_OP_MULDIV:
      return "TOKEN_OP_MULDIV";
    case TOKEN_OP_ADDSUB:
      return "TOKEN_OP_ADDSUB";
    case TOKEN_OP_COMPARE:
      return "TOKEN_OP_COMPARE";
    case TOKEN_OP_INEQUAL:
      return "TOKEN_OP_INEQUAL";
    case TOKEN_OP_AND:
      return "TOKEN_OP_AND";
    case TOKEN_OP_OR:
      return "TOKEN_OP_OR";
    case TOKEN_OP_DOLLAR:
      return "TOKEN_OP_DOLLAR";
    case TOKEN_OP_ASSIGN:
      return "TOKEN_OP_ASSIGN";
    case TOKEN_OP_COMMA:
      return "TOKEN_OP_COMMA";
    case TOKEN_KW_GLOBAL:
      return "TOKEN_KW_GLOBAL";
    case TOKEN_P_ASCRIBE:
      return "TOKEN_P_ASCRIBE";
    case TOKEN_KW_PUBLISH:
      return "TOKEN_KW_PUBLISH";
    case TOKEN_KW_DATA:
      return "TOKEN_KW_DATA";
    case TOKEN_INDENT:
      return "TOKEN_INDENT";
    case TOKEN_DEDENT:
      return "TOKEN_DEDENT";
    case TOKEN_KW_TUPLE:
      return "TOKEN_KW_TUPLE";
    case TOKEN_KW_TARGET:
      return "TOKEN_KW_TARGET";
    case TOKEN_P_POPEN:
      return "TOKEN_P_POPEN";
    case TOKEN_P_PCLOSE:
      return "TOKEN_P_PCLOSE";
    case TOKEN_STR_RAW:
      return "TOKEN_STR_RAW";
    case TOKEN_STR_SINGLE:
      return "TOKEN_STR_SINGLE";
    case TOKEN_STR_OPEN:
      return "TOKEN_STR_OPEN";
    case TOKEN_STR_CLOSE:
      return "TOKEN_STR_CLOSE";
    case TOKEN_STR_MID:
      return "TOKEN_STR_MID";
    case TOKEN_REG_SINGLE:
      return "TOKEN_REG_SINGLE";
    case TOKEN_REG_OPEN:
      return "TOKEN_REG_OPEN";
    case TOKEN_REG_CLOSE:
      return "TOKEN_REG_CLOSE";
    case TOKEN_REG_MID:
      return "TOKEN_REG_MID";
    case TOKEN_MSTR_BEGIN:
      return "TOKEN_MSTR_BEGIN";
    case TOKEN_MSTR_END:
      return "TOKEN_MSTR_END";
    case TOKEN_MSTR_CONTINUE:
      return "TOKEN_MSTR_CONTINUE";
    case TOKEN_MSTR_PAUSE:
      return "TOKEN_MSTR_PAUSE";
    case TOKEN_MSTR_RESUME:
      return "TOKEN_MSTR_RESUME";
    case TOKEN_MSTR_MID:
      return "TOKEN_MSTR_MID";
    case TOKEN_LSTR_BEGIN:
      return "TOKEN_LSTR_BEGIN";
    case TOKEN_LSTR_END:
      return "TOKEN_LSTR_END";
    case TOKEN_LSTR_CONTINUE:
      return "TOKEN_LSTR_CONTINUE";
    case TOKEN_LSTR_PAUSE:
      return "TOKEN_LSTR_PAUSE";
    case TOKEN_LSTR_RESUME:
      return "TOKEN_LSTR_RESUME";
    case TOKEN_LSTR_MID:
      return "TOKEN_LSTR_MID";
    case TOKEN_DOUBLE:
      return "TOKEN_DOUBLE";
    case TOKEN_INTEGER:
      return "TOKEN_INTEGER";
    case TOKEN_KW_MACRO_HERE:
      return "TOKEN_KW_MACRO_HERE";
    case TOKEN_KW_MACRO_LINE:
      return "TOKEN_KW_MACRO_LINE";
    case TOKEN_KW_MACRO_FILE:
      return "TOKEN_KW_MACRO_FILE";
    case TOKEN_KW_MACRO_BANG:
      return "TOKEN_KW_MACRO_BANG";
    case TOKEN_KW_SUBSCRIBE:
      return "TOKEN_KW_SUBSCRIBE";
    case TOKEN_KW_PRIM:
      return "TOKEN_KW_PRIM";
    case TOKEN_KW_MATCH:
      return "TOKEN_KW_MATCH";
    case TOKEN_KW_IF:
      return "TOKEN_KW_IF";
    case TOKEN_P_BSLASH:
      return "TOKEN_P_BSLASH";
    case TOKEN_KW_THEN:
      return "TOKEN_KW_THEN";
    case TOKEN_KW_ELSE:
      return "TOKEN_KW_ELSE";
    case TOKEN_KW_REQUIRE:
      return "TOKEN_KW_REQUIRE";
    case CST_APP:
      return "CST_APP";
    case CST_ARITY:
      return "CST_ARITY";
    case CST_BINARY:
      return "CST_BINARY";
    case CST_BLOCK:
      return "CST_BLOCK";
    case CST_CASE:
      return "CST_CASE";
    case CST_ASCRIBE:
      return "CST_ASCRIBE";
    case CST_DATA:
      return "CST_DATA";
    case CST_DEF:
      return "CST_DEF";
    case CST_EXPORT:
      return "CST_EXPORT";
    case CST_FLAG_EXPORT:
      return "CST_FLAG_EXPORT";
    case CST_FLAG_GLOBAL:
      return "CST_FLAG_GLOBAL";
    case CST_GUARD:
      return "CST_GUARD";
    case CST_HOLE:
      return "CST_HOLE";
    case CST_ID:
      return "CST_ID";
    case CST_IDEQ:
      return "CST_IDEQ";
    case CST_IF:
      return "CST_IF";
    case CST_IMPORT:
      return "CST_IMPORT";
    case CST_INTERPOLATE:
      return "CST_INTERPOLATE";
    case CST_KIND:
      return "CST_KIND";
    case CST_LAMBDA:
      return "CST_LAMBDA";
    case CST_LITERAL:
      return "CST_LITERAL";
    case CST_MATCH:
      return "CST_MATCH";
    case CST_OP:
      return "CST_OP";
    case CST_PACKAGE:
      return "CST_PACKAGE";
    case CST_PAREN:
      return "CST_PAREN";
    case CST_PRIM:
      return "CST_PRIM";
    case CST_PUBLISH:
      return "CST_PUBLISH";
    case CST_REQUIRE:
      return "CST_REQUIRE";
    case CST_REQ_ELSE:
      return "CST_REQ_ELSE";
    case CST_SUBSCRIBE:
      return "CST_SUBSCRIBE";
    case CST_TARGET:
      return "CST_TARGET";
    case CST_TARGET_ARGS:
      return "CST_TARGET_ARGS";
    case CST_TOP:
      return "CST_TOP";
    case CST_TOPIC:
      return "CST_TOPIC";
    case CST_TUPLE:
      return "CST_TUPLE";
    case CST_TUPLE_ELT:
      return "CST_TUPLE_ELT";
    case CST_UNARY:
      return "CST_UNARY";
    case CST_ERROR:
      return "CST_ERROR";
    default:
      return "???";
  }
}
