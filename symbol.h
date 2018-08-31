#ifndef SYMBOL_H
#define SYMBOL_H

#include <stdio.h>
#include <list>
#include <memory>

enum SymbolType { ERROR, ID, OPERATOR, STRING, DEF, LAMBDA, EQUALS, POPEN, PCLOSE, END, EOL, INDENT, DEDENT };

struct Symbol {
  SymbolType type;
  const char *start;
  const char *end;

  Symbol(SymbolType type_, const char *start_, const char *end_) : type(type_), start(start_), end(end_) { }
};

struct input_t;
struct state_t;

struct Lexer {
  std::unique_ptr<input_t> engine;
  std::unique_ptr<state_t> state;

  Lexer(const char *file);
  ~Lexer();

  Symbol get();
};

#endif
