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

static op_type precedence(const std::string& str) {
  char c = str[0];
  switch (c) {
  case '.':
    return op_type(13, 1);
  case 'a': // Application rules run between \\ and $
    return op_type(12, 1);
  case '$':
    return op_type(11, 0);
  case '^':
    return op_type(10, 0);
  case '*':
    return op_type(9, 1);
  case '/':
  case '%':
    return op_type(8, 1);
  case '-':
  case '~':
  // case '!': // single-character '!'
    return op_type(7, 1);
  case '+':
    return op_type(6, 1);
  case '<':
  case '>':
    return op_type(5, 1);
  case '!': // multi-character '!' (like != )
    if (str.size() == 1) return op_type(7, 1);
  case '=':
    return op_type(4, 1);
  case '&':
    return op_type(3, 1);
  case '|':
    return op_type(2, 1);
  case ',':
    return op_type(1, 0);
  case '\\': // LAMBDA
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
  if (lex.next.type != ID) {
    std::cerr << "Was expecting an ID argument, but got a "
      << symbolTable[lex.next.type] << " at "
      << lex.next.location.str() << std::endl;
    lex.fail = true;
  }

  auto out = std::make_pair(lex.text(), lex.next.location);
  lex.consume();
  return out;
}

bool expectValue(const char *type, Lexer& lex) {
  if (expect(LITERAL, lex)) {
    if (lex.next.value->type == type) {
      return true;
    } else {
      std::cerr << "Was expecting a "
        << type << ", but got a "
        << lex.next.value->type << " at "
        << lex.next.location.str() << std::endl;
      lex.fail = true;
      return false;
    }
  } else {
    return false;
  }
}

static Expr* parse_unary(int p, Lexer &lex);
static Expr* parse_binary(int p, Lexer &lex);
static Expr* parse_if(Lexer &lex);
static DefMap::defs parse_defs(Lexer &lex);
static Expr* parse_block(Lexer &lex);

static int relabel_anon(Expr* expr, int index) {
  if (expr->type == VarRef::type) {
    VarRef *ref = reinterpret_cast<VarRef*>(expr);
    if (ref->name != "_") return index;
    ++index;
    ref->name += std::to_string(index);
    return index;
  } else if (expr->type == App::type) {
    App *app = reinterpret_cast<App*>(expr);
    return relabel_anon(app->val.get(), relabel_anon(app->fn.get(), index));
  } else if (expr->type == Lambda::type) {
    Lambda *lambda = reinterpret_cast<Lambda*>(expr);
    return relabel_anon(lambda->body.get(), index);
  }
  // noop for DefMap, Literal, Prim
  return index;
}

static Expr* parse_unary(int p, Lexer &lex) {
  TRACE("UNARY");
  switch (lex.next.type) {
    // Unary operators
    case OPERATOR: {
      Location location = lex.next.location;
      op_type op = precedence(lex.text());
      if (op.p < p) {
        std::cerr << "Lower precedence unary operator "
          << lex.text() << " must use ()s at "
          << lex.next.location.str() << std::endl;
        lex.fail = true;
      }
      auto opp = new VarRef(lex.next.location, "unary " + lex.text());
      lex.consume();
      auto rhs = parse_binary(op.p + op.l, lex);
      location.end = rhs->location.end;
      return new App(location, opp, rhs);
    }
    case LAMBDA: {
      Location location = lex.next.location;
      op_type op = precedence(lex.text());
      if (op.p < p) {
        std::cerr << "Lower precedence unary operator "
          << lex.text() << " must use ()s at "
          << lex.next.location.str() << std::endl;
        lex.fail = true;
      }
      lex.consume();
      auto name = get_arg_loc(lex);
      auto rhs = parse_binary(op.p + op.l, lex);
      location.end = rhs->location.end;
      return new Lambda(location, name.first, rhs);
    }
    // Terminals
    case ID: {
      Expr *out = new VarRef(lex.next.location, lex.text());
      lex.consume();
      return out;
    }
    case LITERAL: {
      Expr *out = new Literal(lex.next.location, std::move(lex.next.value));
      lex.consume();
      return out;
    }
    case PRIM: {
      std::string name;
      Location location = lex.next.location;
      lex.consume();
      if (expectValue(String::type, lex)) {
        name = reinterpret_cast<String*>(lex.next.value.get())->value;
        location.end = lex.next.location.end;
        lex.consume();
      } else {
        name = "bad_prim";
      }
      return new Prim(location, name);
    }
    case POPEN: {
      Location location = lex.next.location;
      lex.consume();
      Expr *out = parse_if(lex);
      location.end = lex.next.location.end;
      if (expect(PCLOSE, lex)) lex.consume();
      out->location = location;
      int args = relabel_anon(out, 0);
      for (int index = args; index >= 1; --index)
        out = new Lambda(location, "_" + std::to_string(index), out);
      return out;
    }
    default: {
      std::cerr << "Was expecting an (OPERATOR/LAMBDA/ID/LITERAL/PRIM/POPEN), got a "
        << symbolTable[lex.next.type] << " at "
        << lex.next.location.str() << std::endl;
      lex.fail = true;
      return new Literal();
    }
  }
}

static Expr* parse_binary(int p, Lexer &lex) {
  TRACE("BINARY");
  auto lhs = parse_unary(p, lex);
  for (;;) {
    switch (lex.next.type) {
      case OPERATOR: {
        std::string name = lex.text();
        op_type op = precedence(name);
        if (op.p < p) return lhs;
        auto opp = new VarRef(lex.next.location, "binary " + name);
        lex.consume();
        auto rhs = parse_binary(op.p + op.l, lex);
        Location app1_loc = lhs->location;
        Location app2_loc = lhs->location;
        app1_loc.end = opp->location.end;
        app2_loc.end = rhs->location.end;
        lhs = new App(app2_loc, new App(app1_loc, opp, lhs), rhs);
        break;
      }
      case LAMBDA:
      case ID:
      case LITERAL:
      case PRIM:
      case POPEN: {
        op_type op = precedence("a"); // application
        if (op.p < p) return lhs;
        auto rhs = parse_binary(op.p + op.l, lex);
        Location location = lhs->location;
        location.end = rhs->location.end;
        lhs = new App(location, lhs, rhs);
        break;
      }
      default: {
        return lhs;
      }
    }
  }
}

static Expr* parse_if(Lexer &lex) {
  TRACE("IF");
  if (lex.next.type == IF) {
    Location l = lex.next.location;
    lex.consume();
    auto condE = parse_binary(0, lex);
    if (expect(THEN, lex)) lex.consume();
    auto thenE = parse_binary(0, lex);
    if (expect(ELSE, lex)) lex.consume();
    auto elseE = parse_binary(0, lex);
    l.end = elseE->location.end;
    return new App(l, new App(l, new App(l, condE,
      new Lambda(l, "_", thenE)),
      new Lambda(l, "_", elseE)),
      new Literal());
  } else {
    return parse_binary(0, lex);
  }
}

static DefMap::defs parse_defs(Lexer &lex) {
  TRACE("DEFS");
  std::map<std::string, std::unique_ptr<Expr> > map;

  while (lex.next.type == DEF) {
    Location def = lex.next.location;
    lex.consume();

    std::list<std::pair<std::string, Location> > args;
    while (lex.next.type == ID) args.push_back(get_arg_loc(lex));

    std::string name;
    if (lex.next.type == OPERATOR) {
      if (args.empty()) {
        name = "unary " + lex.text();
        lex.consume();
        args.push_back(get_arg_loc(lex));
      } else if (args.size() == 1) {
        name = "binary " + lex.text();
        lex.consume();
        args.push_back(get_arg_loc(lex));
      } else {
        name = "broken";
        std::cerr << "Operator def is neither unary nor binary at " << def.str() << std::endl;
        lex.fail = true;
      }
    } else {
      if (args.empty()) {
        name = "broken";
        std::cerr << "def has no name at " << def.str() << std::endl;
        lex.fail = true;
      } else {
        name = args.front().first;
        args.pop_front();
      }
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
      body = parse_if(lex);
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
  auto body = parse_if(lex);
  location.end = body->location.end;
  expect(EOL, lex);
  lex.consume();
  return new DefMap(location, map, body);
}

DefMap::defs parse_top(Lexer &lex) {
  TRACE("TOP");
  if (lex.next.type == EOL) lex.consume();
  DefMap::defs out = parse_defs(lex);
  expect(END, lex);
  return out;
}
