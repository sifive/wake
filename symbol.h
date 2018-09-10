#ifndef SYMBOL_H
#define SYMBOL_H

#include "location.h"
#include <memory>

enum SymbolType {
  ERROR, ID, OPERATOR, LITERAL, DEF, GLOBAL, PUBLISH, SUBSCRIBE, PRIM, LAMBDA,
  EQUALS, POPEN, PCLOSE, IF, THEN, ELSE, END, EOL, INDENT, DEDENT
};
extern const char *symbolTable[];

struct Value;
struct Symbol {
  SymbolType type;
  Location location;
  std::shared_ptr<Value> value;

  Symbol(SymbolType type_, const Location &location_) : type(type_), location(location_) { }
  Symbol(SymbolType type_, const Location &location_, std::shared_ptr<Value> &&value_) : type(type_), location(location_), value(value_) { }
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
