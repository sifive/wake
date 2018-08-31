#include "expr.h"
#include "symbol.h"
#include "parser.h"

//#define TRACE(x) do { fprintf(stderr, "%s\n", x); } while (0)
#define TRACE(x) do { } while (0)

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

void expect(SymbolType type, Lexer &lex) {
  if (lex.next.type != type) {
    fprintf(stderr, "Was expecting a %s, but got a %s at %s\n", symbolTable[type], symbolTable[lex.next.type], lex.location());
    exit(1);
  }
}

static std::string get_id(Lexer &lex) {
  expect(ID, lex);
  std::string name(lex.next.start, lex.next.end);
  lex.consume();
  return name;
}

static Expr* parse_term(Lexer &lex);
static Expr* parse_product(Lexer &lex);
static Expr* parse_sum(int precedence, Lexer &lex);
static DefMap::defs parse_defs(Lexer &lex);
static Expr* parse_block(Lexer &lex);

static Expr* parse_term(Lexer &lex) {
  TRACE("TERM");
  switch (lex.next.type) {
    case ID: {
      std::string location = lex.location();
      auto name = get_id(lex);
      return new VarRef(name, location);
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
      expect(PCLOSE, lex);
      lex.consume();
      return x;
    }
    default: {
      expect(POPEN, lex);
      return 0;
    }
  }
}

static Expr* parse_product(Lexer &lex) {
  TRACE("PRODUCT");
  auto product = parse_term(lex);

  for (;;) {
    switch (lex.next.type) {
      case ID:
      case STRING:
      case LAMBDA:
      case POPEN: {
        auto arg = parse_term(lex);
        product = new App(product, arg);
        break;
      }
      default: {
        return product;
      }
    }
  }
}

static Expr* parse_sum(int p, Lexer &lex) {
  TRACE("SUM");
  auto lhs = parse_product(lex);
  op_type op;
  while (lex.next.type == OPERATOR && (op = precedence(lex.next)).p >= p) {
    std::string name(lex.next.start, lex.next.end);
    std::string location = lex.location();
    lex.consume();
    auto var = new VarRef(name, location);
    auto rhs = parse_sum(op.p + op.l, lex);
    lhs = new App(new App(var, lhs), rhs);
  }
  return lhs;
}

static DefMap::defs parse_defs(Lexer &lex) {
  TRACE("DEFS");
  std::map<std::string, std::unique_ptr<Expr> > map;

  while (lex.next.type == DEF) {
    lex.consume();

    std::string name = get_id(lex);
    if (map.find(name) != map.end()) {
      fprintf(stderr, "Duplicate def %s at %s\n", name.c_str(), lex.location());
      exit(1);
    }

    std::list<std::string> args;
    while (lex.next.type == ID) args.push_back(get_id(lex));

    expect(EQUALS, lex);
    lex.consume();

    Expr *body;
    if (lex.next.type == EOL) {
      lex.consume();
      expect(INDENT, lex);
      lex.consume();
      body = parse_block(lex);
      expect(DEDENT, lex);
      lex.consume();
    } else {
      body = parse_sum(0, lex);
      expect(EOL, lex);
      lex.consume();
    }

    // add the arguments
    for (auto i = args.rbegin(); i != args.rend(); ++i)
      body = new Lambda(*i, body);

    map[name] = std::unique_ptr<Expr>(body);
  }
  return map;
}

static Expr* parse_block(Lexer &lex) {
  TRACE("BLOCK");
  DefMap::defs map = parse_defs(lex);
  auto body = parse_sum(0, lex);
  expect(EOL, lex);
  lex.consume();
  return new DefMap(map, body);
}

DefMap::defs parse_top(Lexer &lex) {
  DefMap::defs out = parse_defs(lex);
  expect(END, lex);
  return out;
}
