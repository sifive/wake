#ifndef PARSER_H
#define PARSER_H

#include "symbol.h"

struct Top;
struct Expr;

bool expect(SymbolType type, Lexer &lex);
void parse_top(Top &top, Lexer &lex);
Expr *parse_command(Lexer &lex);
Expr *parse_expr(Lexer &lex);

// These types must be defined by prim.wake
struct Sum;
extern Sum *Boolean; // True | False
extern Sum *Order; // LT | EQ | GT
extern Sum *List; // Nil | a, b
extern Sum *Pair; // Pair a b

#endif
