#include "expr.h"
#include "symbol.h"
#include "parser.h"

struct op_type {
  int p;
  int l;
  op_type(int p_, int l_) : p(p_), l(l_) { }
  op_type() : p(-1), l(-1) { }
};

static op_type precedence(Symbol x) {
  switch (x.start[0]) {
  case '.':
    return op_type(8, 1);
  case '^':
    return op_type(7, 0);
  case '*':
  case '/':
  case '%':
    return op_type(6, 1);
  case '-':
  case '+':
    return op_type(5, 1);
  // case ':':
  case '<':
  case '>':
    return op_type(4, 1);
  case '=':
  case '!':
    return op_type(3, 1);
  case '&':
    return op_type(2, 1);
  case '|':
    return op_type(1, 1);
  case ',':
    return op_type(0, 0);
  default:
    return op_type(-1, -1);
  }
}

static std::string get_id(Lexer &lex) {
  assert (lex.next.type == ID);
  std::string name(lex.next.start, lex.next.end);
  lex.consume();
  return name;
}

static Expr* parse_sum(int precedence, Lexer &lex);

static Expr* parse_term(Lexer &lex) {
  switch (lex.next.type) {
    case ID: {
      auto name = get_id(lex);
      return new VarRef(name);
    }
    // case STRING:
    case LAMBDA: {
      lex.consume();
      auto name = get_id(lex);
      auto term = parse_term(lex);
      return new Lambda(name, term);
    }
    case POPEN: {
      lex.consume();
      auto x = parse_sum(0, lex);
      assert (lex.next.type == PCLOSE);
      lex.consume();
      return x;
    }
    default: {
      printf("Expected a term, found a %s\n", symbolTable[lex.next.type]);
      assert (0);
      return 0;
    }
  }
}

static Expr* parse_product(Lexer &lex) {
  auto product = parse_term(lex);

  switch (lex.next.type) {
    case ID:
    case STRING:
    case LAMBDA:
    case POPEN: {
      auto arg = parse_term(lex);
      product = new App(product, arg);
    }
    default: {
      return product;
    }
  }
}

static Expr* parse_sum(int p, Lexer &lex) {
  auto lhs = parse_product(lex);
  op_type op;
  while (lex.next.type == OPERATOR && (op = precedence(lex.next)).p >= p) {
    std::string name(lex.next.start, lex.next.end);
    lex.consume();
    auto var = new VarRef(name);
    auto rhs = parse_sum(op.p + op.l, lex);
    return new App(new App(var, lhs), rhs);
  }
  return lhs;
}

static Expr* parse_block(Lexer &lex) {
  DefMap::defs map = parse_defs(lex);
  auto body = parse_sum(0, lex);
  return new DefMap(map, body);
}

DefMap::defs parse_defs(Lexer &lex) {
  std::map<std::string, std::unique_ptr<Expr> > map;

  while (lex.next.type == DEF) {
    lex.consume();

    std::string name = get_id(lex);
    assert (map.find(name) == map.end());

    std::list<std::string> args;
    while (lex.next.type == ID) args.push_back(get_id(lex));

    assert (lex.next.type == EQUALS);
    lex.consume();

    Expr *body;
    if (lex.next.type == EOL) {
      lex.consume();
      assert (lex.next.type == INDENT);
      lex.consume();
      body = parse_block(lex);
      assert (lex.next.type == DEDENT);
      lex.consume();
    } else {
      body = parse_sum(0, lex);
      assert (lex.next.type == EOL);
      lex.consume();
    }

    map[name] = std::unique_ptr<Expr>(body);
  }
  return map;
}
