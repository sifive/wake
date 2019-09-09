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
#include "location.h"
#include <iostream>
#include <sstream>
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

static bool expectString(Lexer &lex) {
  if (expect(LITERAL, lex)) {
    if (lex.next.expr->type == &Literal::type) {
      Literal *lit = static_cast<Literal*>(lex.next.expr.get());
      HeapObject *obj = lit->value.get();
      if (typeid(*obj) == typeid(String)) {
        return true;
      } else {
        std::cerr << "Was expecting a String, but got a different literal at "
          << lex.next.location.text() << std::endl;
        lex.fail = true;
        return false;
      }
    } else {
      std::cerr << "Was expecting a String, but got an interpolated string at "
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
    std::cerr
      << "Wildcard cannot be used as a constructor at " << ast.token.text()
      << std::endl;
    return true;
  }

  if (!ast.args.empty() && !ast.name.empty() && Lexer::isLower(ast.name.c_str())) {
    std::cerr
      << "Lower-case identifier cannot be used as a constructor at "
      << ast.token.text()
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
      VarRef *ref = static_cast<VarRef*>(expr);
      if (ref->name != "_") return index;
      ++index;
      ref->name += " ";
      ref->name += std::to_string(index);
      return index;
    } else if (expr->type == &App::type) {
      App *app = static_cast<App*>(expr);
      return relabel_descend(app->val.get(), relabel_descend(app->fn.get(), index));
    } else if (expr->type == &Lambda::type) {
      Lambda *lambda = static_cast<Lambda*>(expr);
      return relabel_descend(lambda->body.get(), index);
    } else if (expr->type == &Match::type) {
      Match *match = static_cast<Match*>(expr);
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
        Literal *lit = static_cast<Literal*>(e);
        HeapObject *obj = lit->value.get();
        if (typeid(*obj) == typeid(Integer)) comparison = "icmp";
        if (typeid(*obj) == typeid(Double)) comparison = "dcmp";
      }
      if (!guard) guard = new VarRef(e->location, "True");
      guard = new App(e->location, new App(e->location, new App(e->location, new App(e->location,
        new VarRef(e->location, "destruct Order"),
        new Lambda(e->location, "_", new VarRef(e->location, "False"), " ")),
        new Lambda(e->location, "_", guard, " ")),
        new Lambda(e->location, "_", new VarRef(e->location, "False"), " ")),
        new App(e->location, new App(e->location,
          new Lambda(e->location, "_", new Lambda(e->location, "_", new Prim(e->location, comparison), " ")),
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
      opp->flags |= FLAG_AST;
      lex.consume();
      auto rhs = parse_binary(op.p + op.l, lex, multiline);
      location.end = rhs->location.end;
      App *out = new App(location, opp, rhs);
      out->flags |= FLAG_AST;
      return out;
    }
    case MATCH: {
      return parse_match(p, lex);
    }
    case LAMBDA: {
      op_type op = op_precedence("\\");
      if (op.p < p) precedence_error(lex);
      Location region = lex.next.location;
      lex.consume();
      ASTState state(false, false);
      AST ast = parse_ast(APP_PRECEDENCE+1, lex, state);
      if (check_constructors(ast)) lex.fail = true;
      auto rhs = parse_binary(op.p + op.l, lex, multiline);
      region.end = rhs->location.end;
      Lambda *out;
      if (Lexer::isUpper(ast.name.c_str()) || Lexer::isOperator(ast.name.c_str())) {
        Match *match = new Match(region);
        match->patterns.emplace_back(std::move(ast), rhs, nullptr);
        match->args.emplace_back(new VarRef(region, "_ xx"));
        out = new Lambda(region, "_ xx", match);
      } else {
        out = new Lambda(region, ast.name, rhs);
        out->token = ast.token;
      }
      out->flags |= FLAG_AST;
      return out;
    }
    // Terminals
    case ID: {
      Expr *out = new VarRef(lex.next.location, lex.id());
      out->flags |= FLAG_AST;
      lex.consume();
      return out;
    }
    case LITERAL: {
      Expr *out = lex.next.expr.release();
      lex.consume();
      out->flags |= FLAG_AST;
      return out;
    }
    case PRIM: {
      std::string name;
      Location location = lex.next.location;
      op_type op = op_precedence("p");
      if (op.p < p) precedence_error(lex);
      lex.consume();
      if (expectString(lex)) {
        Literal *lit = static_cast<Literal*>(lex.next.expr.get());
        name = static_cast<String*>(lit->value.get())->as_str();
        location.end = lex.next.location.end;
        lex.consume();
      } else {
        name = "bad_prim";
      }
      Prim *prim = new Prim(location, name);
      prim->flags |= FLAG_AST;
      return prim;
    }
    case HERE: {
      std::string name(lex.next.location.filename);
      std::string::size_type cut = name.find_last_of('/');
      if (cut == std::string::npos) name = "."; else name.resize(cut);
      Expr *out = new Literal(lex.next.location, String::literal(lex.heap, name), &String::typeVar);
      out->flags |= FLAG_AST;
      lex.consume();
      return out;
    }
    case SUBSCRIBE: {
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
      if (eateol && expect(EOL, lex)) lex.consume();
      location.end = lex.next.location.end;
      if (expect(PCLOSE, lex)) lex.consume();
      out->location = location;
      if (out->type == &Lambda::type) out->flags |= FLAG_AST;
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
      App *out = new App(l, new App(l, new App(l,
        new VarRef(l, "destruct Boolean"),
        new Lambda(l, "_", thenE, " .then")),
        new Lambda(l, "_", elseE, " .else")),
        condE);
      out->flags |= FLAG_AST;
      return out;
    }
    default: {
      std::cerr << "Was expecting an (OPERATOR/LAMBDA/ID/LITERAL/PRIM/POPEN), got a "
        << symbolTable[lex.next.type] << " at "
        << lex.next.location.text() << std::endl;
      lex.fail = true;
      return new Literal(LOCATION, String::literal(lex.heap, "bad unary"), &String::typeVar);
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
        opp->flags |= FLAG_AST;
        lex.consume();
        auto rhs = parse_binary(op.p + op.l, lex, multiline);
        Location app1_loc = lhs->location;
        Location app2_loc = lhs->location;
        app1_loc.end = opp->location.end;
        app2_loc.end = rhs->location.end;
        lhs = new App(app2_loc, new App(app1_loc, opp, lhs), rhs);
        lhs->flags |= FLAG_AST;
        break;
      }
      case MATCH:
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
        lhs->flags |= FLAG_AST;
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

struct Definition {
  std::string name;
  Location location;
  std::unique_ptr<Expr> body;
  Definition(std::string &&name_, const Location &location_, Expr *body_)
   : name(std::move(name_)), location(location_), body(body_) { }
  Definition(const std::string &name_, const Location &location_, Expr *body_)
   : name(std::move(name_)), location(location_), body(body_) { }
};

static void extract_def(std::vector<Definition> &out, long index, AST &&ast, Expr *body) {
  std::string key = "extract " + std::to_string(++index);
  out.emplace_back(key, ast.token, body);
  long x = 0;
  for (auto &m : ast.args) {
    std::stringstream s;
    s << "get" << ast.name << ":" << ast.args.size() << ":" << x++;
    Expr *sub = new App(m.token,
      new VarRef(m.token, s.str()),
      new VarRef(body->location, key));
    if (Lexer::isUpper(m.name.c_str())) {
      extract_def(out, index, std::move(m), sub);
    } else {
      out.emplace_back(m.name, m.token, sub);
    }
  }
}

static std::vector<Definition> parse_def(Lexer &lex, long index, bool target, bool publish) {
  lex.consume();

  ASTState state(false, false);
  AST ast = parse_ast(0, lex, state);
  std::string name = std::move(ast.name);
  ast.name.clear();
  if (check_constructors(ast)) lex.fail = true;

  bool extract = Lexer::isUpper(name.c_str());
  if (extract && (target || publish)) {
    std::cerr << "Upper-case identifier cannot be used as a target/publish name at "
      << ast.token.text() << std::endl;
    lex.fail = true;
    extract = false;
  }

  size_t tohash = ast.args.size();
  if (target && lex.next.type == LAMBDA) {
    lex.consume();
    AST sub = parse_ast(APP_PRECEDENCE, lex, state, AST(lex.next.location));
    if (check_constructors(ast)) lex.fail = true;
    for (auto &x : sub.args) ast.args.push_back(std::move(x));
    ast.region.end = sub.region.end;
  }

  Location fn = ast.region;

  expect(EQUALS, lex);
  lex.consume();

  Expr *body = parse_block(lex, false);
  if (expect(EOL, lex)) lex.consume();

  std::vector<Definition> out;
  if (extract) {
    ast.name = std::move(name);
    extract_def(out, index, std::move(ast), body);
    return out;
  }

  // do we need a pattern match? lower / wildcard are ok
  bool pattern = false;
  for (auto &x : ast.args) {
    pattern |= Lexer::isOperator(x.name.c_str()) || Lexer::isUpper(x.name.c_str());
  }

  std::vector<std::pair<std::string, Location> > args;
  if (!pattern) {
    // no pattern; simple lambdas for the arguments
    for (auto &x : ast.args) args.emplace_back(x.name, x.token);
  } else {
    // bind the arguments to anonymous lambdas and push the whole thing into a pattern
    size_t nargs = ast.args.size();
    Match *match = new Match(fn);
    if (nargs > 1) {
      match->patterns.emplace_back(std::move(ast), body, nullptr);
    } else {
      match->patterns.emplace_back(std::move(ast.args.front()), body, nullptr);
    }
    for (size_t i = 0; i < nargs; ++i) {
      args.emplace_back("_ " + std::to_string(i), LOCATION);
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
    Location bl = body->location;
    Expr *hash = new Prim(bl, "hash");
    for (size_t i = 0; i < tohash; ++i) hash = new Lambda(bl, "_", hash, " ");
    for (size_t i = 0; i < tohash; ++i) hash = new App(bl, hash, new VarRef(bl, args[i].first));
    Expr *subhash = new Prim(bl, "hash");
    for (size_t i = tohash; i < args.size(); ++i) subhash = new Lambda(bl, "_", subhash, " ");
    for (size_t i = tohash; i < args.size(); ++i) subhash = new App(bl, subhash, new VarRef(bl, args[i].first));
    Lambda *gen = new Lambda(bl, "_", body, " ");
    Lambda *tget = new Lambda(bl, "_fn", new Prim(bl, "tget"), " ");
    body = new App(bl, new App(bl, new App(bl, new App(bl,
      new Lambda(bl, "_target", new Lambda(bl, "_hash", new Lambda(bl, "_subhash", tget))),
      new VarRef(bl, "table " + name)), hash), subhash), gen);
  }

  if (publish && !args.empty()) {
    std::cerr << "Publish definition may not be a function " << fn.text() << std::endl;
    lex.fail = true;
  } else {
    for (auto i = args.rbegin(); i != args.rend(); ++i) {
      Lambda *lambda = new Lambda(fn, i->first, body);
      lambda->token = i->second;
      body = lambda;
    }
  }

  out.emplace_back(std::move(name), ast.token, body);
  return out;
}

static void bind_global(const std::string &name, Top *top, Lexer &lex) {
  if (!top || name == "_") return;

  auto it = top->globals.insert(std::make_pair(name, top->defmaps.size()-1));
  if (!it.second) {
    std::cerr << "Duplicate global "
      << name << " at "
      << top->defmaps.back()->map.find(name)->second.body->location.text() << " and "
      << top->defmaps[it.first->second]->map.find(name)->second.body->location.text() << std::endl;
    lex.fail = true;
  }
}

static void bind_def(Lexer &lex, DefMap::Defs &map, Definition &&def, Top *top = 0) {
  if (def.name == "_")
    def.name = "_" + std::to_string(map.size()) + " _";

  Location l = def.body->location;
  auto out = map.insert(std::make_pair(std::move(def.name), DefMap::Value(def.location, std::move(def.body))));

  if (!out.second) {
    std::cerr << "Duplicate def "
      << out.first->first << " at "
      << out.first->second.body->location.text() << " and "
      << l.text() << std::endl;
    lex.fail = true;
  }

  bind_global(out.first->first, top, lex);
}

static void publish_defs(DefMap::Pubs &pub, std::vector<Definition> &&defs) {
  for (auto &def : defs) {
    pub[def.name].emplace_back(def.location, std::move(def.body));
  }
}

static AST parse_unary_ast(int p, Lexer &lex, ASTState &state) {
  TRACE("UNARY_AST");
  switch (lex.next.type) {
    // Unary operators
    case OPERATOR: {
      op_type op = op_precedence(lex.id().c_str());
      if (op.p < p) precedence_error(lex);
      std::string name = "unary " + lex.id();
      Location token = lex.next.location;
      lex.consume();
      AST rhs = parse_ast(op.p + op.l, lex, state);
      std::vector<AST> args;
      args.emplace_back(std::move(rhs));
      auto out = AST(token, std::move(name), std::move(args));
      out.region.end = out.args.back().region.end;
      return out;
    }
    // Terminals
    case ID: {
      AST out(lex.next.location, lex.id());
      lex.consume();
      return out;
    }
    case POPEN: {
      Location region = lex.next.location;
      lex.consume();
      AST out = parse_ast(0, lex, state);
      region.end = lex.next.location.end;
      if (expect(PCLOSE, lex)) lex.consume();
      out.region = region;
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
        Location token = lex.next.location;
        lex.consume();
        auto rhs = parse_ast(op.p + op.l, lex, state);
        Location region = lhs.region;
        region.end = rhs.region.end;
        std::vector<AST> args;
        args.emplace_back(std::move(lhs));
        args.emplace_back(std::move(rhs));
        lhs = AST(token, std::move(name), std::move(args));
        lhs.region = region;
        break;
      }
      case LITERAL:
      case ID:
      case POPEN: {
        op_type op = op_precedence("a"); // application
        if (op.p < p) return lhs;
        AST rhs = parse_ast(op.p + op.l, lex, state);
        lhs.region.end = rhs.region.end;
        if (Lexer::isOperator(lhs.name.c_str())) {
          std::cerr << "Cannot supply additional constructor arguments to " << lhs.name
            << " at " << lhs.region.text() << std::endl;
          lex.fail = true;
        }
        lhs.args.emplace_back(std::move(rhs));
        break;
      }
      case COLON: {
        if (state.type) {
          op_type op = op_precedence(lex.id().c_str());
          if (op.p < p) return lhs;
          Location tagloc = lhs.region;
          lex.consume();
          if (!lhs.args.empty() || Lexer::isOperator(lhs.name.c_str())) {
            std::cerr << "Left-hand-side of COLON must be a simple lower-case identifier, not "
              << lhs.name << " at " << lhs.region.file() << std::endl;
            lex.fail = true;
          }
          std::string tag = std::move(lhs.name);
          lhs = parse_ast(op.p + op.l, lex, state);
          lhs.tag = std::move(tag);
          lhs.region.start = tagloc.start;
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
Sum *Unit;
Sum *Pair;
Sum *Result;
Sum *JValue;

bool sums_ok() {
  bool ok = true;

  if (Boolean) {
    if (Boolean->members.size() != 2 ||
        Boolean->members[0].ast.args.size() != 0 ||
        Boolean->members[1].ast.args.size() != 0) {
      std::cerr << "Special constructor Boolean not defined correctly at "
        << Boolean->region.file() << "." << std::endl;
      ok = false;
    }
  } else {
    std::cerr << "Primitive data type Boolean not defined." << std::endl;
    ok = false;
  }

  if (Order) {
    if (Order->members.size() != 3 ||
        Order->members[0].ast.args.size() != 0 ||
        Order->members[1].ast.args.size() != 0 ||
        Order->members[2].ast.args.size() != 0) {
      std::cerr << "Special constructor Order not defined correctly at "
        << Order->region.file() << "." << std::endl;
      ok = false;
    }
  } else {
    std::cerr << "Primitive data type Order not defined." << std::endl;
    ok = false;
  }

  if (List) {
    if (List->members.size() != 2 ||
        List->members[0].ast.args.size() != 0 ||
        List->members[1].ast.args.size() != 2) {
      std::cerr << "Special constructor List not defined correctly at "
        << List->region.file() << "." << std::endl;
      ok = false;
    }
  } else {
    std::cerr << "Primitive data type List not defined." << std::endl;
    ok = false;
  }

  if (Unit) {
    if (Unit->members.size() != 1 ||
        Unit->members[0].ast.args.size() != 0) {
      std::cerr << "Special constructor Unit not defined correctly at "
        << Unit->region.file() << "." << std::endl;
      ok = false;
    }
  } else {
    std::cerr << "Primitive data type Unit not defined." << std::endl;
    ok = false;
  }

  if (Pair) {
    if (Pair->members.size() != 1 ||
        Pair->members[0].ast.args.size() != 2) {
      std::cerr << "Special constructor Pair not defined correctly at "
        << Pair->region.file() << "." << std::endl;
      ok = false;
    }
  } else {
    std::cerr << "Primitive data type Pair not defined." << std::endl;
    ok = false;
  }

  if (Result) {
    if (Result->members.size() != 2 ||
        Result->members[0].ast.args.size() != 1 ||
        Result->members[1].ast.args.size() != 1) {
      std::cerr << "Special constructor Result not defined correctly at "
        << Result->region.file() << "." << std::endl;
      ok = false;
    }
  } else {
    std::cerr << "Primitive data type Result not defined." << std::endl;
    ok = false;
  }

  if (JValue) {
    if (JValue->members.size() != 7 ||
        JValue->members[0].ast.args.size() != 1 ||
        JValue->members[1].ast.args.size() != 1 ||
        JValue->members[2].ast.args.size() != 1 ||
        JValue->members[3].ast.args.size() != 1 ||
        JValue->members[4].ast.args.size() != 0 ||
        JValue->members[5].ast.args.size() != 1 ||
        JValue->members[6].ast.args.size() != 1) {
      std::cerr << "Special constructor JValue not defined correctly at "
        << JValue->region.file() << "." << std::endl;
      ok = false;
    }
  } else {
    std::cerr << "Primitive data type JValue not defined." << std::endl;
    ok = false;
  }

  return ok;
}

static AST parse_type_def(Lexer &lex) {
  lex.consume();

  ASTState state(false, false);
  AST def = parse_ast(0, lex, state);
  if (check_constructors(def)) lex.fail = true;
  if (!def) return def;

  if (def.name == "_" || Lexer::isLower(def.name.c_str())) {
    std::cerr << "Type name must be upper-case or operator, not "
      << def.name << " at "
      << def.token.file() << std::endl;
    lex.fail = true;
  }

  std::set<std::string> args;
  for (auto &x : def.args) {
    if (!Lexer::isLower(x.name.c_str())) {
      std::cerr << "Type argument must be lower-case, not "
        << x.name << " at "
        << x.token.file() << std::endl;
      lex.fail = true;
    }
    if (!args.insert(x.name).second) {
      std::cerr << "Type argument "
        << x.name << " occurs more than once at "
        << x.token.file() << std::endl;
      lex.fail = true;
    }
  }

  if (expect(EQUALS, lex)) lex.consume();

  return def;
}

static void check_special(Lexer &lex, const std::string &name, Sum *sump) {
  if (name == "Integer" || name == "String" || name == "RegExp" || name == "Target" ||
      name == FN || name == "Job" || name == "Array" || name == "Double") {
    std::cerr << "Constuctor " << name
      << " is reserved at " << sump->token.file() << "." << std::endl;
    lex.fail = true;
  }

  if (name == "Boolean") Boolean = sump;
  if (name == "Order")   Order = sump;
  if (name == "List")    List = sump;
  if (name == "Unit")    Unit = sump;
  if (name == "Pair")    Pair = sump;
  if (name == "Result")  Result = sump;
  if (name == "JValue")  JValue = sump;
}

static void parse_tuple(Lexer &lex, DefMap::Defs &map, Top *top, bool global) {
  AST def = parse_type_def(lex);
  if (!def) return;

  std::string name = def.name;
  std::string tname = "destruct " + name;
  Sum sum(std::move(def));
  AST tuple(sum.token, std::string(sum.name));
  tuple.region = sum.region;
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

  Location location = sum.token;
  Destruct *destruct = new Destruct(location, std::move(sum));
  std::shared_ptr<Sum> sump = destruct->sum;
  Expr *destructfn =
    new Lambda(sump->token, "_",
    new Lambda(sump->token, "_",
    destruct));

  Constructor &c = sump->members.back();
  Expr *construct = new Construct(c.ast.token, sump, &c);
  for (size_t i = c.ast.args.size(); i > 0; --i)
    construct = new Lambda(c.ast.token, c.ast.args[i-1].tag, construct);

  bind_def(lex, map, Definition(c.ast.name, c.ast.token, construct), global?top:0);
  bind_def(lex, map, Definition(tname, c.ast.token, destructfn), global?top:0);

  check_special(lex, name, sump.get());

  // Create get/set/edit helper methods
  int outer = 0;
  for (unsigned i = 0; i < members.size(); ++i) {
    std::string &mname = c.ast.args[i].tag;
    Location memberToken = c.ast.args[i].region;
    bool global = members[i];
    if (mname.empty()) continue;

    // Implement get methods
    std::string get = "get" + name + mname;
    Expr *getfn = new Lambda(memberToken, "_", new Get(memberToken, sump, &c, i));
    bind_def(lex, map, Definition(get, memberToken, getfn), global?top:0);

    // Implement def extractor methods
    std::stringstream s;
    s << "get" << name << ":" << c.ast.args.size() << ":" << i;
    Expr *egetfn = new Lambda(memberToken, "_", new Get(memberToken, sump, &c, i));
    bind_def(lex, map, Definition(s.str(), memberToken, egetfn), global?top:0);

    // Implement edit methods
    Expr *editifn = new VarRef(memberToken, name);
    for (int inner = 0; inner < (int)members.size(); ++inner) {
      auto get = new Get(memberToken, sump, &c, inner);
      editifn = new App(memberToken, editifn,
        (inner == outer)
        ? static_cast<Expr*>(new App(memberToken,
           new VarRef(memberToken, "fn" + mname), get))
        : static_cast<Expr*>(get));
    }

    std::string edit = "edit" + name + mname;
    Expr *editfn =
      new Lambda(memberToken, "fn" + mname,
        new Lambda(memberToken, "_ x", editifn));

    bind_def(lex, map, Definition(edit, memberToken, editfn), global?top:0);

    // Implement set methods
    Expr *setifn = new VarRef(memberToken, name);
    for (int inner = 0; inner < (int)members.size(); ++inner) {
      setifn = new App(memberToken, setifn,
        (inner == outer)
        ? static_cast<Expr*>(new VarRef(memberToken, mname))
        : static_cast<Expr*>(new Get(memberToken, sump, &c, inner)));
    }

    std::string set = "set" + name + mname;
    Expr *setfn =
      new Lambda(memberToken, mname,
        new Lambda(memberToken, "_ x", setifn));

    bind_def(lex, map, Definition(set, memberToken, setfn), global?top:0);

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
        << cons.region.file() << std::endl;
      lex.fail = true;
    }
    if (cons.name == "_" || Lexer::isLower(cons.name.c_str())) {
      std::cerr << "Constructor name must be upper-case or operator, not "
        << cons.name << " at "
        << cons.token.file() << std::endl;
      lex.fail = true;
    }
    sum.addConstructor(std::move(cons));
  }
}

static void parse_data(Lexer &lex, DefMap::Defs &map, Top *top, bool global) {
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
  Location location = sum.token;
  Destruct *destruct = new Destruct(location, std::move(sum));
  std::shared_ptr<Sum> sump = destruct->sum;
  Expr *destructfn = new Lambda(sump->token, "_", destruct);

  for (auto &c : sump->members) {
    destructfn = new Lambda(sump->token, "_", destructfn);
    Expr *construct = new Construct(c.ast.token, sump, &c);
    for (size_t i = 0; i < c.ast.args.size(); ++i)
      construct = new Lambda(c.ast.token, "_", construct);

    bind_def(lex, map, Definition(c.ast.name, c.ast.token, construct), global?top:0);
  }

  bind_def(lex, map, Definition("destruct " + name, sump->token, destructfn), global?top:0);
  for (auto &cons : sump->members) {
    for (size_t i = 0; i < cons.ast.args.size(); ++i) {
      Expr *body = new Lambda(sump->token, "_", new Get(sump->token, sump, &cons, i));
      std::string name = "get " + cons.ast.name + " " + std::to_string(i);
      bind_def(lex, map, Definition(name, sump->token, body), global?top:0);
    }
  }

  check_special(lex, name, sump.get());
}

static void parse_decl(DefMap::Defs &map, Lexer &lex, Top *top, bool global) {
  switch (lex.next.type) {
    default:
       std::cerr << "Missing DEF after GLOBAL at " << lex.next.location.text() << std::endl;
       lex.fail = true;
    case DEF: {
      for (auto &def : parse_def(lex, map.size(), false, false))
         bind_def(lex, map, std::move(def), global?top:0);
      break;
    }
    case TARGET: {
      auto defs = parse_def(lex, 0, true, false);
      auto &def = defs.front();
      auto &l = def.body->location;
      std::stringstream s;
      s << l.text();
      bind_def(lex, map, Definition("table " + def.name, def.location,
        new App(l, new Lambda(l, "_", new Prim(l, "tnew"), " "),
        new Literal(l, String::literal(lex.heap, s.str()), &String::typeVar))));
      bind_def(lex, map, std::move(def), global?top:0);
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
    DefMap::Defs map;
    DefMap::Pubs pub;

    bool repeat = true;
    while (repeat) {
      switch (lex.next.type) {
        case TARGET:
        case DEF: {
          parse_decl(map, lex, 0, false);
          break;
        }
        case PUBLISH: {
          publish_defs(pub, parse_def(lex, 0, false, true));
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
    if (pub.empty() && map.empty()) {
      out = body;
    } else {
      out = new DefMap(location, std::move(map), std::move(pub), body);
      out->flags |= FLAG_AST;
    }

    out->location.start.bytes -= (out->location.start.column-1);
    out->location.start.column = 1;

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
        publish_defs(defmap.pub, parse_def(lex, 0, false, true));
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
