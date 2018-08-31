#ifndef SYMBOL_H
#define SYMBOL_H

#include <stdio.h>
#include <list>
#include <memory>

enum SymbolType { ERROR, ID, OPERATOR, STRING, DEF, LAMBDA, EQUALS, POPEN, PCLOSE, END, EOL, INDENT, DEDENT };
extern const char *symbolTable[];
struct Symbol {
  SymbolType type;
  const char *start;
  const char *end;

  Symbol(SymbolType type_, const char *start_, const char *end_) : type(type_), start(start_), end(end_) { }
  Symbol() : type(ERROR), start(0), end(0) { }
};

struct input_t;
struct state_t;

struct Lexer {
  std::unique_ptr<input_t> engine;
  std::unique_ptr<state_t> state;
  Symbol next;

  Lexer(const char *file);
  ~Lexer();

  void consume();
  const char *location();
};

#endif
