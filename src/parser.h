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

#ifndef PARSER_H
#define PARSER_H

#include "symbol.h"

struct Top;
struct Expr;

bool expect(SymbolType type, Lexer &lex);
void parse_top(Top &top, Lexer &lex);
Expr *parse_command(Lexer &lex);
Expr *parse_expr(Lexer &lex);
bool sums_ok();

// These types must be defined by prim.wake
struct Sum;
extern Sum *Boolean; // True | False
extern Sum *Order; // LT | EQ | GT
extern Sum *List; // Nil | a, b
extern Sum *Pair; // Pair a b
extern Sum *Unit; // Unit
extern Sum *JValue; // JString String | JInteger Integer | JReal String | JBoolean Boolean | JNull | JObject  List (Pair String JValue) | JArray List JValue
extern Sum *Result; // Pass x | Fail y

#endif
