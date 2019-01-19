#ifndef SYMBOL_H
#define SYMBOL_H

#include "location.h"
#include <memory>
#include <cstdint>

enum SymbolType {
  ERROR, ID, OPERATOR, LITERAL, DEF, TUPLE, GLOBAL, PUBLISH, SUBSCRIBE, PRIM, LAMBDA,
  DATA, EQUALS, POPEN, PCLOSE, BOPEN, BCLOSE, IF, THEN, ELSE, HERE, MEMOIZE, END,
  MATCH, EOL, INDENT, DEDENT
};
extern const char *symbolTable[];

struct Expr;
struct Symbol {
  SymbolType type;
  Location location;
  std::unique_ptr<Expr> expr;

  Symbol(SymbolType type_, const Location &location_) : type(type_), location(location_) { }
  Symbol(SymbolType type_, const Location &location_, Expr *expr_) : type(type_), location(location_), expr(expr_) { }
};

struct input_t;
struct state_t;

struct Lexer {
  std::unique_ptr<input_t> engine;
  std::unique_ptr<state_t> state;
  Symbol next;
  bool fail;

  Lexer(const char *file);
  Lexer(const std::string &cmdline, const char *target);
  ~Lexer();

  std::string text() const;
  void consume();

  static bool isUpper(const char *str); // unicode-upper
  static bool isLower(const char *str); // unicode-letter \ unicode-upper
  static bool isOperator(const char *str);
};

struct op_type {
  int p;
  int l;
  op_type(int p_, int l_) : p(p_), l(l_) { }
  op_type() : p(-1), l(-1) { }
};

bool push_utf8(std::string &result, uint32_t c);
int pop_utf8(uint32_t *rune, const char *str);

op_type op_precedence(const char *str);

#endif
