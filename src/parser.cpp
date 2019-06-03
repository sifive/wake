/*
 * Copyright 2019 SiFive, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You should have received a copy of LICENSE.Apache2 along with
 * this software. If not, you may obtain a copy at
 *
 *    https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

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

bool expect(SymbolType type, Lexer &lex) {
  if (lex.next.type != type) {
    std::cerr << "Was expecting a "
      << symbolTable[type] << ", but got a "
      << symbolTable[lex.next.type] << " at "
      << lex.next.location.text() << std::endl;
    lex.fail = true;
    return false;
  }
  return true;
}

static std::pair<std::string, Location> get_arg_loc(Lexer &lex) {
  if (lex.next.type != ID) {
    std::cerr << "Was expecting an ID argument, but got a "
      << symbolTable[lex.next.type] << " at "
      << lex.next.location.text() << std::endl;
    lex.fail = true;
  }

  auto out = std::make_pair(lex.id(), lex.next.location);
  lex.consume();
  return out;
}

static bool expectValue(const TypeDescriptor *type, Lexer &lex) {
  if (expect(LITERAL, lex)) {
    if (lex.next.expr->type == &Literal::type) {
      Literal *lit = reinterpret_cast<Literal*>(lex.next.expr.get());
      if (lit->value->type == type) {
        return true;
      } else {
        std::cerr << "Was expecting a "
          << type->name << ", but got a "
          << lit->value->type->name << " at "
          << lex.next.location.text() << std::endl;
        lex.fail = true;
        return false;
      }
    } else {
      std::cerr << "Was expecting a "
        << type->name << ", but got an interpolated string at "
        << lex.next.location.text() << std::endl;
      lex.fail = true;
      return false;
    }
  } else {
    return false;
  }
}

static Expr *parse_binary(int p, Lexer &lex, bool multiline);
static Expr *parse_block(Lexer &lex, bool multiline);

struct ASTState {
  bool type;  // control ':' reduction
  bool match; // allow literals
  std::vector<Expr*> guard;
  ASTState(bool type_, bool match_) : type(type_), match(match_) { }
};

static AST parse_unary_ast(int p, Lexer &lex, ASTState &state);
static AST parse_ast(int p, Lexer &lex, ASTState &state, AST &&lhs);
static AST parse_ast(int p, Lexer &lex, ASTState &state) {
  return parse_ast(p, lex, state, parse_unary_ast(p, lex, state));
}

static bool check_constructors(const AST &ast) {
  if (!ast.args.empty() && ast.name == "_") {
    std::cerr << "Wildcard _"
      << " cannot be used as a constructor at " << ast.location.text()
      << std::endl;
    return true;
  }

  if (!ast.args.empty() && !ast.name.empty() && Lexer::isLower(ast.name.c_str())) {
    std::cerr << "Lower-case identifier " << ast.name
      << " cannot be used as a constructor at " << ast.location.text()
      << std::endl;
    return true;
  }

  bool fail = false;
  for (auto &a : ast.args) fail = check_constructors(a) || fail;
  return fail;
}

static int relabel_descend(Expr *expr, int index) {
  if (!(expr->flags & FLAG_TOUCHED)) {
    expr->flags |= FLAG_TOUCHED;
    if (expr->type == &VarRef::type) {
      VarRef *ref = reinterpret_cast<VarRef*>(expr);
      if (ref->name != "_") return index;
      ++index;
      ref->name += " ";
      ref->name += std::to_string(index);
      return index;
    } else if (expr->type == &App::type) {
      App *app = reinterpret_cast<App*>(expr);
      return relabel_descend(app->val.get(), relabel_descend(app->fn.get(), index));
    } else if (expr->type == &Lambda::type) {
      Lambda *lambda = reinterpret_cast<Lambda*>(expr);
      return relabel_descend(lambda->body.get(), index);
    } else if (expr->type == &Match::type) {
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
    out = new Lambda(out->location, "_ " + std::to_string(index), out);
  return out;
}

static void precedence_error(Lexer &lex) {
  std::cerr << "Lower precedence unary operator "
    << lex.id() << " must use ()s at "
    << lex.next.location.file() << std::endl;
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
        std::cerr << "Unexpected end of match definition at " << lex.next.location.text() << std::endl;
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
    ASTState state(false, true);
    AST ast = multiarg
      ? parse_ast(APP_PRECEDENCE, lex, state, AST(lex.next.location))
      : parse_ast(0, lex, state);
    if (check_constructors(ast)) lex.fail = true;

    Expr *guard = 0;
    if (lex.next.type == IF) {
      lex.consume();
      bool eateol = lex.next.type == INDENT;
      guard = parse_block(lex, false);
      if (eateol && expect(EOL, lex)) lex.consume();
    }

    for (size_t i = 0; i < state.guard.size(); ++i) {
      Expr* e = state.guard[i];
      std::string comparison("scmp");
      if (e->type == &Literal::type) {
        Literal *lit = reinterpret_cast<Literal*>(e);
        if (lit->value->type == &Integer::type) comparison = "icmp";
        if (lit->value->type == &Double::type) comparison = "dcmp";
      }
      if (!guard) guard = new VarRef(e->location, "True");
      guard = new App(e->location, new App(e->location, new App(e->location, new App(e->location,
        new VarRef(e->location, "destruct Order"),
        new Lambda(e->location, "_", new VarRef(e->location, "False"))),
        new Lambda(e->location, "_", guard)),
        new Lambda(e->location, "_", new VarRef(e->location, "False"))),
        new App(e->location, new App(e->location,
          new Lambda(e->location, "_", new Lambda(e->location, "_", new Prim(e->location, comparison))),
          e), new VarRef(e->location, "_ k" + std::to_string(i))));
    }

    if (expect(EQUALS, lex)) lex.consume();
    Expr *expr = parse_block(lex, false);
    out->patterns.emplace_back(std::move(ast), expr, guard);

    switch (lex.next.type) {
      case DEDENT:
        repeat = false;
        lex.consume();
        break;
      case EOL:
        lex.consume();
        break;
      default:
        std::cerr << "Unexpected end of match definition at " << lex.next.location.text() << std::endl;
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
  if (lex.next.type == EOL && multiline) lex.consume();
  switch (lex.next.type) {
    // Unary operators
    case OPERATOR: {
      Location location = lex.next.location;
      op_type op = op_precedence(lex.id().c_str());
      if (op.p < p) precedence_error(lex);
      auto opp = new VarRef(lex.next.location, "unary " + lex.id());
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
      if (expectValue(&Integer::type, lex)) {
        Literal *lit = reinterpret_cast<Literal*>(lex.next.expr.get());
        mpz_t &x = reinterpret_cast<Integer*>(lit->value.get())->value;
        if (mpz_fits_slong_p(x)) {
          skip = mpz_get_si(x);
        } else {
          std::cerr << "Integer argument to memoize too large at "
             << location.text() << std::endl;
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
      ASTState state(false, false);
      AST ast = parse_ast(APP_PRECEDENCE+1, lex, state);
      if (check_constructors(ast)) lex.fail = true;
      auto rhs = parse_binary(op.p + op.l, lex, multiline);
      location.end = rhs->location.end;
      if (Lexer::isUpper(ast.name.c_str()) || Lexer::isOperator(ast.name.c_str())) {
        Match *match = new Match(location);
        match->patterns.emplace_back(std::move(ast), rhs, nullptr);
        match->args.emplace_back(new VarRef(LOCATION, "_ xx"));
        return new Lambda(location, "_ xx", match);
      } else {
        return new Lambda(location, ast.name, rhs);
      }
    }
    // Terminals
    case ID: {
      Expr *out = new VarRef(lex.next.location, lex.id());
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
      if (expectValue(&String::type, lex)) {
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
      std::string name(lex.next.location.filename);
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
      bool eateol = lex.next.type == INDENT;
      Expr *out = parse_block(lex, multiline);
      location.end = lex.next.location.end;
      if (eateol && expect(EOL, lex)) lex.consume();
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
      if (lex.next.type == EOL && multiline) lex.consume();
      if (expect(THEN, lex)) lex.consume();
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
        << lex.next.location.text() << std::endl;
      lex.fail = true;
      return new Literal(LOCATION, "bad unary");
    }
  }
}

static Expr *parse_binary(int p, Lexer &lex, bool multiline) {
  TRACE("BINARY");
  auto lhs = parse_unary(p, lex, multiline);
  for (;;) {
    switch (lex.next.type) {
      case OPERATOR: {
        op_type op = op_precedence(lex.id().c_str());
        if (op.p < p) return lhs;
        auto opp = new VarRef(lex.next.location, "binary " + lex.id());
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

static Expr *parse_def(Lexer &lex, std::string &name, bool target = false) {
  lex.consume();

  ASTState state(false, false);
  AST ast = parse_ast(0, lex, state);
  name = std::move(ast.name);
  ast.name.clear();
  if (check_constructors(ast)) lex.fail = true;

  if (Lexer::isUpper(name.c_str())) {
    std::cerr << "Upper-case identifier cannot be used as a function name at "
      << ast.location.text() << std::endl;
    lex.fail = true;
  }

  int tohash = ast.args.size();
  if (target && lex.next.type == LAMBDA) {
    lex.consume();
    AST sub = parse_ast(APP_PRECEDENCE, lex, state, AST(lex.next.location));
    if (check_constructors(ast)) lex.fail = true;
    for (auto &x : sub.args) ast.args.push_back(std::move(x));
    ast.location.end = sub.location.end;
  }

  Location fn = ast.location;

  expect(EQUALS, lex);
  lex.consume();

  Expr *body = parse_block(lex, false);
  if (expect(EOL, lex)) lex.consume();

  // do we need a pattern match? lower / wildcard are ok
  bool pattern = false;
  for (auto &x : ast.args) {
    pattern |= Lexer::isOperator(x.name.c_str()) || Lexer::isUpper(x.name.c_str());
  }

  std::vector<std::string> args;
  if (!pattern) {
    // no pattern; simple lambdas for the arguments
    for (auto &x : ast.args) args.push_back(x.name);
  } else {
    // bind the arguments to anonymous lambdas and push the whole thing into a pattern
    int nargs = ast.args.size();
    Match *match = new Match(fn);
    if (nargs > 1) {
      match->patterns.emplace_back(std::move(ast), body, nullptr);
    } else {
      match->patterns.emplace_back(std::move(ast.args.front()), body, nullptr);
    }
    for (int i = 0; i < nargs; ++i) {
      args.push_back("_ " + std::to_string(i));
      match->args.emplace_back(new VarRef(fn, "_ " + std::to_string(i)));
    }
    body = match;
  }

  if (target) {
    if (tohash == 0) {
      std::cerr << "Target definition must have at least one hashed argument "
        << fn.text() << std::endl;
      lex.fail = true;
    }
    Expr *hash = new Prim(fn, "hash");
    for (int i = 0; i < tohash; ++i) hash = new Lambda(fn, "_", hash);
    for (int i = 0; i < tohash; ++i) hash = new App(fn, hash, new VarRef(fn, args[i]));
    body = new App(fn, new App(fn, new App(fn,
      new Lambda(fn, "_target", new Lambda(fn, "_hash", new Lambda(fn, "_fn", new Prim(fn, "tget")))),
      new VarRef(fn, "table " + name)),
      hash),
      new Lambda(fn, "_", body));
  }

  for (auto i = args.rbegin(); i != args.rend(); ++i)
    body = new Lambda(fn, *i, body);

  return body;
}

static void bind_def(Lexer &lex, DefMap::defs &map, std::string name, Expr *def) {
  if (name == "_")
    name += std::to_string(map.size());

  auto i = map.find(name);
  if (i != map.end()) {
    std::cerr << "Duplicate def "
      << name << " at "
      << i->second->location.text() << " and "
      << def->location.text() << std::endl;
    lex.fail = true;
  }
  map[name] = std::unique_ptr<Expr>(def);
}

static void publish_def(DefMap::defs &publish, const std::string &name, Expr *def) {
  // Build a prepender
  std::unique_ptr<Expr> tail(
    new App(def->location, new App(def->location,
      new VarRef(def->location, "binary ++"),
      def), new VarRef(def->location, "_ tail")));

  DefMap::defs::iterator i;
  if ((i = publish.find(name)) == publish.end()) {
    i = publish.insert(std::make_pair(name, std::move(tail))).first;
  } else {
    // Apply the existing prepender to us (we come after it)
    i->second = std::unique_ptr<Expr>(new App(def->location, i->second.release(), tail.release()));
  }

  i->second = std::unique_ptr<Expr>(new Lambda(def->location, "_ tail", i->second.release()));
}

static void bind_global(const std::string &name, Top *top, Lexer &lex) {
  if (!top || name == "_") return;

  auto it = top->globals.find(name);
  if (it != top->globals.end()) {
    std::cerr << "Duplicate global "
      << name << " at "
      << top->defmaps.back()->map[name]->location.text() << " and "
      << top->defmaps[it->second]->map[name]->location.text() << std::endl;
    lex.fail = true;
  } else {
    top->globals[name] = top->defmaps.size()-1;
  }
}

static AST parse_unary_ast(int p, Lexer &lex, ASTState &state) {
  TRACE("UNARY_AST");
  switch (lex.next.type) {
    // Unary operators
    case OPERATOR: {
      Location location = lex.next.location;
      op_type op = op_precedence(lex.id().c_str());
      if (op.p < p) precedence_error(lex);
      std::string name = "unary " + lex.id();
      lex.consume();
      AST rhs = parse_ast(op.p + op.l, lex, state);
      location.end = rhs.location.end;
      std::vector<AST> args;
      args.emplace_back(std::move(rhs));
      return AST(location, std::move(name), std::move(args));
    }
    // Terminals
    case ID: {
      AST out(lex.next.location, lex.id());
      lex.consume();
      return out;
    }
    case POPEN: {
      Location location = lex.next.location;
      lex.consume();
      AST out = parse_ast(0, lex, state);
      location.end = lex.next.location.end;
      if (expect(PCLOSE, lex)) lex.consume();
      out.location = location;
      return out;
    }
    case LITERAL: {
      if (state.match) {
        AST out(lex.next.location, "_ k" + std::to_string(state.guard.size()));
        state.guard.push_back(lex.next.expr.release());
        lex.consume();
        return out;
      }
      // fall through to default
    }
    default: {
      std::cerr << "Was expecting an (OPERATOR/ID/POPEN), got a "
        << symbolTable[lex.next.type] << " at "
        << lex.next.location.text() << std::endl;
      lex.consume();
      lex.fail = true;
      return AST(lex.next.location);
    }
  }
}

static AST parse_ast(int p, Lexer &lex, ASTState &state, AST &&lhs_) {
  AST lhs(std::move(lhs_));
  TRACE("AST");
  for (;;) {
    switch (lex.next.type) {
      case OPERATOR: {
        op_type op = op_precedence(lex.id().c_str());
        if (op.p < p) return lhs;
        std::string name = "binary " + lex.id();
        lex.consume();
        auto rhs = parse_ast(op.p + op.l, lex, state);
        Location loc = lhs.location;
        loc.end = rhs.location.end;
        std::vector<AST> args;
        args.emplace_back(std::move(lhs));
        args.emplace_back(std::move(rhs));
        lhs = AST(loc, std::move(name), std::move(args));
        break;
      }
      case LITERAL:
      case ID:
      case POPEN: {
        op_type op = op_precedence("a"); // application
        if (op.p < p) return lhs;
        AST rhs = parse_ast(op.p + op.l, lex, state);
        Location location = lhs.location;
        location.end = rhs.location.end;
        if (Lexer::isOperator(lhs.name.c_str())) {
          std::cerr << "Cannot supply additional constructor arguments to " << lhs.name
            << " at " << location.text() << std::endl;
          lex.fail = true;
        }
        lhs.args.emplace_back(std::move(rhs));
        lhs.location = location;
        break;
      }
      case COLON: {
        if (state.type) {
          op_type op = op_precedence(lex.id().c_str());
          if (op.p < p) return lhs;
          Location tagloc = lhs.location;
          lex.consume();
          if (!lhs.args.empty() || Lexer::isOperator(lhs.name.c_str())) {
            std::cerr << "Left-hand-side of COLON must be a simple lower-case identifier, not "
              << lhs.name << " at " << lhs.location.file() << std::endl;
            lex.fail = true;
          }
          std::string tag = std::move(lhs.name);
          lhs = parse_ast(op.p + op.l, lex, state);
          lhs.tag = std::move(tag);
          lhs.location.start = tagloc.start;
          break;
        }
        // fall-through to default
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
Sum *Result;
Sum *Unit;
Sum *JValue;

static AST parse_type_def(Lexer &lex) {
  lex.consume();

  ASTState state(false, false);
  AST def = parse_ast(0, lex, state);
  if (check_constructors(def)) lex.fail = true;
  if (!def) return def;

  if (def.name == "_" || Lexer::isLower(def.name.c_str())) {
    std::cerr << "Type name must be upper-case or operator, not "
      << def.name << " at "
      << def.location.file() << std::endl;
    lex.fail = true;
  }

  std::set<std::string> args;
  for (auto &x : def.args) {
    if (!Lexer::isLower(x.name.c_str())) {
      std::cerr << "Type argument must be lower-case, not "
        << x.name << " at "
        << x.location.file() << std::endl;
      lex.fail = true;
    }
    if (!args.insert(x.name).second) {
      std::cerr << "Type argument "
        << x.name << " occurs more than once at "
        << x.location.file() << std::endl;
      lex.fail = true;
    }
  }

  if (expect(EQUALS, lex)) lex.consume();

  return def;
}

static void check_special(Lexer &lex, const std::string &name, Sum *sump) {
  if (name == "Integer" || name == "String" || name == "RegExp" ||
      name == FN || name == "Job" || name == "Array" || name == "Double") {
    std::cerr << "Constuctor " << name
      << " is reserved at " << sump->location.file() << "." << std::endl;
    lex.fail = true;
  }

  if (name == "Boolean") {
    if (sump->members.size() != 2 ||
        sump->members[0].ast.args.size() != 0 ||
        sump->members[1].ast.args.size() != 0) {
      std::cerr << "Special constructor Boolean not defined correctly at "
        << sump->location.file() << "." << std::endl;
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
        << sump->location.file() << "." << std::endl;
      lex.fail = true;
    }
    Order = sump;
  }

  if (name == "List") {
    if (sump->members.size() != 2 ||
        sump->members[0].ast.args.size() != 0 ||
        sump->members[1].ast.args.size() != 2) {
      std::cerr << "Special constructor List not defined correctly at "
        << sump->location.file() << "." << std::endl;
      lex.fail = true;
    }
    List = sump;
  }

  if (name == "Pair") {
    if (sump->members.size() != 1 ||
        sump->members[0].ast.args.size() != 2) {
      std::cerr << "Special constructor Pair not defined correctly at "
        << sump->location.file() << "." << std::endl;
      lex.fail = true;
    }
    Pair = sump;
  }

  if (name == "Result") {
    if (sump->members.size() != 2 ||
        sump->members[0].ast.args.size() != 1 ||
        sump->members[1].ast.args.size() != 1) {
      std::cerr << "Special constructor Result not defined correctly at "
        << sump->location.file() << "." << std::endl;
      lex.fail = true;
    }
    Result = sump;
  }

  if (name == "Unit") {
    if (sump->members.size() != 1 ||
        sump->members[0].ast.args.size() != 0) {
      std::cerr << "Special constructor Unit not defined correctly at "
        << sump->location.file() << "." << std::endl;
      lex.fail = true;
    }
    Unit = sump;
  }

  if (name == "JValue") {
    if (sump->members.size() != 7) {
      std::cerr << "Special constructor JValue not defined correctly at "
        << sump->location.file() << "." << std::endl;
      lex.fail = true;
    }
    JValue = sump;
  }
}

static void parse_tuple(Lexer &lex, DefMap::defs &map, Top *top, bool global) {
  AST def = parse_type_def(lex);
  if (!def) return;

  std::string name = def.name;
  std::string tname = "destruct " + name;
  Sum sum(std::move(def));
  AST tuple(sum.location, std::string(sum.name));
  std::vector<bool> members;

  if (!expect(INDENT, lex)) return;
  lex.consume();
  expect(EOL, lex);
  lex.consume();

  bool repeat = true;
  while (repeat) {
    bool global = lex.next.type == GLOBAL;
    if (global) lex.consume();

    ASTState state(true, false);
    AST member = parse_ast(0, lex, state);
    if (check_constructors(member)) lex.fail = true;
    if (member) {
      tuple.args.push_back(member);
      members.push_back(global);
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
        std::cerr
          << "Unexpected end of tuple definition at "
          << lex.next.location.text() << std::endl;
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
  for (size_t i = c.ast.args.size(); i > 0; --i)
    construct = new Lambda(c.ast.location, c.ast.args[i-1].tag, construct);

  bind_def(lex, map, c.ast.name, construct);
  if (global) bind_global(c.ast.name, top, lex);

  bind_def(lex, map, tname, destructfn);
  if (global) bind_global(tname, top, lex);

  check_special(lex, name, sump);

  // Create get/set/edit helper methods
  int outer = 0;
  for (unsigned i = 0; i < members.size(); ++i) {
    std::string &mname = c.ast.args[i].tag;
    bool global = members[i];
    if (mname.empty()) continue;

    // Implement get methods
    Expr *getifn = new VarRef(sump->location, "_ " + std::to_string(outer+1));
    for (int inner = (int)members.size(); inner >= 0; --inner)
      getifn = new Lambda(sump->location, "_ " + std::to_string(inner), getifn);

    std::string get = "get" + name + mname;
    Expr *getfn =
      new Lambda(sump->location, "_ x",
        new App(sump->location,
          new App(sump->location,
            new VarRef(sump->location, tname),
            getifn),
          new VarRef(sump->location, "_ x")));

    bind_def(lex, map, get, getfn);
    if (global) bind_global(get, top, lex);

    // Implement edit methods
    Expr *editifn = new VarRef(sump->location, name);
    for (int inner = 0; inner < (int)members.size(); ++inner)
      editifn = new App(sump->location, editifn,
        (inner == outer)
        ? reinterpret_cast<Expr*>(new App(sump->location,
           new VarRef(sump->location, "fn" + mname),
           new VarRef(sump->location, "_ " + std::to_string(inner+1))))
        : reinterpret_cast<Expr*>(new VarRef(sump->location, "_ " + std::to_string(inner+1))));
    for (int inner = (int)members.size(); inner >= 0; --inner)
      editifn = new Lambda(sump->location, "_ " + std::to_string(inner), editifn);

    std::string edit = "edit" + name + mname;
    Expr *editfn =
      new Lambda(sump->location, "fn" + mname,
        new Lambda(sump->location, "_ x",
          new App(sump->location,
            new App(sump->location,
              new VarRef(sump->location, tname),
              editifn),
            new VarRef(sump->location, "_ x"))));

    bind_def(lex, map, edit, editfn);
    if (global) bind_global(edit, top, lex);

    // Implement set methods
    Expr *setifn = new VarRef(sump->location, name);
    for (int inner = 0; inner < (int)members.size(); ++inner)
      setifn = new App(sump->location, setifn,
        (inner == outer)
        ? reinterpret_cast<Expr*>(new VarRef(sump->location, mname))
        : reinterpret_cast<Expr*>(new VarRef(sump->location, "_ " + std::to_string(inner+1))));
    for (int inner = (int)members.size(); inner >= 0; --inner)
      setifn = new Lambda(sump->location, "_ " + std::to_string(inner), setifn);

    std::string set = "set" + name + mname;
    Expr *setfn =
      new Lambda(sump->location, mname,
        new Lambda(sump->location, "_ x",
          new App(sump->location,
            new App(sump->location,
              new VarRef(sump->location, tname),
              setifn),
            new VarRef(sump->location, "_ x"))));

    bind_def(lex, map, set, setfn);
    if (global) bind_global(set, top, lex);

    ++outer;
  }
}

static void parse_data_elt(Lexer &lex, Sum &sum) {
  ASTState state(true, false);
  AST cons = parse_ast(0, lex, state);

  if (cons) {
    if (check_constructors(cons)) lex.fail = true;
    if (!cons.tag.empty()) {
      std::cerr << "Constructor "
        << cons.name << " should not be tagged with "
        << cons.tag << " at "
        << cons.location.file() << std::endl;
      lex.fail = true;
    }
    if (cons.name == "_" || Lexer::isLower(cons.name.c_str())) {
      std::cerr << "Constructor name must be upper-case or operator, not "
        << cons.name << " at "
        << cons.location.file() << std::endl;
      lex.fail = true;
    }
    sum.addConstructor(std::move(cons));
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
      parse_data_elt(lex, sum);
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
          std::cerr
            << "Unexpected end of data definition at "
            << lex.next.location.text() << std::endl;
          lex.fail = true;
          repeat = false;
          break;
        }
      }
    }
  } else {
    parse_data_elt(lex, sum);
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
       std::cerr << "Missing DEF after GLOBAL at " << lex.next.location.text() << std::endl;
       lex.fail = true;
    case DEF: {
      std::string name;
      auto def = parse_def(lex, name);
      bind_def(lex, map, name, def);
      if (global) bind_global(name, top, lex);
      break;
    }
    case TARGET: {
      std::string name;
      auto def = parse_def(lex, name, true);
      bind_def(lex, map, name, def);
      bind_def(lex, map, "table " + name, new Prim(def->location, "tnew"));
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
        case TARGET:
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
      case TARGET:
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
