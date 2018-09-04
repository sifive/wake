#ifndef PARSER_H
#define PARSER_H

struct Lexer;
struct Top;

void parse_top(Top &top, Lexer &lex);

#endif
