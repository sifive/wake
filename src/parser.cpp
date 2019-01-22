#include "parser.h"
#include "expr.h"
#include "symbol.h"
#include "value.h"
#include "datatype.h"
#include "location.h"
#include <iostream>
#include <list>
#include <set>

//#define TRACE(x) do { fprintf(stderr, "%s\n", x); } while (0)
#define TRACE(x) do { } while (0)

bool expect(SymbolType type, Symbol &sym) {
  if (sym.type != type) {
    std::cerr << "Was expecting a "
      << symbolTable[type] << ", but got a "
      << symbolTable[sym.type] << " at "
      << sym.location << std::endl;
    return false;
  }
  return true;
}

bool expect(SymbolType type, Lexer &lex) {
  bool ok = expect(type, lex.next);
  lex.fail |= !ok;
  return ok;
}

bool expect(SymbolType type, JLexer &lex) {
  bool ok = expect(type, lex.next);
  lex.fail |= !ok;
  return ok;
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

static AST parse_ast(int p, Lexer &lex, bool makefirst = false, bool firstok = false);
static Expr *parse_binary(int p, Lexer &lex, bool multiline);
static Expr *parse_def(Lexer &lex, std::string &name);
static Expr *parse_block(Lexer &lex, bool multiline);

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
    } else if (expr->type == Match::type) {
      Match *match = reinterpret_cast<Match*>(expr);
      for (auto &v : match->args)
        index = relabel_descend(v.get(), index);
      return index;
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

static Expr *parse_match(int p, Lexer &lex) {
  Location location = lex.next.location;
  op_type op = op_precedence("m");
  if (op.p < p) precedence_error(lex);
  lex.consume();

  Match *out = new Match(location);
  bool repeat;

  repeat = true;
  while (repeat) {
    auto rhs = parse_binary(op.p + op.l, lex, false);
    out->args.emplace_back(rhs);

    switch (lex.next.type) {
      case OPERATOR:
      case MATCH:
      case MEMOIZE:
      case LAMBDA:
      case ID:
      case LITERAL:
      case PRIM:
      case HERE:
      case SUBSCRIBE:
      case POPEN:
        break;
      case INDENT:
        lex.consume();
        repeat = false;
        break;
      default:
        std::cerr << "Unexpected end of match definition at " << lex.next.location << std::endl;
        lex.fail = true;
        repeat = false;
        break;
    }
  }

  if (expect(EOL, lex)) lex.consume();

  // Process the patterns
  bool multiarg = out->args.size() > 1;
  repeat = true;
  while (repeat) {
    AST ast = parse_ast(multiarg?APP_PRECEDENCE:0, lex, multiarg, multiarg);
    if (expect(EQUALS, lex)) lex.consume();
    Expr *expr = parse_block(lex, false);
    out->patterns.emplace_back(std::move(ast), expr);

    switch (lex.next.type) {
      case DEDENT:
        repeat = false;
        lex.consume();
        break;
      case EOL:
        lex.consume();
        break;
      default:
        std::cerr << "Unexpected end of match definition at " << lex.next.location << std::endl;
        lex.fail = true;
        repeat = false;
        break;
    }
  }

  out->location.end = out->patterns.back().expr->location.end;
  return out;
}

static Expr *parse_unary(int p, Lexer &lex, bool multiline) {
  TRACE("UNARY");
  switch (lex.next.type) {
    // Unary operators
    case OPERATOR: {
      Location location = lex.next.location;
      op_type op = op_precedence(lex.text().c_str());
      if (op.p < p) precedence_error(lex);
      auto opp = new VarRef(lex.next.location, "unary " + lex.text());
      lex.consume();
      auto rhs = parse_binary(op.p + op.l, lex, multiline);
      location.end = rhs->location.end;
      return new App(location, opp, rhs);
    }
    case MATCH: {
      return parse_match(p, lex);
    }
    case MEMOIZE: {
      Location location = lex.next.location;
      op_type op = op_precedence("m");
      if (op.p < p) precedence_error(lex);
      lex.consume();
      long skip = 0;
      if (expectValue(Integer::type, lex)) {
        Literal *lit = reinterpret_cast<Literal*>(lex.next.expr.get());
        mpz_t &x = reinterpret_cast<Integer*>(lit->value.get())->value;
        if (mpz_fits_slong_p(x)) {
          skip = mpz_get_si(x);
        } else {
          std::cerr << "Integer argument to memoize too large at "
             << location << std::endl;
          lex.fail = true;
        }
        location.end = lex.next.location.end;
        lex.consume();
      }
      auto rhs = parse_binary(op.p + op.l, lex, multiline);
      location.end = rhs->location.end;
      return new Memoize(location, skip, rhs);
    }
    case LAMBDA: {
      Location location = lex.next.location;
      op_type op = op_precedence("\\");
      if (op.p < p) precedence_error(lex);
      lex.consume();
      AST ast = parse_ast(APP_PRECEDENCE+1, lex);
      auto rhs = parse_binary(op.p + op.l, lex, multiline);
      location.end = rhs->location.end;
      if (Lexer::isUpper(ast.name.c_str()) || Lexer::isOperator(ast.name.c_str())) {
        Match *match = new Match(location);
        match->patterns.emplace_back(std::move(ast), rhs);
        match->args.emplace_back(new VarRef(LOCATION, "_xx"));
        return new Lambda(location, "_xx", match);
      } else {
        return new Lambda(location, ast.name, rhs);
      }
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
      Expr *out = parse_block(lex, multiline);
      location.end = lex.next.location.end;
      if (lex.next.type == EOL) lex.consume();
      if (expect(PCLOSE, lex)) lex.consume();
      out->location = location;
      return out;
    }
    case IF: {
      Location l = lex.next.location;
      op_type op = op_precedence("i");
      if (op.p < p) precedence_error(lex);
      lex.consume();
      auto condE = parse_block(lex, multiline);
      if (expect(THEN, lex)) lex.consume();
      if (lex.next.type == EOL && multiline) lex.consume();
      auto thenE = parse_block(lex, multiline);
      if (lex.next.type == EOL && multiline) lex.consume();
      if (expect(ELSE, lex)) lex.consume();
      auto elseE = parse_block(lex, multiline);
      l.end = elseE->location.end;
      return new App(l, new App(l, new App(l,
        new VarRef(l, "destruct Boolean"),
        new Lambda(l, "_", thenE)),
        new Lambda(l, "_", elseE)),
        condE);
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

static Expr *parse_binary(int p, Lexer &lex, bool multiline) {
  TRACE("BINARY");
  if (lex.next.type == EOL && multiline) lex.consume();
  auto lhs = parse_unary(p, lex, multiline);
  for (;;) {
    switch (lex.next.type) {
      case OPERATOR: {
        op_type op = op_precedence(lex.text().c_str());
        if (op.p < p) return lhs;
        auto opp = new VarRef(lex.next.location, "binary " + lex.text());
        lex.consume();
        auto rhs = parse_binary(op.p + op.l, lex, multiline);
        Location app1_loc = lhs->location;
        Location app2_loc = lhs->location;
        app1_loc.end = opp->location.end;
        app2_loc.end = rhs->location.end;
        lhs = new App(app2_loc, new App(app1_loc, opp, lhs), rhs);
        break;
      }
      case MATCH:
      case MEMOIZE:
      case LAMBDA:
      case ID:
      case LITERAL:
      case PRIM:
      case HERE:
      case SUBSCRIBE:
      case IF:
      case POPEN: {
        op_type op = op_precedence("a"); // application
        if (op.p < p) return lhs;
        auto rhs = parse_binary(op.p + op.l, lex, multiline);
        Location location = lhs->location;
        location.end = rhs->location.end;
        lhs = new App(location, lhs, rhs);
        break;
      }
      case EOL: {
        if (multiline) {
          lex.consume();
          break;
        }
      }
      default: {
        return lhs;
      }
    }
  }
}

static Expr *parse_def(Lexer &lex, std::string &name) {
  lex.consume();

  AST ast = parse_ast(0, lex, false, true);
  name = std::move(ast.name);
  ast.name.clear();

  if (Lexer::isOperator(name.c_str()) && !ast.args.empty()) {
    // Unfortunately, parse_ast will allow: (a b) * (C d); reject it
    AST &arg1 = ast.args.front();
    if (!arg1.args.empty()) {
      if (arg1.name == "_") {
        std::cerr << "Wildcard _"
          << " cannot be used as a constructor at " << arg1.location
          << std::endl;
        lex.fail = true;
      }
      if (Lexer::isLower(arg1.name.c_str())) {
        std::cerr << "Lower-case identifier " << arg1.name
          << " cannot be used as a constructor at " << arg1.location
          << std::endl;
        lex.fail = true;
      }
    }
  }

  if (Lexer::isUpper(name.c_str())) {
    std::cerr << "Upper-case identifier " << name
      << " cannot be used as a function name at " << ast.location
      << std::endl;
    lex.fail = true;
  }

  expect(EQUALS, lex);
  lex.consume();

  Expr *body = parse_block(lex, false);
  if (expect(EOL, lex)) lex.consume();

  // do we need a pattern match? lower / wildcard are ok
  bool pattern = false;
  for (auto &x : ast.args) {
    pattern |= Lexer::isOperator(x.name.c_str()) || Lexer::isUpper(x.name.c_str());
  }

  if (!pattern) {
    // no pattern; simple lambdas for the arguments
    for (auto i = ast.args.rbegin(); i != ast.args.rend(); ++i) {
      Location location = body->location;
      location.start = i->location.start;
      body = new Lambda(location, i->name, body);
    }
  } else {
    // bind the arguments to anonymous lambdas and push the whole thing into a pattern
    int args = ast.args.size();
    Match *match = new Match(body->location);
    if (args > 1) {
      match->patterns.emplace_back(std::move(ast), body);
    } else {
      match->patterns.emplace_back(std::move(ast.args.front()), body);
    }
    body = match;
    for (int i = 0; i < args; ++i) {
      body = new Lambda(body->location, "_" + std::to_string(args-1-i), body);
      match->args.emplace_back(new VarRef(LOCATION, "_" + std::to_string(i)));
    }
  }

  return body;
}

static void bind_def(bool &fail, DefMap::defs &map, std::string name, Expr *def) {
  if (name == "_")
    name += std::to_string(map.size());

  auto i = map.find(name);
  if (i != map.end()) {
    std::cerr << "Duplicate def "
      << name << " at "
      << i->second->location << " and "
      << def->location << std::endl;
    fail = true;
  }
  map[name] = std::unique_ptr<Expr>(def);
}

static void bind_def(Lexer &lex, DefMap::defs &map, std::string name, Expr *def) {
  return bind_def(lex.fail, map, name, def);
}

static void bind_def(JLexer &lex, DefMap::defs &map, std::string name, Expr *def) {
  return bind_def(lex.fail, map, name, def);
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

static void bind_global(const std::string &name, Top *top, bool &fail) {
  if (!top || name == "_") return;

  auto it = top->globals.find(name);
  if (it != top->globals.end()) {
    std::cerr << "Duplicate global "
      << name << " at "
      << top->defmaps.back()->map[name]->location << " and "
      << top->defmaps[it->second]->map[name]->location << std::endl;
    fail = true;
  } else {
    top->globals[name] = top->defmaps.size()-1;
  }
}

static void bind_global(const std::string &name, Top *top, Lexer &lex) {
  return bind_global(name, top, lex.fail);
}

static void bind_global(const std::string &name, Top *top, JLexer &lex) {
  return bind_global(name, top, lex.fail);
}

static void publish_seal(DefMap::defs &publish) {
  for (auto &i : publish) {
    i.second = std::unique_ptr<Expr>(new Lambda(i.second->location, "_tail", i.second.release()));
  }
}

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

static AST parse_ast(int p, Lexer &lex, bool makefirst, bool firstok) {
  TRACE("AST");
  AST lhs = makefirst ? AST(lex.next.location) : parse_unary_ast(p, lex);
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
        if (Lexer::isOperator(lhs.name.c_str())) {
          std::cerr << "Cannot supply additional constructor arguments to " << lhs.name
            << " at " << location << std::endl;
          lex.fail = true;
        } else if (!firstok && lhs.args.empty() && lhs.name == "_") {
          std::cerr << "Wildcard _"
            << " cannot be used as a constructor at " << location
            << std::endl;
          lex.fail = true;
        } else if (!firstok && lhs.args.empty() && Lexer::isLower(lhs.name.c_str())) {
          std::cerr << "Lower-case identifier " << lhs.name
            << " cannot be used as a constructor at " << location
            << std::endl;
          lex.fail = true;
        }
        firstok = false;
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
Sum *Order;
Sum *List;
Sum *Pair;
Sum *Unit;

static void check_cons_name(const AST &ast, Lexer &lex) {
  if (ast.name == "_" || Lexer::isLower(ast.name.c_str())) {
    std::cerr << "Constructor name must be upper-case or operator, not "
      << ast.name << " at "
      << ast.location << std::endl;
    lex.fail = true;
  }
}

static AST parse_type_def(Lexer &lex) {
  lex.consume();

  AST def = parse_ast(0, lex);
  if (!def) return def;

  if (def.name == "_" || Lexer::isLower(def.name.c_str())) {
    std::cerr << "Type name must be upper-case or operator, not "
      << def.name << " at "
      << def.location << std::endl;
    lex.fail = true;
  }

  std::set<std::string> args;
  for (auto &x : def.args) {
    if (!Lexer::isLower(x.name.c_str())) {
      std::cerr << "Type argument must be lower-case, not "
        << x.name << " at "
        << x.location << std::endl;
      lex.fail = true;
    }
    if (!args.insert(x.name).second) {
      std::cerr << "Type argument "
        << x.name << " occurs more than once at "
        << x.location << std::endl;
      lex.fail = true;
    }
  }

  if (expect(EQUALS, lex)) lex.consume();

  return def;
}

static void check_special(Lexer &lex, const std::string &name, Sum *sump) {
  if (name == "Integer" || name == "String" || name == "RegExp" ||
      name == "CatStream" || name == "Exception" || name == FN ||
      name == "JobResult" || name == "Array") {
    std::cerr << "Constuctor " << name
      << " is reserved at " << sump->location << "." << std::endl;
    lex.fail = true;
  }

  if (name == "Boolean") {
    if (sump->members.size() != 2 ||
        sump->members[0].ast.args.size() != 0 ||
        sump->members[1].ast.args.size() != 0) {
      std::cerr << "Special constructor Boolean not defined correctly at "
        << sump->location << "." << std::endl;
      lex.fail = true;
    }
    Boolean = sump;
  }

  if (name == "Order") {
    if (sump->members.size() != 3 ||
        sump->members[0].ast.args.size() != 0 ||
        sump->members[1].ast.args.size() != 0 ||
        sump->members[2].ast.args.size() != 0) {
      std::cerr << "Special constructor Order not defined correctly at "
        << sump->location << "." << std::endl;
      lex.fail = true;
    }
    Order = sump;
  }

  if (name == "List") {
    if (sump->members.size() != 2 ||
        sump->members[0].ast.args.size() != 0 ||
        sump->members[1].ast.args.size() != 2) {
      std::cerr << "Special constructor List not defined correctly at "
        << sump->location << "." << std::endl;
      lex.fail = true;
    }
    List = sump;
  }

  if (name == "Pair") {
    if (sump->members.size() != 1 ||
        sump->members[0].ast.args.size() != 2) {
      std::cerr << "Special constructor Pair not defined correctly at "
        << sump->location << "." << std::endl;
      lex.fail = true;
    }
    Pair = sump;
  }

  if (name == "Unit") {
    if (sump->members.size() != 1 ||
        sump->members[0].ast.args.size() != 0) {
      std::cerr << "Special constructor Unit not defined correctly at "
        << sump->location << "." << std::endl;
      lex.fail = true;
    }
    Unit = sump;
  }
}

static void parse_tuple(Lexer &lex, DefMap::defs &map, Top *top, bool global) {
  AST def = parse_type_def(lex);
  if (!def) return;

  std::string name = def.name;
  std::string tname = "destruct " + name;
  Sum sum(std::move(def));
  AST tuple(sum.location, std::string(sum.name));
  std::vector<std::pair<std::string, bool> > members;

  if (!expect(INDENT, lex)) return;
  lex.consume();
  expect(EOL, lex);
  lex.consume();

  bool repeat = true;
  while (repeat) {
    bool global = lex.next.type == GLOBAL;
    if (global) lex.consume();

    std::string mname = get_arg_loc(lex).first;

    AST member = parse_ast(0, lex);
    if (member) {
      tuple.args.push_back(member);
      members.emplace_back(mname, global);
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
        std::cerr << "Unexpected end of tuple definition at " << lex.next.location << std::endl;
        lex.fail = true;
        repeat = false;
        break;
      }
    }
  }

  sum.addConstructor(std::move(tuple));

  Location location = sum.location;
  Destruct *destruct = new Destruct(location, std::move(sum));
  Sum *sump = &destruct->sum;
  Expr *destructfn =
    new Lambda(sump->location, "_",
    new Lambda(sump->location, "_",
    destruct));

  Constructor &c = sump->members.back();
  Expr *construct = new Construct(c.ast.location, sump, &c);
  for (size_t i = 0; i < c.ast.args.size(); ++i)
    construct = new Lambda(c.ast.location, "_", construct);

  bind_def(lex, map, c.ast.name, construct);
  if (global) bind_global(c.ast.name, top, lex);

  bind_def(lex, map, tname, destructfn);
  if (global) bind_global(tname, top, lex);

  check_special(lex, name, sump);

  // Create get/set/edit helper methods
  int outer = 0;
  for (auto &m : members) {
    std::string &mname = m.first;
    bool global = m.second;

    // Implement get methods
    Expr *getifn = new VarRef(sump->location, "_" + std::to_string(outer+1));
    for (int inner = (int)members.size(); inner >= 0; --inner)
      getifn = new Lambda(sump->location, "_" + std::to_string(inner), getifn);

    std::string get = "get" + name + mname;
    Expr *getfn =
      new Lambda(sump->location, "_x",
        new App(sump->location,
          new App(sump->location,
            new VarRef(sump->location, tname),
            getifn),
          new VarRef(sump->location, "_x")));

    bind_def(lex, map, get, getfn);
    if (global) bind_global(get, top, lex);

    // Implement edit methods
    Expr *editifn = new VarRef(sump->location, name);
    for (int inner = 0; inner < (int)members.size(); ++inner)
      editifn = new App(sump->location, editifn,
        (inner == outer)
        ? reinterpret_cast<Expr*>(new App(sump->location,
           new VarRef(sump->location, "_f"),
           new VarRef(sump->location, "_" + std::to_string(inner+1))))
        : reinterpret_cast<Expr*>(new VarRef(sump->location, "_" + std::to_string(inner+1))));
    for (int inner = (int)members.size(); inner >= 0; --inner)
      editifn = new Lambda(sump->location, "_" + std::to_string(inner), editifn);

    std::string edit = "edit" + name + mname;
    Expr *editfn =
      new Lambda(sump->location, "_f",
        new Lambda(sump->location, "_x",
          new App(sump->location,
            new App(sump->location,
              new VarRef(sump->location, tname),
              editifn),
            new VarRef(sump->location, "_x"))));

    bind_def(lex, map, edit, editfn);
    if (global) bind_global(edit, top, lex);

    // Implement set methods
    Expr *setifn = new VarRef(sump->location, name);
    for (int inner = 0; inner < (int)members.size(); ++inner)
      setifn = new App(sump->location, setifn,
        (inner == outer)
        ? reinterpret_cast<Expr*>(new VarRef(sump->location,"_v"))
        : reinterpret_cast<Expr*>(new VarRef(sump->location, "_" + std::to_string(inner+1))));
    for (int inner = (int)members.size(); inner >= 0; --inner)
      setifn = new Lambda(sump->location, "_" + std::to_string(inner), setifn);

    std::string set = "set" + name + mname;
    Expr *setfn =
      new Lambda(sump->location, "_v",
        new Lambda(sump->location, "_x",
          new App(sump->location,
            new App(sump->location,
              new VarRef(sump->location, tname),
              setifn),
            new VarRef(sump->location, "_x"))));

    bind_def(lex, map, set, setfn);
    if (global) bind_global(set, top, lex);

    ++outer;
  }
}

static void parse_data(Lexer &lex, DefMap::defs &map, Top *top, bool global) {
  AST def = parse_type_def(lex);
  if (!def) return;

  Sum sum(std::move(def));

  if (lex.next.type == INDENT) {
    lex.consume();
    if (expect(EOL, lex)) lex.consume();

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
    if (global) bind_global(c.ast.name, top, lex);
  }

  std::string tname = "destruct " + name;
  bind_def(lex, map, tname, destructfn);
  if (global) bind_global(tname, top, lex);
  check_special(lex, name, sump);
}

static void parse_decl(DefMap::defs &map, Lexer &lex, Top *top, bool global) {
  switch (lex.next.type) {
    default:
       std::cerr << "Missing DEF after GLOBAL at " << lex.next.location << std::endl;
       lex.fail = true;
    case DEF: {
      std::string name;
      auto def = parse_def(lex, name);
      bind_def(lex, map, name, def);
      if (global) bind_global(name, top, lex);
      break;
    }
    case TUPLE: {
      parse_tuple(lex, map, top, global);
      break;
    }
    case DATA: {
      parse_data(lex, map, top, global);
      break;
    }
  }
}

static Expr *parse_block(Lexer &lex, bool multiline) {
  TRACE("BLOCK");
  Expr *out;

  if (lex.next.type == INDENT) {
    lex.consume();
    if (expect(EOL, lex)) lex.consume();

    Location location = lex.next.location;
    DefMap::defs map;
    DefMap::defs publish;

    bool repeat = true;
    while (repeat) {
      switch (lex.next.type) {
        case DEF: {
          parse_decl(map, lex, 0, false);
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
    auto body = relabel_anon(parse_binary(0, lex, true));
    location.end = body->location.end;
    out = (publish.empty() && map.empty()) ? body : new DefMap(location, std::move(map), std::move(publish), body);

    if (expect(DEDENT, lex)) lex.consume();
    return out;
  } else {
    out = relabel_anon(parse_binary(0, lex, multiline));
  }

  return out;
}

Expr *parse_expr(Lexer &lex) {
  return parse_binary(0, lex, false);
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
        parse_decl(defmap.map, lex, &top, true);
        break;
      }
      case TUPLE:
      case DATA:
      case DEF: {
        parse_decl(defmap.map, lex, &top,false);
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
  auto out = parse_block(lex, false);
  expect(END, lex);
  return out;
}

static Expr *parse_jelement(JLexer &jlex, Top* top);

static Expr *invalid_json(JLexer &jlex) {
  jlex.fail = true;
  jlex.consume();
  return new Literal(jlex.next.location, std::make_shared<Integer>(0));
}

Expr *parse_jlist(JLexer &jlex) {
  Expr *out = new VarRef(jlex.next.location, "Nil");
  if (jlex.next.type == SCLOSE) return out;

  std::vector<Expr*> exps;
  bool repeat = true;
  while (repeat) {
    exps.push_back(parse_jelement(jlex, 0));
    switch (jlex.next.type) {
      case COMMA: {
        jlex.consume();
        break;
      }
      case SCLOSE: {
        jlex.consume();
        repeat = false;
        break;
      }
      default: {
        jlex.consume();
        std::cerr << "Was expecting COMMA/SCLOSE, got a "
          << symbolTable[jlex.next.type]
          << " at " << jlex.next.location << std::endl;
      }
    }
  }

  for (auto i = exps.rbegin(); i != exps.rend(); ++i) {
    out =
      new App(jlex.next.location,
        new App(jlex.next.location,
          new VarRef(jlex.next.location, "binary ,"), *i), out);
  }

  return out;
}

Expr *parse_jargs(JLexer &jlex, Expr *inner) {
  jlex.consume();
  if (jlex.next.type == SCLOSE) return inner;

  std::vector<std::string> args;
  bool repeat = true;
  while (repeat) {
    std::string arg = "_";
    if (expect(STR, jlex)) {
      Literal *lit = reinterpret_cast<Literal*>(jlex.next.expr.get());
      String *str = reinterpret_cast<String*>(lit->value.get());
      arg = str->value;
    }
    jlex.consume();
    args.push_back(std::move(arg));

    switch (jlex.next.type) {
      case COMMA: {
        jlex.consume();
        break;
      }
      case SCLOSE: {
        jlex.consume();
        repeat = false;
        break;
      }
      default: {
        jlex.consume();
        std::cerr << "Was expecting COMMA/SCLOSE, got a "
          << symbolTable[jlex.next.type]
          << " at " << jlex.next.location << std::endl;
      }
    }
  }

  for (auto i = args.rbegin(); i != args.rend(); ++i)
    inner = new Lambda(jlex.next.location, *i, inner);

  return inner;
}

Expr *parse_jbody(JLexer &jlex) {
  Location app = jlex.next.location;

  expect(SOPEN, jlex);
  jlex.consume();

  if (jlex.next.type == SCLOSE) {
    jlex.consume();
    app.end = jlex.next.location.end;
    return new VarRef(app, "Unit");
  }

  Expr *out = 0;
  bool repeat = true;
  while (repeat) {
    Expr *next;
    switch (jlex.next.type) {
      case STR: {
        Literal *lit = reinterpret_cast<Literal*>(jlex.next.expr.get());
        String *str = reinterpret_cast<String*>(lit->value.get());
        next = new VarRef(jlex.next.location, str->value);
        jlex.consume();
        break;
      }
      case SOPEN: {
        next = parse_jbody(jlex);
        break;
      }
      default: {
        std::cerr << "Was expecting STR/SOPEN, got a "
          << symbolTable[jlex.next.type]
          << " at " << jlex.next.location << std::endl;
        next = invalid_json(jlex);
      }
    }

    app.end = next->location.end;
    if (!out) {
      out = next;
    } else {
      out = new App(app, out, next);
    }

    switch (jlex.next.type) {
      case COMMA: {
        jlex.consume();
        break;
      }
      case SCLOSE: {
        jlex.consume();
        repeat = false;
        break;
      }
      default: {
        std::cerr << "Was expecting COMMA/SCLOSE, got a "
          << symbolTable[jlex.next.type]
          << " at " << jlex.next.location << std::endl;
        jlex.consume();
        jlex.fail = true;
      }
    }
  }

  return out;
}

Expr *parse_jobject(JLexer &jlex, Top *top) {
  DefMap *defmap = new DefMap(jlex.next.location);
  if (top) top->defmaps.emplace_back(defmap);
  Expr *out = defmap;

  jlex.consume();
  if (jlex.next.type == BCLOSE && !top) {
    std::cerr << "Empty objects not suppported by wake (must at least have a body) at "
      << jlex.next.location << "." << std::endl;
    return invalid_json(jlex);
  }

  bool repeat = true;
  bool body = false;
  while (repeat) {
    // Extract the JSON key
    std::string key = "bad";
    if (expect(STR, jlex)) {
      Literal *lit = reinterpret_cast<Literal*>(jlex.next.expr.get());
      String *str = reinterpret_cast<String*>(lit->value.get());
      key = str->value;
    }
    jlex.consume();

    expect(COLON, jlex);
    jlex.consume();

    if (key == "body") {
      if (top) {
        std::cerr << "body cannot be defined for top-level at " <<
          jlex.next.location << "." << std::endl;
        return invalid_json(jlex);
      } else {
        body = true;
        defmap->body = std::unique_ptr<Expr>(parse_jbody(jlex));
      }
    } else if (key == "args") {
      if (top) {
        std::cerr << "args cannot be defined for top-level at " <<
          jlex.next.location << "." << std::endl;
        return invalid_json(jlex);
      } else {
        out = parse_jargs(jlex, out);
      }
    } else {
      Expr *value = parse_jelement(jlex, 0);
      bind_def(jlex, defmap->map, key, value);
      bind_global(key, top, jlex);
    }

    switch (jlex.next.type) {
      case COMMA: {
        jlex.consume();
        break;
      }
      case BCLOSE: {
        defmap->location.end = jlex.next.location.end;
        jlex.consume();
        repeat = false;
        break;
      }
      default: {
        std::cerr << "Was expecting COMMA/BCLOSE, got a "
          << symbolTable[jlex.next.type]
          << " at " << jlex.next.location << std::endl;
      }
    }
  }

  if (!body && !top) {
    std::cerr << "Object without body at " << defmap->location << std::endl;
    jlex.fail = true;
  }

  return out;
}

static Expr *parse_jelement(JLexer &jlex, Top* top) {
  switch (jlex.next.type) {
    case FLOAT: {
      std::cerr << "Non-integer numbers not suppported by wake at "
        << jlex.next.location << "." << std::endl;
      return invalid_json(jlex);
    }
    case NULLVAL: {
      std::cerr << "Null values numbers not suppported by wake at "
        << jlex.next.location << "." << std::endl;
      return invalid_json(jlex);
    }
    case STR: {
      Expr *out = jlex.next.expr.release();
      jlex.consume();
      return out;
    }
    case NUM: {
      Expr *out = jlex.next.expr.release();
      jlex.consume();
      return out;
    }
    case TRUE: {
      Expr *out = new VarRef(jlex.next.location, "True");
      jlex.consume();
      return out;
    }
    case FALSE: {
      Expr *out = new VarRef(jlex.next.location, "False");
      jlex.consume();
      return out;
    }
    case SOPEN: {
      return parse_jlist(jlex);
    }
    case BOPEN: {
      return parse_jobject(jlex, top);
    }
    default: {
      std::cerr << "Unexpected symbol "
        << symbolTable[jlex.next.type]
          << " at " << jlex.next.location << std::endl;
      return invalid_json(jlex);
    }
  }
}

void parse_json(Top &top, JLexer &jlex) {
  parse_jelement(jlex, &top);
  expect(END, jlex);
}
