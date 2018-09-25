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

static op_type precedence(const std::string &str) {
  char c = str[0];
  switch (c) {
  case '.':
    return op_type(13, 1);
  case 'a': // Application rules run between . and $
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
  case 'm': // MEMOIZE
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
      << lex.next.location << std::endl;
    lex.fail = true;
    return false;
  }
  return true;
}

static std::pair<std::string, Location> get_arg_loc(Lexer &lex) {
  if (lex.next.type != ID) {
    std::cerr << "Was expecting an ID argument, but got a "
      << symbolTable[lex.next.type] << " at "
      << lex.next.location << std::endl;
    lex.fail = true;
  }

  auto out = std::make_pair(lex.text(), lex.next.location);
  lex.consume();
  return out;
}

bool expectValue(const char *type, Lexer &lex) {
  if (expect(LITERAL, lex)) {
    if (lex.next.value->type == type) {
      return true;
    } else {
      std::cerr << "Was expecting a "
        << type << ", but got a "
        << lex.next.value->type << " at "
        << lex.next.location << std::endl;
      lex.fail = true;
      return false;
    }
  } else {
    return false;
  }
}

static Expr *parse_unary(int p, Lexer &lex);
static Expr *parse_binary(int p, Lexer &lex);
static Expr *parse_if(Lexer &lex);
static Expr *parse_def(Lexer &lex, std::string &name);
static Expr *parse_block(Lexer &lex);

static int relabel_descend(Expr *expr, int index) {
  if (!(expr->flags & FLAG_TOUCHED)) {
    expr->flags |= FLAG_TOUCHED;
    if (expr->type == VarRef::type) {
      VarRef *ref = reinterpret_cast<VarRef*>(expr);
      if (ref->name != "_") return index;
      ++index;
      ref->name += std::to_string(index);
      return index;
    } else if (expr->type == App::type) {
      App *app = reinterpret_cast<App*>(expr);
      return relabel_descend(app->val.get(), relabel_descend(app->fn.get(), index));
    } else if (expr->type == Lambda::type) {
      Lambda *lambda = reinterpret_cast<Lambda*>(expr);
      return relabel_descend(lambda->body.get(), index);
    }
  }
  // noop for DefMap, Literal, Prim
  return index;
}

static Expr *relabel_anon(Expr *out) {
  int args = relabel_descend(out, 0);
  for (int index = args; index >= 1; --index)
    out = new Lambda(out->location, "_" + std::to_string(index), out);
  return out;
}

static Expr *parse_unary(int p, Lexer &lex) {
  TRACE("UNARY");
  switch (lex.next.type) {
    // Unary operators
    case OPERATOR: {
      Location location = lex.next.location;
      op_type op = precedence(lex.text());
      if (op.p < p) {
        std::cerr << "Lower precedence unary operator "
          << lex.text() << " must use ()s at "
          << lex.next.location << std::endl;
        lex.fail = true;
      }
      auto opp = new VarRef(lex.next.location, "unary " + lex.text());
      lex.consume();
      auto rhs = parse_binary(op.p + op.l, lex);
      location.end = rhs->location.end;
      return new App(location, opp, rhs);
    }
    case MEMOIZE: {
      Location location = lex.next.location;
      op_type op = precedence(lex.text());
      if (op.p < p) {
        std::cerr << "Lower precedence unary operator "
          << lex.text() << " must use ()s at "
          << lex.next.location << std::endl;
        lex.fail = true;
      }
      lex.consume();
      auto rhs = parse_binary(op.p + op.l, lex);
      location.end = rhs->location.end;
      return new Memoize(location, rhs);
    }
    case LAMBDA: {
      Location location = lex.next.location;
      op_type op = precedence(lex.text());
      if (op.p < p) {
        std::cerr << "Lower precedence unary operator "
          << lex.text() << " must use ()s at "
          << lex.next.location << std::endl;
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
    case HERE: {
      std::string name(lex.next.location.file);
      std::string::size_type cut = name.find_last_of('/');
      if (cut == std::string::npos) name = "."; else name.resize(cut);
      Expr *out = new Literal(lex.next.location, std::make_shared<String>(std::move(name)));
      lex.consume();
      return out;
    }
    case SUBSCRIBE: {
      std::string name;
      Location location = lex.next.location;
      lex.consume();
      auto id = get_arg_loc(lex);
      location.end = id.second.end;
      return new Subscribe(location, id.first);
    }
    case POPEN: {
      Location location = lex.next.location;
      lex.consume();
      Expr *out = parse_block(lex);
      location.end = lex.next.location.end;
      if (lex.next.type == EOL) lex.consume();
      if (expect(PCLOSE, lex)) lex.consume();
      out->location = location;
      return out;
    }
    default: {
      std::cerr << "Was expecting an (OPERATOR/LAMBDA/ID/LITERAL/PRIM/POPEN), got a "
        << symbolTable[lex.next.type] << " at "
        << lex.next.location << std::endl;
      lex.fail = true;
      return new Literal(LOCATION, "bad unary");
    }
  }
}

static Expr *parse_binary(int p, Lexer &lex) {
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
      case MEMOIZE:
      case LAMBDA:
      case ID:
      case LITERAL:
      case PRIM:
      case HERE:
      case SUBSCRIBE:
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

static Expr *parse_if(Lexer &lex) {
  TRACE("IF");
  if (lex.next.type == IF) {
    Location l = lex.next.location;
    lex.consume();
    auto condE = parse_block(lex);
    if (lex.next.type == EOL) lex.consume();
    if (expect(THEN, lex)) lex.consume();
    auto thenE = parse_block(lex);
    if (lex.next.type == EOL) lex.consume();
    if (expect(ELSE, lex)) lex.consume();
    auto elseE = parse_block(lex);
    l.end = elseE->location.end;
    return new App(l, new App(l, new App(l, condE,
      new Lambda(l, "_", thenE)),
      new Lambda(l, "_", elseE)),
      new Literal(LOCATION, "if"));
  } else {
    return relabel_anon(parse_binary(0, lex));
  }
}

static Expr *parse_def(Lexer &lex, std::string &name) {
  Location def = lex.next.location;
  lex.consume();

  std::list<std::pair<std::string, Location> > args;
  while (lex.next.type == ID) args.push_back(get_arg_loc(lex));

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
      std::cerr << "Operator def is neither unary nor binary at " << def << std::endl;
      lex.fail = true;
    }
  } else {
    if (args.empty()) {
      name = "broken";
      std::cerr << "def has no name at " << def << std::endl;
      lex.fail = true;
    } else {
      name = args.front().first;
      args.pop_front();
    }
  }

  expect(EQUALS, lex);
  lex.consume();

  Expr *body = parse_block(lex);
  if (expect(EOL, lex)) lex.consume();

  // add the arguments
  for (auto i = args.rbegin(); i != args.rend(); ++i) {
    Location location = body->location;
    location.start = i->second.start;
    body = new Lambda(location, i->first, body);
  }

  return body;
}

static void bind_def(Lexer &lex, DefMap::defs &map, const std::string &name, Expr *def) {
  auto i = map.find(name);
  if (i != map.end()) {
    std::cerr << "Duplicate def "
      << name << " at "
      << i->second->location << " and "
      << def->location << std::endl;
    lex.fail = true;
  }
  map[name] = std::unique_ptr<Expr>(def);
}

static void publish_def(Lexer &lex, DefMap::defs &publish, const std::string &name, Expr *def) {
  DefMap::defs::iterator i;
  if ((i = publish.find(name)) == publish.end()) {
    // A reference to _tail which we close with a lambda at the top of the chain
    i = publish.insert(std::make_pair(name, std::unique_ptr<Expr>(new VarRef(def->location, "_tail")))).first;
  }
  // Make a tuple
  i->second = std::unique_ptr<Expr>(new Lambda(def->location, "_pub",
    new App(def->location,
      new App(def->location,
        new VarRef(def->location, "_pub"),
        def),
      i->second.release())));
}

static void publish_seal(DefMap::defs &publish) {
  for (auto &i : publish) {
    i.second = std::unique_ptr<Expr>(new Lambda(i.second->location, "_tail", i.second.release()));
  }
}

static Expr *parse_block(Lexer &lex) {
  TRACE("BLOCK");
  Expr *out;

  if (lex.next.type == EOL) {
    lex.consume();
    if (expect(INDENT, lex)) lex.consume();

    Location location = lex.next.location;
    DefMap::defs map;
    DefMap::defs publish;

    bool repeat = true;
    while (repeat) {
      switch (lex.next.type) {
        case DEF: {
          std::string name;
          auto def = parse_def(lex, name);
          bind_def(lex, map, name, def);
          break;
        }
        case PUBLISH: {
          std::string name;
          auto def = parse_def(lex, name);
          publish_def(lex, publish, name, def);
          break;
        }
        default: {
          repeat = false;
          break;
        }
      }
    }

    publish_seal(publish);
    auto body = parse_if(lex);
    location.end = body->location.end;
    out = (publish.empty() && map.empty()) ? body : new DefMap(location, std::move(map), std::move(publish), body);

    if (expect(DEDENT, lex)) lex.consume();
    return out;
  } else {
    out = parse_if(lex);
  }

  return out;
}

void parse_top(Top &top, Lexer &lex) {
  TRACE("TOP");
  if (lex.next.type == EOL) lex.consume();
  top.defmaps.push_back(DefMap(lex.next.location));
  DefMap &defmap = top.defmaps.back();

  bool repeat = true;
  while (repeat) {
    switch (lex.next.type) {
      case GLOBAL: {
        std::string name;
        auto def = parse_def(lex, name);
        bind_def(lex, defmap.map, name, def);
        if (top.globals.find(name) != top.globals.end()) {
          std::cerr << "Duplicate global "
            << name << " at "
            << def->location << " and "
            << top.defmaps[top.globals[name]].map[name]->location << std::endl;
          lex.fail = true;
        } else {
          top.globals[name] = top.defmaps.size()-1;
        }
        break;
      }
      case DEF: {
        std::string name;
        auto def = parse_def(lex, name);
        bind_def(lex, defmap.map, name, def);
        break;
      }
      case PUBLISH: {
        std::string name;
        auto def = parse_def(lex, name);
        publish_def(lex, defmap.publish, name, def);
        break;
      }
      default: {
        repeat = false;
        break;
      }
    }
  }

  publish_seal(defmap.publish);
  defmap.location.end = lex.next.location.start;
  expect(END, lex);
}

Expr *parse_command(Lexer &lex) {
  TRACE("COMMAND");
  if (lex.next.type == EOL) lex.consume();
  auto out = parse_block(lex);
  expect(END, lex);
  return out;
}
