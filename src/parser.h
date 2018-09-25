#ifndef PARSER_H
#define PARSER_H

struct Lexer;
struct Top;
struct Expr;

void parse_top(Top &top, Lexer &lex);
Expr *parse_command(Lexer &lex);

#endif
