#ifndef PARSER_H
#define PARSER_H

#include "symbol.h"

struct Top;
struct Expr;

bool expect(SymbolType type, Lexer &lex);
void parse_top(Top &top, Lexer &lex);
Expr *parse_command(Lexer &lex);
Expr *parse_block(Lexer &lex);

#endif
