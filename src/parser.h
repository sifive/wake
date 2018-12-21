#ifndef PARSER_H
#define PARSER_H

#include "symbol.h"

struct Top;
struct Expr;

bool expect(SymbolType type, Lexer &lex);
void parse_top(Top &top, Lexer &lex);
Expr *parse_command(Lexer &lex);
Expr *parse_block(Lexer &lex);

// These types must be defined by prim.wake
struct Sum;
extern Sum *Bool; // True | False
extern Sum *List; // Nil | a, b
extern Sum *Pair; // Pair a b

#endif
