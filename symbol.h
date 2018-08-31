#ifndef SYMBOL_H
#define SYMBOL_H

#include "location.h"
#include <stdio.h>
#include <list>
#include <memory>

enum SymbolType { ERROR, ID, OPERATOR, LITERAL, DEF, LAMBDA, EQUALS, POPEN, PCLOSE, END, EOL, INDENT, DEDENT };
extern const char *symbolTable[];

struct Value;
struct Symbol {
  SymbolType type;
  Location location;
  std::unique_ptr<Value> value;

  Symbol(SymbolType type_, const Location& location_, Value* value_) : type(type_), location(location_), value(value_) { }
  Symbol() : type(ERROR), location() { }
};

struct input_t;
struct state_t;

struct Lexer {
  std::unique_ptr<input_t> engine;
  std::unique_ptr<state_t> state;
  Symbol next;
  bool fail;

  Lexer(const char *file);
  ~Lexer();

  std::string text();
  void consume();
};

#endif
