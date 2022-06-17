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

#ifndef LEXER_H
#define LEXER_H

#include <stdint.h>

#include <string>

#include "util/fragment.h"
#include "util/segment.h"

// This special token is not created by lemon
#define TOKEN_EOF 0

class DiagnosticReporter;
struct LexerOutput {
  int id;              // Values defined in parser.h
  const uint8_t *end;  // Points just past the end of the Token
  bool ok;             // false: syntactically invalid Token

  LexerOutput(int id_, const uint8_t *end_, bool ok_ = true) : id(id_), end(end_), ok(ok_) {}
  LexerOutput() {}
};

LexerOutput lex_wake(const uint8_t *s, const uint8_t *e);
LexerOutput lex_dstr(const uint8_t *s, const uint8_t *e);
LexerOutput lex_rstr(const uint8_t *s, const uint8_t *e);
LexerOutput lex_mstr_resume(const uint8_t *s, const uint8_t *e);
LexerOutput lex_mstr_continue(const uint8_t *s, const uint8_t *e);
LexerOutput lex_lstr_resume(const uint8_t *s, const uint8_t *e);
LexerOutput lex_lstr_continue(const uint8_t *s, const uint8_t *e);
LexerOutput lex_printable(const uint8_t *s, const uint8_t *e);

enum IdKind { LOWER, UPPER, OPERATOR };
IdKind lex_kind(const uint8_t *s, const uint8_t *e);
inline IdKind lex_kind(const std::string &s) {
  const uint8_t *x = reinterpret_cast<const uint8_t *>(s.c_str());
  return lex_kind(x, x + s.size());
}

std::string relex_id(const uint8_t *s, const uint8_t *e);
std::string relex_string(FileFragment fragment);
std::string relex_mstring(const uint8_t *s, const uint8_t *e);
std::string relex_regexp(uint8_t id, const uint8_t *s, const uint8_t *e);

struct op_type {
  int p;
  int l;
  op_type(int p_, int l_) : p(p_), l(l_) {}
};

op_type op_precedence(const uint8_t *s, const uint8_t *e);
inline op_type op_precedence(const std::string &s) {
  const uint8_t *x = reinterpret_cast<const uint8_t *>(s.c_str());
  return op_precedence(x, x + s.size());
}

std::ostream &operator<<(std::ostream &os, StringSegment token);

#endif
