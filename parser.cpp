#include "parser.h"
#include "expr.h"
#include "symbol.h"
#include "value.h"
#include <iostream>
#include <list>

//#define TRACE(x) do { fprintf(stderr, "%s\n", x); } while (0)
#define TRACE(x) do { } while (0)

struct op_type {
  int p;
  int l;
  op_type(int p_, int l_) : p(p_), l(l_) { }
  op_type() : p(-1), l(-1) { }
};

static op_type precedence(char c) {
  switch (c) {
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

bool expect(SymbolType type, Lexer &lex) {
  if (lex.next.type != type) {
    std::cerr << "Was expecting a "
      << symbolTable[type] << ", but got a "
      << symbolTable[lex.next.type] << " at "
      << lex.next.location.str() << std::endl;
    lex.fail = true;
    return false;
  }
  return true;
}

static std::pair<std::string, Location> get_arg_loc(Lexer &lex) {
  if (lex.next.type != ID && lex.next.type != DROP) {
    std::cerr << "Was expecting an ID/DROP argument, but got a "
      << symbolTable[lex.next.type] << " at "
      << lex.next.location.str() << std::endl;
    lex.fail = true;
  }

  auto out = std::make_pair(lex.text(), lex.next.location);
  lex.consume();
  return out;
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
      Expr* out = new VarRef(lex.next.location, lex.text());
      lex.consume();
      return out;
    }
    case LITERAL: {
      Expr *out = new Literal(lex.next.location, std::move(lex.next.value));
      lex.consume();
      return out;
    }
    case LAMBDA: {
      Location location = lex.next.location;
      lex.consume();
      auto name = get_arg_loc(lex);
      auto term = parse_term(lex);
      location.end = term->location.end;
      return new Lambda(location, name.first, term);
    }
    case POPEN: {
      Location location = lex.next.location;
      lex.consume();
      auto x = parse_sum(0, lex);
      location.end = lex.next.location.end;
      if (expect(PCLOSE, lex)) lex.consume();
      x->location = location;
      return x;
    }
    default: {
      std::cerr << "Was expecting an ID/LAMBDA/POPEN/OPERATOR/LITERAL, got a "
        << symbolTable[lex.next.type] << " at "
        << lex.next.location.str() << std::endl;
      lex.fail = true;
      return new Literal(Location(), std::unique_ptr<Value>(new String("bad")));
    }
  }
}

static Expr* parse_product(Lexer &lex) {
  TRACE("PRODUCT");
  auto product = parse_term(lex);

  for (;;) {
    switch (lex.next.type) {
      case ID:
      case LITERAL:
      case LAMBDA:
      case POPEN: {
        auto arg = parse_term(lex);
        Location location = product->location;
        location.end = arg->location.end;
        product = new App(location, product, arg);
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
  std::string name;
  op_type op;
  while (lex.next.type == OPERATOR && (op = precedence((name = lex.text())[0])).p >= p) {
    auto opp = new VarRef(lex.next.location, name);
    lex.consume();
    auto rhs = parse_sum(op.p + op.l, lex);
    Location app1_loc = lhs->location;
    Location app2_loc = lhs->location;
    app1_loc.end = opp->location.end;
    app2_loc.end = rhs->location.end;
    lhs = new App(app2_loc, new App(app1_loc, opp, lhs), rhs);
  }
  return lhs;
}

static DefMap::defs parse_defs(Lexer &lex) {
  TRACE("DEFS");
  std::map<std::string, std::unique_ptr<Expr> > map;

  while (lex.next.type == DEF) {
    lex.consume();

    std::list<std::pair<std::string, Location> > args;
    while (lex.next.type == ID || lex.next.type == DROP) args.push_back(get_arg_loc(lex));

    std::string name;
    if (lex.next.type == OPERATOR && args.size() == 1) {
      name = lex.text();
      lex.consume();
      args.push_back(get_arg_loc(lex));
    } else {
      name = args.front().first;
      args.pop_front();
    }

    if (map.find(name) != map.end()) {
      std::cerr << "Duplicate def "
        << name << " at "
        << map[name]->location.str() << " and "
        << lex.next.location.str() << std::endl;
      lex.fail = true;
    }

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
    for (auto i = args.rbegin(); i != args.rend(); ++i) {
      Location location = body->location;
      location.start = i->second.start;
      body = new Lambda(location, i->first, body);
    }

    map[name] = std::unique_ptr<Expr>(body);
  }
  return map;
}

static Expr* parse_block(Lexer &lex) {
  TRACE("BLOCK");
  Location location = lex.next.location;
  DefMap::defs map = parse_defs(lex);
  auto body = parse_sum(0, lex);
  location.end = body->location.end;
  expect(EOL, lex);
  lex.consume();
  return new DefMap(location, map, body);
}

DefMap::defs parse_top(Lexer &lex) {
  if (lex.next.type == EOL) lex.consume();
  DefMap::defs out = parse_defs(lex);
  expect(END, lex);
  return out;
}
