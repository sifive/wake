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

// Open Group Base Specifications Issue 7
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <iostream>
#include <sstream>
#include <set>

#include "frontend/parser.h"
#include "frontend/expr.h"
#include "frontend/symbol.h"
#include "runtime/value.h"
#include "location.h"

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
      HeapObject *obj = lit->value->get();
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
  bool topParen;
  std::vector<Expr*> guard;
  ASTState(bool type_, bool match_) : type(type_), match(match_), topParen(false) { }
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
    } else if (expr->type == &Ascribe::type) {
      Ascribe *ascribe = static_cast<Ascribe*>(expr);
      return relabel_descend(ascribe->body.get(), index);
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
static Expr *add_literal_guards(Expr *guard, const ASTState &state) {
  for (size_t i = 0; i < state.guard.size(); ++i) {
    Expr* e = state.guard[i];
    std::string comparison("scmp");
    if (e->type == &Literal::type) {
      Literal *lit = static_cast<Literal*>(e);
      HeapObject *obj = lit->value->get();
      if (typeid(*obj) == typeid(Integer)) comparison = "icmp";
      if (typeid(*obj) == typeid(Double)) comparison = "dcmp_nan_lt";
      if (typeid(*obj) == typeid(RegExp)) comparison = "rcmp";
    }
    if (!guard) guard = new VarRef(e->location, "True@wake");

    Match *match = new Match(e->location);
    match->args.emplace_back(new App(e->location, new App(e->location,
        new Lambda(e->location, "_", new Lambda(e->location, "_", new Prim(e->location, comparison), " ")),
        e), new VarRef(e->location, "_ k" + std::to_string(i))));
    match->patterns.emplace_back(AST(e->location, "LT@wake"), new VarRef(e->location, "False@wake"), nullptr);
    match->patterns.emplace_back(AST(e->location, "GT@wake"), new VarRef(e->location, "False@wake"), nullptr);
    match->patterns.emplace_back(AST(e->location, "EQ@wake"), guard, nullptr);
    guard = match;
  }
  return guard;
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

    Expr *guard = nullptr;
    if (lex.next.type == IF) {
      lex.consume();
      bool eateol = lex.next.type == INDENT;
      guard = parse_block(lex, false);
      if (eateol && expect(EOL, lex)) lex.consume();
    }

    guard = add_literal_guards(guard, state);

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
        match->args.emplace_back(new VarRef(ast.region, "_ xx"));
        out = new Lambda(region, "_ xx", match);
      } else if (ast.type) {
        DefMap *dm = new DefMap(region);
        dm->body = std::unique_ptr<Expr>(rhs);
        dm->defs.insert(std::make_pair(ast.name, DefValue(ast.region, std::unique_ptr<Expr>(
          new Ascribe(LOCATION, std::move(*ast.type), new VarRef(LOCATION, "_ typed"), ast.region)))));
        out = new Lambda(region, "_ typed", dm);
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
        name = static_cast<String*>(lit->value->get())->as_str();
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
      Match *out = new Match(l);
      out->args.emplace_back(condE);
      out->patterns.emplace_back(AST(l, "True@wake"),  thenE, nullptr);
      out->patterns.emplace_back(AST(l, "False@wake"), elseE, nullptr);
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
      case COLON: {
        op_type op = op_precedence(lex.id().c_str());
        if (op.p < p) return lhs;
        lex.consume();
        ASTState state(true, false);
        AST signature = parse_ast(op.p + op.l, lex, state);
        if (check_constructors(signature)) lex.fail = true;
        Location location = lhs->location;
        location.end = signature.region.end;
        lhs = new Ascribe(location, std::move(signature), lhs, lhs->location);
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
  std::vector<ScopedTypeVar> typeVars;
  Definition(const std::string &name_, const Location &location_, Expr *body_, std::vector<ScopedTypeVar> &&typeVars_)
   : name(name_), location(location_), body(body_), typeVars(std::move(typeVars_)) { }
  Definition(const std::string &name_, const Location &location_, Expr *body_)
   : name(name_), location(location_), body(body_) { }
};

static void extract_def(std::vector<Definition> &out, long index, AST &&ast, const std::vector<ScopedTypeVar> &typeVars, Expr *body) {
  std::string key = "_ extract " + std::to_string(++index);
  out.emplace_back(key, ast.token, body, std::vector<ScopedTypeVar>(typeVars));
  for (auto &m : ast.args) {
    AST pattern(ast.region, std::string(ast.name));
    pattern.type = std::move(ast.type);
    std::string mname("_" + m.name);
    for (auto &n : ast.args) {
      pattern.args.push_back(AST(m.token, "_"));
      if (&n == &m) {
        AST &back = pattern.args.back();
        back.name = mname;
        back.type = std::move(m.type);
      }
    }
    Match *match = new Match(m.token);
    match->args.emplace_back(new VarRef(body->location, key));
    match->patterns.emplace_back(std::move(pattern), new VarRef(m.token, mname), nullptr);
    if (Lexer::isUpper(m.name.c_str()) || Lexer::isOperator(m.name.c_str())) {
      extract_def(out, index, std::move(m), typeVars, match);
    } else {
      out.emplace_back(m.name, m.token, match, std::vector<ScopedTypeVar>(typeVars));
    }
  }
}

static std::vector<Definition> parse_def(Lexer &lex, long index, bool target, bool publish) {
  lex.consume();

  ASTState state(false, false);
  AST ast = parse_ast(0, lex, state);
  if (ast.name.empty()) ast.name = "undef";
  std::string name = std::move(ast.name);
  ast.name.clear();
  if (check_constructors(ast)) lex.fail = true;

  bool extract = Lexer::isUpper(name.c_str()) || (state.topParen && Lexer::isOperator(name.c_str()));
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

  // Record type variables introduced by the def before we rip the ascription appart
  std::vector<ScopedTypeVar> typeVars;
  ast.typeVars(typeVars);

  std::vector<Definition> out;
  if (extract) {
    ast.name = std::move(name);
    extract_def(out, index, std::move(ast), typeVars, body);
    return out;
  }

  // do we need a pattern match? lower / wildcard are ok
  bool pattern = false;
  bool typed = false;
  for (auto &x : ast.args) {
    pattern |= Lexer::isOperator(x.name.c_str()) || Lexer::isUpper(x.name.c_str());
    typed |= x.type;
  }

  optional<AST> type = std::move(ast.type);
  std::vector<std::pair<std::string, Location> > args;
  if (pattern) {
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
  } else if (typed) {
    DefMap *dm = new DefMap(fn);
    dm->body = std::unique_ptr<Expr>(body);
    for (size_t i = 0; i < ast.args.size(); ++i) {
      AST &arg = ast.args[i];
      args.emplace_back(arg.name, arg.token);
      if (arg.type) {
        dm->defs.insert(std::make_pair("_type " + arg.name, DefValue(arg.region, std::unique_ptr<Expr>(
          new Ascribe(LOCATION, std::move(*arg.type), new VarRef(LOCATION, arg.name), arg.token)))));
      }
    }
    body = dm;
  } else {
    // no pattern; simple lambdas for the arguments
    for (auto &x : ast.args) args.emplace_back(x.name, x.token);
  }

  if (type)
    body = new Ascribe(LOCATION, std::move(*type), body, body->location);

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

  out.emplace_back(name, ast.token, body, std::move(typeVars));
  return out;
}

static void bind_global(const Definition &def, Symbols *globals, Lexer &lex) {
  if (!globals || def.name == "_") return;

  globals->defs.insert(std::make_pair(def.name, SymbolSource(def.location, SYM_LEAF)));
  // Duplicate globals will be detected as file-local conflicts
}

static void bind_export(const Definition &def, Symbols *exports, Lexer &lex) {
  if (!exports || def.name == "_") return;

  exports->defs.insert(std::make_pair(def.name, SymbolSource(def.location, SYM_LEAF)));
  // Duplicate exports will be detected as file-local conflicts
}

static void bind_def(Lexer &lex, DefMap &map, Definition &&def, Symbols *exports, Symbols *globals) {
  bind_global(def, globals, lex);
  bind_export(def, exports, lex);

  if (def.name == "_")
    def.name = "_" + std::to_string(map.defs.size()) + " _";

  Location l = def.body->location;
  auto out = map.defs.insert(std::make_pair(std::move(def.name), DefValue(
    def.location, std::move(def.body), std::move(def.typeVars))));

  if (!out.second) {
    std::cerr << "Duplicate definition "
      << out.first->first << " at "
      << out.first->second.body->location.text() << " and "
      << l.text() << std::endl;
    lex.fail = true;
  }
}

static void bind_type(Lexer &lex, Package &package, const std::string &name, const Location &location, Symbols *exports, Symbols *globals) {
  if (globals) globals->types.insert(std::make_pair(name, SymbolSource(location, SYM_LEAF)));
  if (exports) exports->types.insert(std::make_pair(name, SymbolSource(location, SYM_LEAF)));

  auto it = package.package.types.insert(std::make_pair(name, SymbolSource(location, SYM_LEAF)));
  if (!it.second) {
    std::cerr << "Duplicate type "
      << it.first->first << " at "
      << it.first->second.location.text() << " and "
      << location.text() << std::endl;
    lex.fail = true;
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
      state.topParen = false;
      return out;
    }
    // Terminals
    case ID: {
      AST out(lex.next.location, lex.id());
      if (out.name == "_" && state.type) {
        std::cerr << "Type signatures may not include _ at "
          << lex.next.location.file() << std::endl;
        lex.fail = true;
      }
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
      state.topParen = true;
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
        state.topParen = false;
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
        state.topParen = false;
        break;
      }
      case COLON: {
        op_type op = op_precedence(lex.id().c_str());
        if (op.p < p) return lhs;
        if (state.type) {
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
        } else {
          lex.consume();
          state.type = true;
          lhs.type = optional<AST>(new AST(parse_ast(op.p + op.l, lex, state)));
          state.type = false;
        }
        break;
      }
      default: {
        return lhs;
      }
    }
  }
}

std::shared_ptr<Sum> Boolean;
std::shared_ptr<Sum> Order;
std::shared_ptr<Sum> List;
std::shared_ptr<Sum> Unit;
std::shared_ptr<Sum> Pair;
std::shared_ptr<Sum> Result;
std::shared_ptr<Sum> JValue;

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
    std::cerr << "Required data type Boolean@wake not defined." << std::endl;
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
    std::cerr << "Required data type Order@wake not defined." << std::endl;
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
    std::cerr << "Required data type List@wake not defined." << std::endl;
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
    std::cerr << "Required data type Unit@wake not defined." << std::endl;
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
    std::cerr << "Required data type Pair@wake not defined." << std::endl;
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
    std::cerr << "Required data type Result@wake not defined." << std::endl;
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
    std::cerr << "Required data type JValue@wake not defined." << std::endl;
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

static void check_special(Lexer &lex, const std::string &name, const std::shared_ptr<Sum> &sump) {
  if (name == "Boolean") Boolean = sump;
  if (name == "Order")   Order = sump;
  if (name == "List")    List = sump;
  if (name == "Unit")    Unit = sump;
  if (name == "Pair")    Pair = sump;
  if (name == "Result")  Result = sump;
  if (name == "JValue")  JValue = sump;
}

static void parse_topic(Lexer &lex, Package &package, Symbols *exports, Symbols *globals, bool exportb, bool globalb) {
  File &file = package.files.back();
  lex.consume();

  auto id = get_arg_loc(lex);
  if (!Lexer::isLower(id.first.c_str())) {
    std::cerr << "Topic identifier '" << id.first
      << "' is not lower-case at "
      << id.second.file() << std::endl;
    lex.fail = true;
  }

  if (expect(COLON, lex)) lex.consume();

  ASTState state(true, false);
  AST def = parse_ast(0, lex, state);
  if (check_constructors(def)) lex.fail = true;

  // Confirm there are no open type variables
  TypeMap ids;
  TypeVar x;
  x.setDOB();
  if (!def.unify(x, ids)) lex.fail = true;

  if (expect(EOL, lex)) lex.consume();

  auto it = file.topics.insert(std::make_pair(id.first, Topic(id.second, std::move(def))));
  if (!it.second) {
    std::cerr << "Duplicate topic " << id.first
      << " at " << id.second.file() << std::endl;
    lex.fail = true;
  }

  if (exportb) exports->topics.insert(std::make_pair(id.first, SymbolSource(id.second, SYM_LEAF)));
  if (globalb) globals->topics.insert(std::make_pair(id.first, SymbolSource(id.second, SYM_LEAF)));
}

#define FLAG_GLOBAL 1
#define FLAG_EXPORT 2
static void parse_tuple(Lexer &lex, Package &package, Symbols *exports, Symbols *globals, bool exportb, bool globalb) {
  DefMap &map = *package.files.back().content;
  AST def = parse_type_def(lex);
  if (!def) return;

  if (Lexer::isOperator(def.name.c_str())) {
    std::cerr << "Tuple name must not be operator, was "
      << def.name << " at "
      << def.token.file() << std::endl;
    lex.fail = true;
    return;
  }

  std::string name = def.name;
  std::shared_ptr<Sum> sump = std::make_shared<Sum>(std::move(def));
  AST tuple(sump->token, std::string(sump->name));
  tuple.region = sump->region;
  std::vector<int> members;

  if (!expect(INDENT, lex)) return;
  lex.consume();
  expect(EOL, lex);
  lex.consume();

  bool repeat = true;
  bool exportt = exportb, globalt = globalb;
  while (repeat) {
    int flags = 0;
    bool quals = true;
    while (quals) switch (lex.next.type) {
      case GLOBAL: lex.consume(); flags |= FLAG_GLOBAL; globalt = true; break;
      case EXPORT: lex.consume(); flags |= FLAG_EXPORT; exportt = true; break;
      default: quals = false; break;
    }

    ASTState state(true, false);
    AST member = parse_ast(0, lex, state);
    if (check_constructors(member)) lex.fail = true;
    if (member) {
      tuple.args.push_back(member);
      members.push_back(flags);
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

  sump->addConstructor(std::move(tuple));

  Constructor &c = sump->members.back();
  Expr *construct = new Construct(c.ast.token, sump, &c);
  for (size_t i = c.ast.args.size(); i > 0; --i)
    construct = new Lambda(c.ast.token, c.ast.args[i-1].tag, construct);

  bind_type(lex, package, sump->name, sump->token, exportt?exports:nullptr, globalt?globals:nullptr);
  bind_def(lex, map, Definition(c.ast.name, c.ast.token, construct), exportb?exports:nullptr, globalb?globals:nullptr);

  if (package.name == "wake") check_special(lex, name, sump);

  // Create get/set/edit helper methods
  size_t outer = 0;
  for (size_t i = 0; i < members.size(); ++i) {
    std::string &mname = c.ast.args[i].tag;
    Location memberToken = c.ast.args[i].region;
    bool globalb = (members[i] & FLAG_GLOBAL) != 0;
    bool exportb = (members[i] & FLAG_EXPORT) != 0;
    if (mname.empty()) continue;

    // Implement get methods
    std::string get = "get" + name + mname;
    Expr *getfn = new Lambda(memberToken, "_", new Get(memberToken, sump, &c, i));
    getfn->flags |= FLAG_SYNTHETIC;
    bind_def(lex, map, Definition(get, memberToken, getfn), exportb?exports:nullptr, globalb?globals:nullptr);

    // Implement edit methods
    DefMap *editmap = new DefMap(memberToken);
    editmap->body = std::unique_ptr<Expr>(new Construct(memberToken, sump, &c));
    for (size_t inner = 0; inner < members.size(); ++inner) {
      Expr *select = new Get(memberToken, sump, &c, inner);
      if (inner == outer)
        select =
          new App(memberToken,
            new VarRef(memberToken, "fn" + mname),
            new App(memberToken,
              new Lambda(memberToken, "_", select),
              new VarRef(memberToken, "_ x")));
      std::string x = std::to_string(members.size()-inner);
      std::string name = "_ a" + std::string(4 - x.size(), '0') + x;
      editmap->defs.insert(std::make_pair(name,
        DefValue(memberToken, std::unique_ptr<Expr>(select))));
    }

    std::string edit = "edit" + name + mname;
    Expr *editfn =
      new Lambda(memberToken, "fn" + mname,
        new Lambda(memberToken, "_ x", editmap));

    editfn->flags |= FLAG_SYNTHETIC;
    bind_def(lex, map, Definition(edit, memberToken, editfn), exportb?exports:nullptr, globalb?globals:nullptr);

    // Implement set methods
    DefMap *setmap = new DefMap(memberToken);
    setmap->body = std::unique_ptr<Expr>(new Construct(memberToken, sump, &c));
    for (size_t inner = 0; inner < members.size(); ++inner) {
      std::string x = std::to_string(members.size()-inner);
      std::string name = "_ a" + std::string(4 - x.size(), '0') + x;
      setmap->defs.insert(std::make_pair(name,
        DefValue(memberToken, std::unique_ptr<Expr>(
          (inner == outer)
          ? static_cast<Expr*>(new VarRef(memberToken, mname))
          : static_cast<Expr*>(new Get(memberToken, sump, &c, inner))))));
    }

    std::string set = "set" + name + mname;
    Expr *setfn =
      new Lambda(memberToken, mname,
        new Lambda(memberToken, "_ x", setmap));

    setfn->flags |= FLAG_SYNTHETIC;
    bind_def(lex, map, Definition(set, memberToken, setfn), exportb?exports:nullptr, globalb?globals:nullptr);

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

static void parse_data(Lexer &lex, Package &package, Symbols *exports, Symbols *globals, bool exportb, bool globalb) {
  DefMap &map = *package.files.back().content;

  AST def = parse_type_def(lex);
  if (!def) return;

  auto sump = std::make_shared<Sum>(std::move(def));

  if (lex.next.type == INDENT) {
    lex.consume();
    if (expect(EOL, lex)) lex.consume();

    bool repeat = true;
    while (repeat) {
      parse_data_elt(lex, *sump);
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
    parse_data_elt(lex, *sump);
    lex.consume();
  }

  bind_type(lex, package, sump->name, sump->token, exportb?exports:nullptr, globalb?globals:nullptr);
  for (auto &c : sump->members) {
    Expr *construct = new Construct(c.ast.token, sump, &c);
    for (size_t i = 0; i < c.ast.args.size(); ++i)
      construct = new Lambda(c.ast.token, "_", construct);

    bind_def(lex, map, Definition(c.ast.name, c.ast.token, construct), exportb?exports:nullptr, globalb?globals:nullptr);
  }

  if (package.name == "wake") check_special(lex, sump->name, sump);
}

static void parse_import(const std::string &pkgname, DefMap &map, Lexer &lex) {
  Symbols::SymbolMap *target;
  const char *kind;

  // Special case for wildcard import
  if (lex.next.type == ID && lex.id() == "_") {
    lex.consume();
    map.imports.import_all.emplace_back(pkgname);
    if (expect(EOL, lex)) lex.consume();
    return;
  }

  switch (lex.next.type) {
    case DEF:
      lex.consume();
      target = &map.imports.defs;
      kind = "definition";
      break;
    case TYPE:
      lex.consume();
      target = &map.imports.types;
      kind = "type";
      break;
    case TOPIC:
      lex.consume();
      target = &map.imports.topics;
      kind = "topic";
      break;
    default:
      target = &map.imports.mixed;
      kind = "symbol";
      break;
  }

  bool unary = false;
  bool binary = false;
  switch (lex.next.type) {
    case UNARY:
      lex.consume();
      unary = true;
      break;
    case BINARY:
      lex.consume();
      binary = true;
      break;
    default:
      break;
  }

  while (lex.next.type == ID || lex.next.type == OPERATOR) {
    SymbolType idop = lex.next.type;
    std::string source, name = lex.id();
    Location location = lex.next.location;
    lex.consume();

    if (lex.next.type == EQUALS) {
      lex.consume();
      if (lex.next.type == idop) {
        source = lex.id() + "@" + pkgname;
        lex.consume();
      } else {
        std::cerr << "Was expecting an "
          << symbolTable[idop] << ", got an "
          << symbolTable[lex.next.type] << " at "
          << lex.next.location.text() << std::endl;
        lex.fail = true;
      }
    } else {
      source = name + "@" + pkgname;
    }

    if (name == "_" || source.substr(0,2) == "_@") {
      std::cerr << "Import of _ must immediately follow the import keyword at "
        << location.text() << std::endl;
      lex.fail = true;
      continue;
    }

    if (idop == OPERATOR) {
      if (unary) {
        name = "unary " + name;
        source = "unary " + source;
      } else if (binary) {
        name = "binary " + name;
        source = "binary " + source;
      } else {
        name = "op " + name;
        source = "op " + source;
      }
    }

    auto it = target->insert(std::make_pair(std::move(name), SymbolSource(location, source)));
    if (!it.second) {
      std::cerr << "Duplicate imported "
        << kind << " '" << it.first->first << "' at "
        << it.first->second.location.text() << " and "
        << location.text() << std::endl;
      lex.fail = true;
    }
  }

  if (expect(EOL, lex)) lex.consume();
}

static void parse_export(const std::string &pkgname, Package &package, Lexer &lex) {
  Symbols::SymbolMap *exports, *local;
  const char *kind;
  switch (lex.next.type) {
    case DEF:
      lex.consume();
      exports = &package.exports.defs;
      local = &package.files.back().local.defs;
      kind = "definition";
      break;
    case TYPE:
      lex.consume();
      exports = &package.exports.types;
      local = &package.files.back().local.types;
      kind = "type";
      break;
    case TOPIC:
      lex.consume();
      exports = &package.exports.topics;
      local = &package.files.back().local.topics;
      kind = "topic";
      break;
    default:
      exports = nullptr;
      local = nullptr;
      kind = nullptr;
      std::cerr << "Was expecting a DEF/TYPE/TOPIC, got a "
        << symbolTable[lex.next.type] << " at "
        << lex.next.location.text() << std::endl;
      lex.fail = true;
      break;
  }

  bool unary = false;
  bool binary = false;
  switch (lex.next.type) {
    case UNARY:
      lex.consume();
      unary = true;
      break;
    case BINARY:
      lex.consume();
      binary = true;
      break;
    default:
      break;
  }

  while (lex.next.type == ID || lex.next.type == OPERATOR) {
    SymbolType idop = lex.next.type;
    std::string source, name = lex.id();
    Location location = lex.next.location;
    lex.consume();

    if (lex.next.type == EQUALS) {
      lex.consume();
      if (lex.next.type == idop) {
        source = lex.id() + "@" + pkgname;
        location.end = lex.next.location.end;
        lex.consume();
      } else {
        std::cerr << "Was expecting an "
          << symbolTable[idop] << ", got an "
          << symbolTable[lex.next.type] << " at "
          << lex.next.location.text() << std::endl;
        lex.fail = true;
        continue;
      }
    } else {
      source = name + "@" + pkgname;
    }

    if (name == "_" || source.substr(0,2) == "_@") {
      std::cerr << "Cannot re-export _ from another package at "
        << location.text() << std::endl;
      lex.fail = true;
      continue;
    }

    if (idop == OPERATOR) {
      if (unary) {
        name = "unary " + name;
        source = "unary " + source;
      } else if (binary) {
        name = "binary " + name;
        source = "binary " + source;
      } else {
        std::cerr << "Cannot re-export an operator without specifying unary/binary at "
          << location.text() << std::endl;
        lex.fail = true;
        continue;
      }
    }

    if (!exports) continue;

    exports->insert(std::make_pair(name, SymbolSource(location, source)));
    // duplciates will be detected as file-local

    auto it = local->insert(std::make_pair(name, SymbolSource(location, source)));
    if (!it.second) {
      std::cerr << "Duplicate file-local "
        << kind << " '"
        << name << "' at "
        << it.first->second.location.text() << " and "
        << location.text() << std::endl;
      lex.fail = true;
    }
  }

  if (expect(EOL, lex)) lex.consume();
}

static void parse_from_import(DefMap &map, Lexer &lex) {
  lex.consume();
  auto id = get_arg_loc(lex);
  if (expect(IMPORT, lex)) lex.consume();
  parse_import(id.first, map, lex);
}

static void parse_from_importexport(Package &package, Lexer &lex) {
  lex.consume();
  auto id = get_arg_loc(lex);

  switch (lex.next.type) {
    case IMPORT:
      lex.consume();
      parse_import(id.first, *package.files.back().content, lex);
      break;
    case EXPORT:
      lex.consume();
      parse_export(id.first, package, lex);
      break;
    default:
      std::cerr << "Was expecting an IMPORT/EXPORT, got a "
        << symbolTable[lex.next.type] << " at "
        << lex.next.location.text() << std::endl;
      lex.fail = true;
  }
}

static void parse_decl(Lexer &lex, DefMap &map, Symbols *exports, Symbols *globals) {
  switch (lex.next.type) {
    case FROM: {
      parse_from_import(map, lex);
      break;
    }
    case DEF: {
      for (auto &def : parse_def(lex, map.defs.size(), false, false))
         bind_def(lex, map, std::move(def), exports, globals);
      break;
    }
    case TARGET: {
      auto defs = parse_def(lex, 0, true, false);
      auto &def = defs.front();
      Location l = LOCATION;
      std::stringstream s;
      s << def.body->location.text();
      bind_def(lex, map, Definition("table " + def.name, l,
          new App(l, new Lambda(l, "_", new Prim(l, "tnew"), " "),
          new Literal(l, String::literal(lex.heap, s.str()), &String::typeVar))),
        nullptr, nullptr);
      bind_def(lex, map, std::move(def), exports, globals);
      break;
    }
    default: {
      // should be unreachable
      break;
    }
  }
}

static Expr *parse_block_body(Lexer &lex);
static Expr *parse_require(Lexer &lex) {
  Location l = lex.next.location;
  lex.consume();

  ASTState state(false, true);
  AST ast = parse_ast(0, lex, state);
  auto guard = add_literal_guards(nullptr, state);

  expect(EQUALS, lex);
  lex.consume();

  Expr *rhs = parse_block(lex, false);
  bool eol = lex.next.type == EOL;
  if (eol) lex.consume();

  Expr *otherwise = nullptr;
  if (lex.next.type == ELSE) {
    lex.consume();
    otherwise = parse_block(lex, false);
    if (expect(EOL, lex)) lex.consume();
  } else if (!eol) {
    expect(EOL, lex);
  }

  Expr *block = parse_block_body(lex);

  Match *out = new Match(l, true);
  out->args.emplace_back(rhs);
  out->patterns.emplace_back(std::move(ast), block, guard);
  out->location.end = block->location.end;
  out->otherwise = std::unique_ptr<Expr>(otherwise);

  return out;
}

static Expr *parse_block_body(Lexer &lex) {
  DefMap *map = new DefMap(lex.next.location);

  bool repeat = true;
  while (repeat) {
    switch (lex.next.type) {
      case FROM:
      case TARGET:
      case DEF: {
        parse_decl(lex, *map, nullptr, nullptr);
        break;
      }
      default: {
        repeat = false;
        break;
      }
    }
  }

  std::unique_ptr<Expr> body;
  if (lex.next.type == REQUIRE) {
    body = std::unique_ptr<Expr>(parse_require(lex));
  } else {
    body = std::unique_ptr<Expr>(relabel_anon(parse_binary(0, lex, true)));
  }

  if (map->defs.empty() && map->imports.empty()) {
    delete map;
    return body.release();
  } else {
    map->body = std::move(body);

    map->location.end = map->body->location.end;
    map->location.start.bytes -= (map->location.start.column-1);
    map->location.start.column = 1;

    return map;
  }
}

static Expr *parse_block(Lexer &lex, bool multiline) {
  TRACE("BLOCK");

  if (lex.next.type == INDENT) {
    lex.consume();
    if (expect(EOL, lex)) lex.consume();
    Expr *map = parse_block_body(lex);
    if (expect(DEDENT, lex)) lex.consume();
    return map;
  } else {
    return relabel_anon(parse_binary(0, lex, multiline));
  }
}

Expr *parse_expr(Lexer &lex) {
  return parse_binary(0, lex, false);
}

static void parse_package(Package &package, Lexer &lex) {
  lex.consume();
  auto id = get_arg_loc(lex);
  if (expect(EOL, lex)) lex.consume();
  if (id.first == "builtin") {
    std::cerr << "Package name 'builtin' is illegal." << std::endl;
    lex.fail = true;
  } else if (package.name.empty()) {
    package.name = id.first;
  } else {
    std::cerr << "Package name redefined at " << id.second.text()
      << " from '" << package.name << "'" << std::endl;
    lex.fail = true;
  }
}

static void no_tags(Lexer &lex, bool exportb, bool globalb) {
  if (exportb) {
    std::cerr << "Cannot prefix "
      << symbolTable[lex.next.type] << " with 'export' at "
      << lex.next.location.text() << std::endl;
    lex.fail = true;
  }
  if (globalb) {
    std::cerr << "Cannot prefix "
      << symbolTable[lex.next.type] << " with 'global' at "
      << lex.next.location.text() << std::endl;
    lex.fail = true;
  }
}

const char *parse_top(Top &top, Lexer &lex) {
  TRACE("TOP");

  std::unique_ptr<Package> package(new Package);
  package->files.resize(1);
  File &file = package->files.back();
  file.content = std::unique_ptr<DefMap>(new DefMap(lex.next.location));
  DefMap &map = *file.content;
  Symbols globals;

  if (lex.next.type == EOL) lex.consume();
  bool repeat  = true;
  bool exportb = false;
  bool globalb = false;
  while (repeat) {
    switch (lex.next.type) {
      case GLOBAL: {
        lex.consume();
        globalb = true;
        break;
      }
      case EXPORT: {
        lex.consume();
        exportb = true;
        break;
      }
      case PACKAGE: {
        no_tags(lex, exportb, globalb);
        parse_package(*package, lex);
        exportb = false;
        globalb = false;
        break;
      }
      case FROM: {
        no_tags(lex, exportb, globalb);
        parse_from_importexport(*package, lex);
        exportb = false;
        globalb = false;
        break;
      }
      case TOPIC: {
        parse_topic(lex, *package, &package->exports, &globals, exportb, globalb);
        exportb = false;
        globalb = false;
        break;
      }
      case TUPLE: {
        parse_tuple(lex, *package, &package->exports, &globals, exportb, globalb);
        exportb = false;
        globalb = false;
        break;
      }
      case DATA: {
        parse_data(lex, *package, &package->exports, &globals, exportb, globalb);
        exportb = false;
        globalb = false;
        break;
      }
      case PUBLISH: {
        no_tags(lex, exportb, globalb);
        for (auto &def : parse_def(lex, 0, false, true)) {
          file.pubs.emplace_back(def.name, DefValue(def.location, std::move(def.body)));
        }
        exportb = false;
        globalb = false;
        break;
      }
      case DEF:
      case TARGET: {
        parse_decl(lex, map, exportb?&package->exports:nullptr, globalb?&globals:nullptr);
        exportb = false;
        globalb = false;
        break;
      }
      default: {
        repeat = false;
        break;
      }
    }
  }

  map.location.end = lex.next.location.start;
  expect(END, lex);

  // Set a default import
  if (file.content->imports.empty())
    file.content->imports.import_all.push_back("wake");

  // Set a default package name
  static size_t anon_file = 0;
  if (package->name.empty()) {
    package->name = std::to_string(++anon_file);
  }

  package->exports.setpkg(package->name);
  globals.setpkg(package->name);

  if (!top.globals.join(globals, "global")) lex.fail = true;

  // localize all top-level symbols
  DefMap::Defs defs(std::move(map.defs));
  map.defs.clear();
  for (auto &def : defs) {
    auto name = def.first + "@" + package->name;
    auto it = file.local.defs.insert(std::make_pair(def.first, SymbolSource(def.second.location, name, SYM_LEAF)));
    if (!it.second) {
      if (it.first->second.qualified == name) {
        it.first->second.location = def.second.location;
        it.first->second.flags |= SYM_LEAF;
        package->exports.defs.find(def.first)->second.flags |= SYM_LEAF;
      } else {
        std::cerr << "Duplicate file-local definition "
          << def.first << " at "
          << it.first->second.location.text() << " and "
          << def.second.location.text() << std::endl;
        lex.fail = true;
      }
    }
    map.defs.insert(std::make_pair(std::move(name), std::move(def.second)));
  }

  // localize all topics
  for (auto &topic : file.topics) {
    auto name = topic.first + "@" + package->name;
    auto it = file.local.topics.insert(std::make_pair(topic.first, SymbolSource(topic.second.location, name, SYM_LEAF)));
    if (!it.second) {
      if (it.first->second.qualified == name) {
        it.first->second.location = topic.second.location;
        it.first->second.flags |= SYM_LEAF;
        package->exports.topics.find(topic.first)->second.flags |= SYM_LEAF;
      } else {
        std::cerr << "Duplicate file-local topic "
          << topic.first << " at "
          << it.first->second.location.text() << " and "
          << topic.second.location.text() << std::endl;
        lex.fail = true;
      }
    }
  }

  // localize all types
  for (auto &type : package->package.types) {
    auto name = type.first + "@" + package->name;
    auto it = file.local.types.insert(std::make_pair(type.first, SymbolSource(type.second.location, name, SYM_LEAF)));
    if (!it.second) {
      if (it.first->second.qualified == name) {
        it.first->second.location = type.second.location;
        it.first->second.flags |= SYM_LEAF;
        package->exports.types.find(type.first)->second.flags |= SYM_LEAF;
      } else {
        std::cerr << "Duplicate file-local type "
          << type.first << " at "
          << it.first->second.location.text() << " and "
          << type.second.location.text() << std::endl;
        lex.fail = true;
      }
    }
  }

  auto it = top.packages.insert(std::make_pair(package->name, nullptr));
  if (it.second) {
    package->package = file.local;
    it.first->second = std::move(package);
  } else {
    if (!it.first->second->package.join(file.local, "package-local")) lex.fail = true;
    it.first->second->exports.join(package->exports, nullptr);
    // duplicated export already reported as package-local duplicate
    it.first->second->files.emplace_back(std::move(file));
  }

  return it.first->second->name.c_str();
}

Expr *parse_command(Lexer &lex) {
  TRACE("COMMAND");
  if (lex.next.type == EOL) lex.consume();
  auto out = parse_block(lex, false);
  expect(END, lex);
  return out;
}
