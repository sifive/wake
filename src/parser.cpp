#include "parser.h"
#include "expr.h"
#include "symbol.h"
#include "value.h"
#include "datatype.h"
#include <iostream>
#include <list>

//#define TRACE(x) do { fprintf(stderr, "%s\n", x); } while (0)
#define TRACE(x) do { } while (0)

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

static bool expectValue(const char *type, Lexer &lex) {
  if (expect(LITERAL, lex)) {
    if (lex.next.expr->type == Literal::type) {
      Literal *lit = reinterpret_cast<Literal*>(lex.next.expr.get());
      if (lit->value->type == type) {
        return true;
      } else {
        std::cerr << "Was expecting a "
          << type << ", but got a "
          << lit->type << " at "
          << lex.next.location << std::endl;
        lex.fail = true;
        return false;
      }
    } else {
      std::cerr << "Was expecting a "
        << type << ", but got an interpolated string at "
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

static void precedence_error(Lexer &lex) {
  std::cerr << "Lower precedence unary operator "
    << lex.text() << " must use ()s at "
    << lex.next.location << std::endl;
  lex.fail = true;
}

static Expr *parse_unary(int p, Lexer &lex) {
  TRACE("UNARY");
  switch (lex.next.type) {
    // Unary operators
    case OPERATOR: {
      Location location = lex.next.location;
      op_type op = op_precedence(lex.text().c_str());
      if (op.p < p) precedence_error(lex);
      auto opp = new VarRef(lex.next.location, "unary " + lex.text());
      lex.consume();
      auto rhs = parse_binary(op.p + op.l, lex);
      location.end = rhs->location.end;
      return new App(location, opp, rhs);
    }
    case MEMOIZE: {
      Location location = lex.next.location;
      op_type op = op_precedence("m");
      if (op.p < p) precedence_error(lex);
      lex.consume();
      auto rhs = parse_binary(op.p + op.l, lex);
      location.end = rhs->location.end;
      return new Memoize(location, rhs);
    }
    case LAMBDA: {
      Location location = lex.next.location;
      op_type op = op_precedence("\\");
      if (op.p < p) precedence_error(lex);
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
      Expr *out = lex.next.expr.release();
      lex.consume();
      return out;
    }
    case PRIM: {
      std::string name;
      Location location = lex.next.location;
      op_type op = op_precedence("p");
      if (op.p < p) precedence_error(lex);
      lex.consume();
      if (expectValue(String::type, lex)) {
        Literal *lit = reinterpret_cast<Literal*>(lex.next.expr.get());
        name = reinterpret_cast<String*>(lit->value.get())->value;
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
      op_type op = op_precedence("s");
      if (op.p < p) precedence_error(lex);
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
        op_type op = op_precedence(lex.text().c_str());
        if (op.p < p) return lhs;
        auto opp = new VarRef(lex.next.location, "binary " + lex.text());
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
        op_type op = op_precedence("a"); // application
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
    return new App(l, new App(l, new App(l,
      new VarRef(l, "Boolean"),
      new Lambda(l, "_", thenE)),
      new Lambda(l, "_", elseE)),
      condE);
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

static void publish_def(DefMap::defs &publish, const std::string &name, Expr *def) {
  DefMap::defs::iterator i;
  if ((i = publish.find(name)) == publish.end()) {
    // A reference to _tail which we close with a lambda at the top of the chain
    i = publish.insert(std::make_pair(name, std::unique_ptr<Expr>(new VarRef(def->location, "_tail")))).first;
  }
  // Make a tuple
  i->second = std::unique_ptr<Expr>(
    new App(def->location,
      new App(def->location,
        new VarRef(def->location, "binary ,"),
        def),
      i->second.release()));
}

static void bind_global(const std::string &name, Top *top, Lexer &lex) {
  if (!top) return;

  auto it = top->globals.find(name);
  if (it != top->globals.end()) {
    std::cerr << "Duplicate global "
      << name << " at "
      << top->defmaps.back()->map[name]->location << " and "
      << top->defmaps[it->second]->map[name]->location << std::endl;
    lex.fail = true;
  } else {
    top->globals[name] = top->defmaps.size()-1;
  }
}

static void publish_seal(DefMap::defs &publish) {
  for (auto &i : publish) {
    i.second = std::unique_ptr<Expr>(new Lambda(i.second->location, "_tail", i.second.release()));
  }
}

static AST parse_ast(int p, Lexer &lex);

static AST parse_unary_ast(int p, Lexer &lex) {
  TRACE("UNARY_AST");
  switch (lex.next.type) {
    // Unary operators
    case OPERATOR: {
      Location location = lex.next.location;
      op_type op = op_precedence(lex.text().c_str());
      if (op.p < p) precedence_error(lex);
      std::string name = "unary " + lex.text();
      lex.consume();
      AST rhs = parse_ast(op.p + op.l, lex);
      location.end = rhs.location.end;
      std::vector<AST> args;
      args.emplace_back(std::move(rhs));
      return AST(location, std::move(name), std::move(args));
    }
    // Terminals
    case ID: {
      AST out(lex.next.location, lex.text());
      lex.consume();
      return out;
    }
    case POPEN: {
      Location location = lex.next.location;
      lex.consume();
      AST out = parse_ast(0, lex);
      location.end = lex.next.location.end;
      if (expect(PCLOSE, lex)) lex.consume();
      out.location = location;
      return out;
    }
    default: {
      std::cerr << "Was expecting an (OPERATOR/ID/POPEN), got a "
        << symbolTable[lex.next.type] << " at "
        << lex.next.location << std::endl;
      lex.fail = true;
      return AST(lex.next.location);
    }
  }
}

static AST parse_ast(int p, Lexer &lex) {
  TRACE("AST");
  AST lhs = parse_unary_ast(p, lex);
  for (;;) {
    switch (lex.next.type) {
      case OPERATOR: {
        op_type op = op_precedence(lex.text().c_str());
        if (op.p < p) return lhs;
        std::string name = "binary " + lex.text();
        lex.consume();
        auto rhs = parse_ast(op.p + op.l, lex);
        Location loc = lhs.location;
        loc.end = rhs.location.end;
        std::vector<AST> args;
        args.emplace_back(std::move(lhs));
        args.emplace_back(std::move(rhs));
        lhs = AST(loc, std::move(name), std::move(args));
        break;
      }
      case ID:
      case POPEN: {
        op_type op = op_precedence("a"); // application
        if (op.p < p) return lhs;
        AST rhs = parse_ast(op.p + op.l, lex);
        Location location = lhs.location;
        location.end = rhs.location.end;
        if (lhs.name.find(' ') != std::string::npos) {
          std::cerr << "Cannot supply additional constructor arguments to " << lhs.name
            << " at " << location << std::endl;
          lex.fail = true;
        } else if (lhs.args.empty() && Lexer::isLower(lhs.name.c_str())) {
          std::cerr << "Lower-case identifier " << lhs.name
            << " cannot be used as a constructor at " << location
            << std::endl;
          lex.fail = true;
        }
        lhs.args.emplace_back(std::move(rhs));
        break;
      }
      default: {
        return lhs;
      }
    }
  }
}

Sum *Boolean;
Sum *List;
Sum *Pair;

static void check_cons_name(const AST &ast, Lexer &lex) {
  bool isOp = ast.name.find(' ') != std::string::npos;
  if (!isOp && Lexer::isLower(ast.name.c_str())) {
    std::cerr << "Constructor name must be upper-case or operator, not "
      << ast.name << " at "
      << ast.location << std::endl;
    lex.fail = true;
  }
}

static void parse_data(Lexer &lex, DefMap::defs &map, Top *top) {
  lex.consume();

  AST def = parse_ast(0, lex);
  if (!def) return;

  bool isOp = def.name.find(' ') != std::string::npos;
  if (!isOp && Lexer::isLower(def.name.c_str())) {
    std::cerr << "Type name must be upper-case or operator, not "
      << def.name << " at "
      << def.location << std::endl;
    lex.fail = true;
  }
  for (auto &x : def.args) {
    bool isOp = x.name.find(' ') != std::string::npos;
    if (isOp || !Lexer::isLower(x.name.c_str())) {
      std::cerr << "Type argument must be lower-case, not "
        << x.name << " at "
        << x.location << std::endl;
      lex.fail = true;
    }
  }

  Sum sum(std::move(def));

  if (expect(EQUALS, lex)) lex.consume();

  if (lex.next.type == EOL) {
    lex.consume();
    if (expect(INDENT, lex)) lex.consume();

    bool repeat = true;
    while (repeat) {
      AST cons = parse_ast(0, lex);
      if (cons) {
        check_cons_name(cons, lex);
        sum.addConstructor(std::move(cons));
      }
      switch (lex.next.type) {
        case DEDENT:
          repeat = false;
          lex.consume();
          expect(EOL, lex);
        case EOL: {
          lex.consume();
          break;
        }
        default: {
          std::cerr << "Unexpected end of data definition at " << lex.next.location << std::endl;
          lex.fail = true;
          repeat = false;
          break;
        }
      }
    }
  } else {
    AST cons = parse_ast(0, lex);
    if (cons) {
      check_cons_name(cons, lex);
      sum.addConstructor(std::move(cons));
    }
    expect(EOL, lex);
    lex.consume();
  }

  std::string name = sum.name;
  Location location = sum.location;
  Destruct *destruct = new Destruct(location, std::move(sum));
  Sum *sump = &destruct->sum;
  Expr *destructfn = new Lambda(sump->location, "_", destruct);

  for (auto &c : sump->members) {
    destructfn = new Lambda(sump->location, "_", destructfn);
    Expr *construct = new Construct(c.ast.location, sump, &c);
    for (size_t i = 0; i < c.ast.args.size(); ++i)
      construct = new Lambda(c.ast.location, "_", construct);

    bind_def(lex, map, c.ast.name, construct);
    bind_global(c.ast.name, top, lex);
  }

  bind_def(lex, map, name, destructfn);
  bind_global(name, top, lex);

  if (name == "Integer" || name == "String" || name == "RegExp" ||
      name == "CatStream" || name == "Exception" || name == FN ||
      name == "JobResult") {
    std::cerr << "Constuctor " << name
      << " is reserved at " << sump->location << "." << std::endl;
    lex.fail = true;
  }

  if (top && name == "Boolean") {
    if (sump->members.size() != 2 ||
        sump->members[0].ast.args.size() != 0 ||
        sump->members[1].ast.args.size() != 0) {
      std::cerr << "Special constructor Boolean not defined correctly at "
        << sump->location << "." << std::endl;
      lex.fail = true;
    }
    Boolean = sump;
  }

  if (top && name == "List") {
    if (sump->members.size() != 2 ||
        sump->members[0].ast.args.size() != 0 ||
        sump->members[1].ast.args.size() != 2) {
      std::cerr << "Special constructor List not defined correctly at "
        << sump->location << "." << std::endl;
      lex.fail = true;
    }
    List = sump;
  }

  if (top && name == "Pair") {
    if (sump->members.size() != 1 ||
        sump->members[0].ast.args.size() != 2) {
      std::cerr << "Special constructor Pair not defined correctly at "
        << sump->location << "." << std::endl;
      lex.fail = true;
    }
    Pair = sump;
  }
}

static void parse_decl(DefMap::defs &map, Lexer &lex, Top *top) {
  switch (lex.next.type) {
    default:
       std::cerr << "Missing DEF after GLOBAL at " << lex.next.location << std::endl;
       lex.fail = true;
    case DEF:
    case VAL: {
      std::string name;
      auto def = parse_def(lex, name);
      bind_def(lex, map, name, def);
      bind_global(name, top, lex);
      break;
    }
    case DATA: {
      parse_data(lex, map, top);
      break;
    }
  }
}

Expr *parse_block(Lexer &lex) {
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
        case VAL:
        case DEF: {
          parse_decl(map, lex, 0);
          break;
        }
        case PUBLISH: {
          std::string name;
          auto def = parse_def(lex, name);
          publish_def(publish, name, def);
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
  top.defmaps.emplace_back(new DefMap(lex.next.location));
  DefMap &defmap = *top.defmaps.back();

  bool repeat = true;
  while (repeat) {
    switch (lex.next.type) {
      case GLOBAL: {
        lex.consume();
        parse_decl(defmap.map, lex, &top);
        break;
      }
      case DATA:
      case VAL:
      case DEF: {
        parse_decl(defmap.map, lex, 0);
        break;
      }
      case PUBLISH: {
        std::string name;
        auto def = parse_def(lex, name);
        publish_def(defmap.publish, name, def);
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
