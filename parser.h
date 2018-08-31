#ifndef PARSER_H
#define PARSER_H

#include "expr.h"
struct Lexer;

DefMap::defs parse_top(Lexer &lex);

#endif
