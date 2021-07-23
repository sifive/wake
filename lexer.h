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

// This special token is not created by lemon
#define TOKEN_EOF 0

struct Token {
    int id;             // Values defined in parser.h
    const uint8_t *end; // Points just past the end of the Token
    bool ok;            // false: syntactically invalid Token

    Token(int id_, const uint8_t *end_, bool ok_ = true)
    : id(id_), end(end_), ok(ok_) { }
    Token() { }
};

Token lex_wake(const uint8_t *s, const uint8_t *e);
Token lex_dstr(const uint8_t *s, const uint8_t *e);
Token lex_rstr(const uint8_t *s, const uint8_t *e);
Token lex_printable(const uint8_t *s, const uint8_t *e);

#endif
