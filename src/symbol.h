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

#ifndef SYMBOL_H
#define SYMBOL_H

#include "location.h"
#include <memory>

enum SymbolType {
  ERROR, ID, OPERATOR, LITERAL, DEF, TUPLE, GLOBAL, PUBLISH, SUBSCRIBE, PRIM, LAMBDA,
  DATA, EQUALS, POPEN, PCLOSE, BOPEN, BCLOSE, IF, THEN, ELSE, HERE, END,
  MATCH, EOL, INDENT, DEDENT, COLON, TARGET, PACKAGE, IMPORT, EXPORT, FROM,
  TYPE, TOPIC, UNARY, BINARY, REQUIRE
};
extern const char *symbolTable[];

struct Expr;
struct Symbol {
  SymbolType type;
  Location location;
  std::unique_ptr<Expr> expr;

  Symbol(SymbolType type_, const Location &location_) : type(type_), location(location_) { }
  Symbol(SymbolType type_, const Location &location_, Expr *expr_) : type(type_), location(location_), expr(expr_) { }
};

struct input_t;
struct state_t;

struct Heap;
struct Lexer {
  Heap &heap;
  std::unique_ptr<input_t> engine;
  std::unique_ptr<state_t> state;
  Symbol next;
  bool fail;

  Lexer(Heap &heap_, const char *file);
  Lexer(Heap &heap_, const std::string &cmdline, const char *target);
  ~Lexer();

  std::string id() const;
  void consume();

  static bool isUpper(const char *str); // unicode-upper
  static bool isLower(const char *str); // unicode-letter \ unicode-upper
  static bool isOperator(const char *str);
};

struct op_type {
  int p;
  int l;
  op_type(int p_, int l_) : p(p_), l(l_) { }
  op_type() : p(-1), l(-1) { }
};

op_type op_precedence(const char *str);

#endif
