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

#include <assert.h>
#include <re2/re2.h>

#include <sstream>
#include <set>
#include <algorithm>

#include "util/fragment.h"
#include "util/diagnostic.h"
#include "util/file.h"
#include "parser/parser.h"
#include "parser/cst.h"
#include "parser/lexer.h"
#include "parser/syntax.h"
#include "types/sums.h"
#include "types/data.h"
#include "expr.h"
#include "todst.h"

static CPPFile cppFile(__FILE__);

static Expr *dst_expr(CSTElement expr);

static std::string getIdentifier(CSTElement element) {
  assert (element.id() == CST_ID || element.id() == CST_OP);
  StringSegment ti = element.firstChildElement().segment();
  return relex_id(ti.start, ti.end);
}

static void dst_package(CSTElement topdef, Package &package) {
  CSTElement child = topdef.firstChildNode();
  std::string id = getIdentifier(child);

  if (id == "builtin") {
    ERROR(child.fragment().location(), "package name 'builtin' is illegal.");
  } else if (package.name.empty()) {
    package.name = id;
  } else {
    ERROR(topdef.fragment().location(), "package name redefined from '" << package.name << "' to '" << id << "'");
  }
}

struct ImportArity {
  bool unary;
  bool binary;
};

static ImportArity dst_arity(CSTElement &child) {
  ImportArity out;
  out.unary  = false;
  out.binary = false;

  if (child.id() == CST_ARITY) {
    switch (child.firstChildElement().id()) {
    case TOKEN_KW_UNARY:  out.unary  = true; break;
    case TOKEN_KW_BINARY: out.binary = true; break;
    }
    child.nextSiblingNode();
  }

  return out;
}

static void prefix_op(ImportArity ia, std::string &name) {
  if (ia.unary) {
    name = "unary " + name;
  } else if (ia.binary) {
    name = "binary " + name;
  } else {
    name = "op " + name;
  }
}

static void dst_import(CSTElement topdef, DefMap &map) {
  CSTElement child = topdef.firstChildNode();

  std::string pkgname = getIdentifier(child);
  child.nextSiblingNode();

  const char *kind = "symbol";
  Symbols::SymbolMap *target = &map.imports.mixed;

  if (child.id() == CST_KIND) {
    switch (child.firstChildElement().id()) {
    case TOKEN_KW_DEF:   kind = "definition"; target = &map.imports.defs;   break;
    case TOKEN_KW_TYPE:  kind = "type";       target = &map.imports.types;  break;
    case TOKEN_KW_TOPIC: kind = "topic";      target = &map.imports.topics; break;
    }
    child.nextSiblingNode();
  }

  ImportArity ia = dst_arity(child);

  // Special case for wildcard import
  if (child.empty()) {
    map.imports.import_all.emplace_back(pkgname, topdef.fragment());
    return;
  }

  for (; !child.empty(); child.nextSiblingNode()) {
    CSTElement ideq = child.firstChildNode();

    uint8_t idop1 = ideq.id(), idop2;
    std::string name = getIdentifier(ideq);
    ideq.nextSiblingNode();

    std::string source;
    if (ideq.empty()) {
      idop2 = idop1;
      source = name + "@" + pkgname;
    } else if ((idop2 = ideq.id()) == idop1 || ia.binary || ia.unary) {
      source = getIdentifier(ideq) + "@" + pkgname;
    } else {
      idop1 = idop2;
      name = getIdentifier(ideq);
      source = name + "@" + pkgname;

      ERROR(child.fragment().location(), "keyword 'binary' or 'unary' required when changing symbol type for " << child.segment());
    }

    if (idop1 == CST_OP) prefix_op(ia, name);
    if (idop2 == CST_OP) prefix_op(ia, source);

    auto it = target->insert(std::make_pair(std::move(name), SymbolSource(child.fragment(), source)));
    if (!it.second) {
      ERROR(child.fragment().location(),
        kind << " '" << it.first->first
        << "' was previously imported at " << it.first->second.fragment.location());
    }
  }
}

static void dst_export(CSTElement topdef, Package &package) {
  CSTElement child = topdef.firstChildNode();

  std::string pkgname = getIdentifier(child);
  child.nextSiblingNode();

  const char *kind = nullptr;
  Symbols::SymbolMap *exports = nullptr;
  Symbols::SymbolMap *local   = nullptr;

  if (child.id() == CST_KIND) {
    auto &e = package.exports;
    auto &l = package.files.back().local;

    switch (child.firstChildElement().id()) {
    case TOKEN_KW_DEF:   kind = "definition"; exports = &e.defs;   local = &l.defs;   break;
    case TOKEN_KW_TYPE:  kind = "type";       exports = &e.types;  local = &l.types;  break;
    case TOKEN_KW_TOPIC: kind = "topic";      exports = &e.topics; local = &l.topics; break;
    }
    child.nextSiblingNode();
  }

  if (!kind) {
    ERROR(child.fragment().location(), "from ... export must be followed by 'def', 'type', or 'topic'");
    return;
  }

  ImportArity ia = dst_arity(child);
  for (; !child.empty(); child.nextSiblingNode()) {
    CSTElement ideq = child.firstChildNode();

    uint8_t idop1 = ideq.id(), idop2;
    std::string name = getIdentifier(ideq);
    ideq.nextSiblingNode();

    std::string source;
    if (ideq.empty()) {
      idop2 = idop1;
      source = name + "@" + pkgname;
    } else {
      idop2 = ideq.id();
      source = getIdentifier(ideq) + "@" + pkgname;
    }

    if ((idop1 == CST_OP || idop2 == CST_OP) && !(ia.unary || ia.binary)) {
      ERROR(child.fragment().location(), "export of " << child.segment() << " must specify 'unary' or 'binary'");
      continue;
    }

    if (idop1 == CST_OP) prefix_op(ia, name);
    if (idop2 == CST_OP) prefix_op(ia, source);

    exports->insert(std::make_pair(name, SymbolSource(child.fragment(), source)));
    // duplciates will be detected as file-local

    auto it = local->insert(std::make_pair(name, SymbolSource(child.fragment(), source)));
    if (!it.second) {
      ERROR(child.fragment().location(),
        kind << " '" << name
        << "' was previously defined at " << it.first->second.fragment.location());
    }
  }
}

struct TopFlags {
  bool exportf;
  bool globalf;
};

static TopFlags dst_flags(CSTElement &child) {
  TopFlags out;

  if (child.id() == CST_FLAG_GLOBAL) {
    out.globalf = true;
    child.nextSiblingNode();
  } else {
    out.globalf = false;
  }

  if (child.id() == CST_FLAG_EXPORT) {
    out.exportf = true;
    child.nextSiblingNode();
  } else {
    out.exportf = false;
  }

  return out;
}

static AST dst_type(CSTElement root) {
  switch (root.id()) {
    case CST_ASCRIBE: {
      CSTElement child = root.firstChildNode();
      AST lhs = dst_type(child);
      child.nextSiblingNode();
      AST rhs = dst_type(child);
      if (!lhs.args.empty() || !lhs.tag.empty() || lex_kind(lhs.name) == OPERATOR) {
        ERROR(lhs.region.location(), "tag-name for a type must be a simple identifier, not " << root.firstChildNode().segment());
        return rhs;
      } else if (rhs.tag.empty()) {
        rhs.tag = std::move(lhs.name);
        rhs.region = root.fragment();
        return rhs;
      } else {
        ERROR(lhs.region.location(), "type " << rhs.region.segment() << " already has a tag-name");
        return rhs;
      }
    }
    case CST_BINARY: {
      CSTElement child = root.firstChildNode();
      AST lhs = dst_type(child);
      child.nextSiblingNode();
      std::string op = "binary " + getIdentifier(child);
      auto fragment = child.fragment();
      child.nextSiblingNode();
      AST rhs = dst_type(child);
      std::vector<AST> args;
      args.emplace_back(std::move(lhs));
      args.emplace_back(std::move(rhs));
      AST out(fragment, std::move(op), std::move(args));
      out.region = root.fragment();
      return out;
    }
    case CST_UNARY: {
      CSTElement child = root.firstChildNode();
      std::vector<AST> args;
      if (child.id() != CST_OP) {
        args.emplace_back(dst_type(child));
        child.nextSiblingNode();
      }
      std::string op = "unary " + getIdentifier(child);
      auto fragment = child.fragment();
      child.nextSiblingNode();
      if (args.empty()) {
        args.emplace_back(dst_type(child));
        child.nextSiblingNode();
      }
      AST out(fragment, std::move(op), std::move(args));
      out.region = root.fragment();
      return out;
    }
    case CST_ID: {
      return AST(root.fragment(), getIdentifier(root));
    }
    case CST_PAREN: {
      AST out = dst_type(root.firstChildNode());
      out.region = root.fragment();
      return out;
    }
    case CST_APP: {
      CSTElement child = root.firstChildNode();
      AST lhs = dst_type(child);
      child.nextSiblingNode();
      AST rhs = dst_type(child);
      switch (lex_kind(lhs.name)) {
      case LOWER:    ERROR(lhs.token.location(),  "lower-case identifier '" << lhs.name << "' cannot be used as a type constructor"); break;
      case OPERATOR: ERROR(rhs.region.location(), "excess type argument " << child.segment() << " supplied to '" << lhs.name << "'"); break;
      default: break;
      }
      lhs.args.emplace_back(std::move(rhs)); 
      lhs.region = root.fragment();
      return lhs;
    }
    default:
      ERROR(root.fragment().location(), "type signatures forbid " << root.segment());
    case CST_ERROR:
      return AST(root.fragment(), "BadType");
  }
}

static void dst_topic(CSTElement topdef, Package &package, Symbols *globals) {
  CSTElement child = topdef.firstChildNode();
  TopFlags flags = dst_flags(child);

  std::string id = getIdentifier(child);
  auto fragment = child.fragment();
  if (lex_kind(id) != LOWER) {
    ERROR(child.fragment().location(), "topic identifier '" << id << "' is not lower-case");
    return;
  }
  child.nextSiblingNode();

  File &file = package.files.back();
  AST def = dst_type(child);

  // Confirm there are no open type variables
  TypeMap ids;
  TypeVar x;
  x.setDOB();
  def.unify(x, ids);

  auto it = file.topics.insert(std::make_pair(id, Topic(fragment, std::move(def))));
  if (!it.second) {
    ERROR(fragment.location(),
      "topic '" << id
      << "' was previously defined at " << it.first->second.fragment.location());
    return;
  }

  if (flags.exportf) package.exports.topics.insert(std::make_pair(id, SymbolSource(fragment, SYM_LEAF)));
  if (flags.globalf) globals->topics.insert(std::make_pair(id, SymbolSource(fragment, SYM_LEAF)));
}

struct Definition {
  std::string name;
  FileFragment fragment;
  std::unique_ptr<Expr> body;
  std::vector<ScopedTypeVar> typeVars;
  Definition(const std::string &name_, const FileFragment &fragment_, Expr *body_, std::vector<ScopedTypeVar> &&typeVars_)
   : name(name_), fragment(fragment_), body(body_), typeVars(std::move(typeVars_)) { }
  Definition(const std::string &name_, const FileFragment &fragment_, Expr *body_)
   : name(name_), fragment(fragment_), body(body_) { }
};

static void bind_global(const Definition &def, Symbols *globals) {
  if (!globals || def.name == "_") return;

  globals->defs.insert(std::make_pair(def.name, SymbolSource(def.fragment, SYM_LEAF)));
  // Duplicate globals will be detected as file-local conflicts
}

static void bind_export(const Definition &def, Symbols *exports) {
  if (!exports || def.name == "_") return;

  exports->defs.insert(std::make_pair(def.name, SymbolSource(def.fragment, SYM_LEAF)));
  // Duplicate exports will be detected as file-local conflicts
}

static void bind_def(DefMap &map, Definition &&def, Symbols *exports, Symbols *globals) {
  bind_global(def, globals);
  bind_export(def, exports);

  if (def.name == "_")
    def.name = "_" + std::to_string(map.defs.size()) + " _";

  Location l = def.body->fragment.location();
  auto out = map.defs.insert(std::make_pair(std::move(def.name), DefValue(
    def.fragment, std::move(def.body), std::move(def.typeVars))));

  if (!out.second) {
    ERROR(l,
      "definition '" << out.first->first
      << "' was previously defined at " << out.first->second.body->fragment.location());
  }
}

static void bind_type(Package &package, const std::string &name, const FileFragment &fragment, Symbols *exports, Symbols *globals) {
  if (globals) globals->types.insert(std::make_pair(name, SymbolSource(fragment, SYM_LEAF)));
  if (exports) exports->types.insert(std::make_pair(name, SymbolSource(fragment, SYM_LEAF)));

  auto it = package.package.types.insert(std::make_pair(name, SymbolSource(fragment, SYM_LEAF)));
  if (!it.second) {
    ERROR(fragment.location(),
      "type '" << it.first->first
      << "' was previously defined at " << it.first->second.fragment.location());
  }
}

static void dst_data(CSTElement topdef, Package &package, Symbols *globals) {
  CSTElement child = topdef.firstChildNode();
  TopFlags flags = dst_flags(child);

  AST type = dst_type(child);
  if (!type.tag.empty()) ERROR(child.fragment().location(), "data type '" << type.name << "' should not be tagged with '" << type.tag << "'");
  auto sump = std::make_shared<Sum>(std::move(type));
  if (sump->args.empty() && lex_kind(sump->name) == LOWER) ERROR(child.fragment().location(), "data type '" << sump->name << "' must be upper-case or operator");
  child.nextSiblingNode();

  for (; !child.empty(); child.nextSiblingNode()) {
    AST cons = dst_type(child);
    if (!cons.tag.empty()) ERROR(cons.region.location(), "constructor '" << cons.name << "' should not be tagged with '" << cons.tag << "'");
    if (cons.args.empty() && lex_kind(cons.name) == LOWER) ERROR(cons.token.location(), "constructor '" << cons.name << "' must be upper-case or operator");
    sump->addConstructor(std::move(cons));
  }

  Symbols *exports = flags.exportf ? &package.exports : nullptr;
  if (!flags.globalf) globals = nullptr;

  bind_type(package, sump->name, sump->token, exports, globals);
  for (auto &c : sump->members) {
    Expr *construct = new Construct(c.ast.token, sump, &c);
    for (size_t i = 0; i < c.ast.args.size(); ++i)
      construct = new Lambda(c.ast.token, "_", construct);

    bind_def(*package.files.back().content, Definition(c.ast.name, c.ast.token, construct), exports, globals);
  }

  if (package.name == "wake") check_special(sump);
}

static void dst_tuple(CSTElement topdef, Package &package, Symbols *globals) {
  CSTElement child = topdef.firstChildNode();
  TopFlags flags = dst_flags(child); // export/global constructor?
  bool exportt = flags.exportf; // we export the type if any member is exported
  bool globalt = flags.globalf;

  AST type = dst_type(child);
  if (!type.tag.empty()) ERROR(child.fragment().location(), "tuple type '" << type.name << "' should not be tagged with '" << type.tag << "'");
  auto sump = std::make_shared<Sum>(std::move(type));
  if (lex_kind(sump->name) != UPPER) ERROR(child.fragment().location(), "tuple type '" << sump->name << "' must be upper-case");
  child.nextSiblingNode();

  std::string name = sump->name;

  AST tuple(sump->token, std::string(sump->name));
  tuple.region = sump->region;
  std::vector<TopFlags> members;

  for (; !child.empty(); child.nextSiblingNode()) {
    CSTElement elt = child.firstChildNode();
    members.emplace_back(dst_flags(elt));
    tuple.args.emplace_back(dst_type(elt));
  }

  sump->addConstructor(std::move(tuple));

  Constructor &c = sump->members.back();
  Expr *construct = new Construct(c.ast.token, sump, &c);
  for (size_t i = c.ast.args.size(); i > 0; --i)
    construct = new Lambda(c.ast.token, c.ast.args[i-1].tag, construct);

  DefMap &map = *package.files.back().content;

  Symbols *exports = &package.exports;

  // Create get/set/edit helper methods
  for (size_t i = 0; i < members.size(); ++i) {
    bool globalb = members[i].globalf;
    bool exportb = members[i].exportf;
    if (globalb) globalt = true;
    if (exportb) exportt = true;

    std::string &mname = c.ast.args[i].tag;
    FileFragment memberRegion = c.ast.args[i].region;
    FileFragment memberToken(
      memberRegion.fcontent(),
      memberRegion.startByte(),
      memberRegion.startByte() + mname.size());

    if (lex_kind(mname) != UPPER) continue;

    // Implement get methods
    std::string get = "get" + name + mname;
    Expr *getfn = new Lambda(memberToken, "_", new Get(memberToken, sump, &c, i));
    getfn->flags |= FLAG_SYNTHETIC;
    bind_def(map, Definition(get, memberToken, getfn), exportb?exports:nullptr, globalb?globals:nullptr);

    // Implement edit methods
    DefMap *editmap = new DefMap(memberToken);
    editmap->body = std::unique_ptr<Expr>(new Construct(memberToken, sump, &c));
    for (size_t inner = 0; inner < members.size(); ++inner) {
      Expr *select = new Get(memberToken, sump, &c, inner);
      if (inner == i)
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
    bind_def(map, Definition(edit, memberToken, editfn), exportb?exports:nullptr, globalb?globals:nullptr);

    // Implement set methods
    DefMap *setmap = new DefMap(memberToken);
    setmap->body = std::unique_ptr<Expr>(new Construct(memberToken, sump, &c));
    for (size_t inner = 0; inner < members.size(); ++inner) {
      std::string x = std::to_string(members.size()-inner);
      std::string name = "_ a" + std::string(4 - x.size(), '0') + x;
      setmap->defs.insert(std::make_pair(name,
        DefValue(memberToken, std::unique_ptr<Expr>(
          (inner == i)
          ? static_cast<Expr*>(new VarRef(memberToken, mname))
          : static_cast<Expr*>(new Get(memberToken, sump, &c, inner))))));
    }

    std::string set = "set" + name + mname;
    Expr *setfn =
      new Lambda(memberToken, mname,
        new Lambda(memberToken, "_ x", setmap));

    setfn->flags |= FLAG_SYNTHETIC;
    bind_def(map, Definition(set, memberToken, setfn), exportb?exports:nullptr, globalb?globals:nullptr);
  }

  bind_type(package, sump->name, sump->token, exportt?exports:nullptr, globalt?globals:nullptr);
  bind_def(map, Definition(c.ast.name, c.ast.token, construct), flags.exportf?exports:nullptr, flags.globalf?globals:nullptr);

  if (package.name == "wake") check_special(sump);
}

static AST dst_pattern(CSTElement root, std::vector<CSTElement> *guard) {
  switch (root.id()) {
    case CST_ASCRIBE: {
      CSTElement child = root.firstChildNode();
      AST lhs = dst_pattern(child, guard);
      child.nextSiblingNode();
      if (lhs.type) {
        ERROR(child.location(), "pattern " << lhs.region.segment() << " already has a type");
      } else {
        lhs.type = optional<AST>(new AST(dst_type(child)));
      }
      return lhs;
    }
    case CST_BINARY: {
      CSTElement child = root.firstChildNode();
      AST lhs = dst_pattern(child, guard);
      child.nextSiblingNode();
      std::string op = "binary " + getIdentifier(child);
      FileFragment fragment = child.fragment();
      child.nextSiblingNode();
      AST rhs = dst_pattern(child, guard);
      std::vector<AST> args;
      args.emplace_back(std::move(lhs));
      args.emplace_back(std::move(rhs));
      AST out(fragment, std::move(op), std::move(args));
      out.region = root.fragment();
      return out;
    }
    case CST_UNARY: {
      CSTElement child = root.firstChildNode();
      std::vector<AST> args;
      if (child.id() != CST_OP) {
        args.emplace_back(dst_pattern(child, guard));
        child.nextSiblingNode();
      }
      std::string op = "unary " + getIdentifier(child);
      FileFragment fragment = child.fragment();
      child.nextSiblingNode();
      if (args.empty()) {
        args.emplace_back(dst_pattern(child, guard));
        child.nextSiblingNode();
      }
      AST out(fragment, std::move(op), std::move(args));
      out.region = root.fragment();
      return out;
    }
    case CST_ID: {
      return AST(root.fragment(), getIdentifier(root));
    }
    case CST_PAREN: {
      AST out = dst_pattern(root.firstChildNode(), guard);
      out.region = root.fragment();
      return out;
    }
    case CST_APP: {
      CSTElement child = root.firstChildNode();
      AST lhs = dst_pattern(child, guard);
      child.nextSiblingNode();
      AST rhs = dst_pattern(child, guard);
      switch (lex_kind(lhs.name)) {
      case LOWER:    ERROR(lhs.token.location(),  "lower-case identifier '" << lhs.name << "' cannot be used as a pattern destructor"); break;
      case OPERATOR: ERROR(rhs.region.location(), "excess argument " << child.segment() << " supplied to '" << lhs.name << "'"); break;
      default: break;
      }
      lhs.args.emplace_back(std::move(rhs)); 
      lhs.region = root.fragment();
      return lhs;
    }
    case CST_HOLE: {
      return AST(root.fragment(), "_");
    }
    case CST_LITERAL: {
      if (guard) {
        AST out(root.fragment(), "_ k" + std::to_string(guard->size()));
        guard->emplace_back(root);
        return out;
      } else {
        ERROR(root.fragment().location(), "def/lambda patterns forbid " << root.segment() << "; use a match");
        return AST(root.fragment(), "_");
      }
    }
    default:
      ERROR(root.fragment().location(), "patterns forbid " << root.segment());
    case CST_ERROR:
      return AST(root.fragment(), "_");
  }
}

static AST dst_def_pattern(CSTElement root) {
  switch (root.id()) {
    case CST_ASCRIBE: {
      CSTElement child = root.firstChildNode();
      AST lhs = dst_def_pattern(child);
      child.nextSiblingNode();
      if (lhs.type) {
        ERROR(child.location(), "pattern " << lhs.region.segment() << " already has a type");
      } else {
        lhs.type = optional<AST>(new AST(dst_type(child)));
      }
      return lhs;
    }
    case CST_APP: {
      CSTElement child = root.firstChildNode();
      AST lhs = dst_def_pattern(child);
      child.nextSiblingNode();
      AST rhs = dst_pattern(child, nullptr);
      if (lex_kind(lhs.name) == OPERATOR) {
        ERROR(rhs.region.location(),
          "excess argument " << child.segment()
          << " supplied to '" << lhs.name << "'");
      }
      lhs.args.emplace_back(std::move(rhs));
      lhs.region = root.fragment();
      return lhs;
    }
    default: {
      return dst_pattern(root, nullptr);
    }
  }
}

static int relabel_descend(Expr *expr, int index) {
  if (!expr) return index;
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
    out = new Lambda(out->fragment, "_ " + std::to_string(index), out);
  return out;
}

static void extract_def(std::vector<Definition> &out, long index, AST &&ast, const std::vector<ScopedTypeVar> &typeVars, Expr *body) {
  std::string key = "_ extract " + std::to_string(++index);
  out.emplace_back(key, ast.token, body, std::vector<ScopedTypeVar>(typeVars));
  if (ast.args.empty()) {
    Match *match = new Match(ast.token);
    match->args.emplace_back(new VarRef(body->fragment, key));
    match->patterns.emplace_back(AST(ast.token, std::string(ast.name)), new VarRef(body->fragment, key), nullptr);
    match->patterns.back().pattern.region = ast.region;
    out.emplace_back("_ discard " + std::to_string(index), ast.token, match, std::vector<ScopedTypeVar>(typeVars));
  }
  for (auto &m : ast.args) {
    AST pattern(ast.token, std::string(ast.name));
    pattern.region = ast.region;
    pattern.type = std::move(ast.type);
    std::string mname("_ " + m.name);
    for (auto &n : ast.args) {
      pattern.args.push_back(AST(m.token, "_"));
      if (&n == &m) {
        AST &back = pattern.args.back();
        back.name = mname;
        back.type = std::move(m.type);
      }
    }
    Match *match = new Match(m.token);
    match->args.emplace_back(new VarRef(body->fragment, key));
    match->patterns.emplace_back(std::move(pattern), new VarRef(m.token, mname), nullptr);
    if (lex_kind(m.name) != LOWER) {
      extract_def(out, index, std::move(m), typeVars, match);
    } else {
      out.emplace_back(m.name, m.token, match, std::vector<ScopedTypeVar>(typeVars));
    }
  }
}

static void dst_def(CSTElement def, DefMap &map, Package *package, Symbols *globals) {
  bool target  = def.id() == CST_TARGET;
  bool publish = def.id() == CST_PUBLISH;

  CSTElement child = def.firstChildNode();
  TopFlags flags = dst_flags(child);

  Symbols *exports = flags.exportf ? &package->exports : nullptr;
  if (!flags.globalf) globals = nullptr;

  AST ast = dst_def_pattern(child);
  std::string name = std::move(ast.name);
  ast.name.clear();

  uint8_t kind = lex_kind(name);
  bool extract = kind == UPPER || (child.id() == CST_PAREN && kind == OPERATOR);
  if (extract && (target || publish)) {
    ERROR(ast.token.location(), "upper-case identifier '" << name << "' cannot be used as a target/publish name");
    return;
  }

  child.nextSiblingNode();

  size_t tohash = ast.args.size();
  if (target && child.id() == CST_TARGET_ARGS) {
    for (CSTElement sub = child.firstChildNode(); !sub.empty(); sub.nextSiblingNode()) {
      ast.args.emplace_back(dst_pattern(sub, nullptr));
    }
    child.nextSiblingNode();
  }

  FileFragment fn = ast.region;

  Expr *body = relabel_anon(dst_expr(child));

  // Record type variables introduced by the def before we rip the ascription appart
  std::vector<ScopedTypeVar> typeVars;
  ast.typeVars(typeVars);

  std::vector<Definition> defs;

  if (extract) {
    ast.name = std::move(name);
    extract_def(defs, map.defs.size(), std::move(ast), typeVars, body);
  } else {
    // do we need a pattern match? lower / wildcard are ok
    bool pattern = false;
    bool typed = false;
    for (auto &x : ast.args) {
      pattern |= lex_kind(x.name) != LOWER;
      typed |= x.type;
    }

    optional<AST> type = std::move(ast.type);
    std::vector<std::pair<std::string, FileFragment> > args;
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
        args.emplace_back("_ " + std::to_string(i), FRAGMENT_CPP_LINE);
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
            new Ascribe(FRAGMENT_CPP_LINE, std::move(*arg.type), new VarRef(FRAGMENT_CPP_LINE, arg.name), arg.token)))));
        }
      }
      body = dm;
    } else {
      // no pattern; simple lambdas for the arguments
      for (auto &x : ast.args) args.emplace_back(x.name, x.token);
    }

    if (type)
      body = new Ascribe(body->fragment, std::move(*type), body, body->fragment);

    if (target) {
      if (tohash == 0) ERROR(fn.location(), "target definition of '" << name << "' must have at least one hashed argument");
      FileFragment bl = body->fragment;

      Expr *tget = new Prim(bl, "tget");
      for (size_t i = 0; i < args.size(); ++i)
        tget = new Lambda(bl, "_", tget, " ");

      tget = new App(bl, new App(bl,
        new Lambda(bl, "_ target", new Lambda(bl, "_ body", tget)),
        new VarRef(bl, "table " + name)),
        new Lambda(bl, "_", body, " "));

      for (size_t i = 0; i < args.size(); ++i)
        tget = new App(bl, tget, new VarRef(bl, args[i].first));

      body = tget;
    }

    if (publish && !args.empty()) {
      ERROR(fn.location(), "publish definition of '" << name << "' may not be a function");
    } else {
      for (auto i = args.rbegin(); i != args.rend(); ++i) {
        Lambda *lambda = new Lambda(fn, i->first, body);
        lambda->token = i->second;
        body = lambda;
      }
    }

    defs.emplace_back(name, ast.token, body, std::move(typeVars));

    if (target) {
      FileFragment l = FRAGMENT_CPP_LINE;

      Expr *table = new Prim(l, "tnew");
      for (size_t i = 0; i < args.size()+2; ++i)
        table = new Lambda(l, "_", table, " ");

      std::stringstream s;
      s << "'" << name << "' <" << defs.front().body->fragment.location() << ">";
      table = new App(l, table, new Literal(l, s.str(), &Data::typeString));
      table = new App(l, table, new Literal(l, std::to_string(tohash), &Data::typeInteger));

      for (size_t i = 0; i < args.size(); ++i)
        table = new App(l, table, new Literal(l, std::string(args[i].first), &Data::typeString));

      bind_def(map, Definition("table " + name, l, table), nullptr, nullptr);
    }
  }

  if (publish) {
    for (auto &def : defs) package->files.back().pubs.emplace_back(def.name, DefValue(def.fragment, std::move(def.body)));
  } else {
    for (auto &def : defs) bind_def(map, std::move(def), exports, globals);
  }
}

static void mstr_add(std::ostream &os, CSTElement token, std::string::size_type wsCut) {
  uint8_t nid = token.id();
  while (!token.empty()) {
    StringSegment ti = token.segment();
    token.nextSiblingElement();
    uint8_t id = nid;
    nid = token.id();

    switch (id) {
      case TOKEN_LSTR_END:
      case TOKEN_MSTR_END:       break;
      case TOKEN_LSTR_RESUME:
      case TOKEN_MSTR_RESUME:    os << relex_mstring(ti.start + 1,     ti.end);      break;
      case TOKEN_WS:             os << relex_mstring(ti.start + wsCut, ti.end);      break;
      case TOKEN_LSTR_PAUSE:
      case TOKEN_MSTR_PAUSE:     os << relex_mstring(ti.start,         ti.end - 2);  break;
      case TOKEN_NL:             if (nid == TOKEN_LSTR_END || nid == TOKEN_MSTR_END) break;
      case TOKEN_MSTR_CONTINUE:
      case TOKEN_LSTR_CONTINUE:
      default:                   os << relex_mstring(ti.start,         ti.end);      break;
    }
  }
}

struct MultiLineStringIndentationFSM {
  std::string prefix;
  bool priorWS;
  bool noPrefix;

  MultiLineStringIndentationFSM() : priorWS(false), noPrefix(true) {}
  void accept(CSTElement lit);

  static std::string::size_type analyze(CSTElement lit);
};

std::string::size_type MultiLineStringIndentationFSM::analyze(CSTElement lit) {
  MultiLineStringIndentationFSM fsm;
  fsm.accept(lit);
  return fsm.prefix.size();
}

void MultiLineStringIndentationFSM::accept(CSTElement lit) {
  for (CSTElement child = lit.firstChildElement(); !child.empty(); child.nextSiblingElement()) {
    switch (child.id()) {
      case TOKEN_WS: {
        std::string ws = child.segment().str();
        if (noPrefix) {
          prefix = std::move(ws);
        } else {
          // Find the longest common prefix
          size_t e = std::min(ws.size(), prefix.size());
          size_t i;
          for (i = 0; i < e; ++i)
             if (ws[i] != prefix[i])
               break;
          prefix.resize(i);
        }
        priorWS = true;
        noPrefix = false;
        break;
      }

      case TOKEN_LSTR_CONTINUE:
      case TOKEN_MSTR_CONTINUE:
      case TOKEN_LSTR_PAUSE:
      case TOKEN_MSTR_PAUSE:
        if (!priorWS) prefix.clear();
        noPrefix = false;
        break;

      case TOKEN_NL:
        priorWS = false;
        break;

      case TOKEN_LSTR_BEGIN:
      case TOKEN_MSTR_BEGIN:
      case TOKEN_LSTR_MID:
      case TOKEN_MSTR_MID:
      case TOKEN_LSTR_END:
      case TOKEN_MSTR_END:
      case TOKEN_LSTR_RESUME:
      case TOKEN_MSTR_RESUME:
      default:
        break;
    }
  }
}

static Literal *dst_literal(CSTElement lit, std::string::size_type wsCut) {
  CSTElement child = lit.firstChildElement();
  uint8_t id = child.id();
  switch (id) {
    case TOKEN_STR_RAW: {
      StringSegment ti = child.segment();
      ++ti.start;
      --ti.end;
      return new Literal(child.fragment(), ti.str(), &Data::typeString);
    }
    case TOKEN_STR_SINGLE:
    case TOKEN_STR_MID:
    case TOKEN_STR_OPEN:
    case TOKEN_STR_CLOSE: {
      FileFragment fragment = child.fragment();
      return new Literal(fragment, relex_string(fragment), &Data::typeString);
    }
    case TOKEN_REG_SINGLE: {
      StringSegment ti = child.segment();
      std::string str = relex_regexp(id, ti.start, ti.end);
      re2::RE2 check(str);
      if (!check.ok()) ERROR(child.fragment().location(), "illegal regular expression: " << check.error());
      return new Literal(child.fragment(), std::move(str), &Data::typeRegExp);
    }
    case TOKEN_REG_MID:
    case TOKEN_REG_OPEN:
    case TOKEN_REG_CLOSE: {
      StringSegment ti = child.segment();
      // rcat expects String tokens, not RegExp
      return new Literal(child.fragment(), relex_regexp(id, ti.start, ti.end), &Data::typeString);
    }
    case TOKEN_DOUBLE: {
      std::string x = child.segment().str();
      x.resize(std::remove(x.begin(), x.end(), '_') - x.begin());
      return new Literal(child.fragment(), std::move(x), &Data::typeDouble);
    }
    case TOKEN_INTEGER: {
      std::string x = child.segment().str();
      x.resize(std::remove(x.begin(), x.end(), '_') - x.begin());
      return new Literal(child.fragment(), std::move(x), &Data::typeInteger);
    }
    case TOKEN_KW_HERE: {
      std::string name(lit.fragment().location().filename);
      std::string::size_type cut = name.find_last_of('/');
      if (cut == std::string::npos) name = "."; else name.resize(cut);
      return new Literal(lit.fragment(), std::move(name), &Data::typeString);
    }
    case TOKEN_LSTR_BEGIN:
    case TOKEN_MSTR_BEGIN: {
      // TOKEN_MSTR_BEGIN NL (WS? MSTR_CONTINUE? NL)* (NL MSTR_END | WS? MSTR_PAUSE)
      std::stringstream ss;
      child.nextSiblingElement(); // skip BEGIN
      child.nextSiblingElement(); // skip NL
      mstr_add(ss, child, wsCut);
      return new Literal(lit.fragment(), ss.str(), &Data::typeString);
    }
    case TOKEN_LSTR_MID:
    case TOKEN_MSTR_MID: {
      StringSegment ti = child.segment();
      return new Literal(child.fragment(), relex_mstring(ti.start+1, ti.end-2), &Data::typeString);
    }
    case TOKEN_LSTR_RESUME:
    case TOKEN_MSTR_RESUME: {
      // TOKEN_MSTR_RESUME (WS? MSTR_CONTINUE? NL)* (NL MSTR_END | WS? MSTR_PAUSE)
      std::stringstream ss;
      mstr_add(ss, child, wsCut);
      return new Literal(lit.fragment(), ss.str(), &Data::typeString);
    }
    default: {
      ERROR(lit.fragment().location(), "unsupported literal " << symbolExample(id) << " = " << lit.segment());
      return new Literal(lit.fragment(), "bad-literal", &Data::typeString);
    }
  }
}

static Expr *dst_interpolate(CSTElement intp) {
  bool regexp = intp.firstChildNode().firstChildElement().id() == TOKEN_REG_OPEN;
  std::vector<Expr*> args;
  std::string total;

  MultiLineStringIndentationFSM fsm;
  for (CSTElement i = intp.firstChildNode(); !i.empty(); i.nextSiblingNode()) {
    if (args.size() % 2 == 0) fsm.accept(i);
  }

  for (CSTElement i = intp.firstChildNode(); !i.empty(); i.nextSiblingNode()) {
    if (args.size() % 2 == 0) {
      Literal *lit = dst_literal(i, fsm.prefix.size());
      if (regexp) total.append(lit->value);
      args.push_back(lit);
    } else {
      args.push_back(dst_expr(i));
    }
  }

  FileFragment full = intp.fragment();
  Expr *cat = new Prim(full, regexp?"rcat":"vcat");
  for (size_t i = 0; i < args.size(); ++i) cat = new Lambda(full, "_", cat, i?"":" ");
  for (auto arg : args) cat = new App(full, cat, arg);

  if (regexp) {
    re2::RE2 check(total);
    if (!check.ok()) ERROR(full.location(), "illegal regular expression: " << check.error());
  }

  cat->flags |= FLAG_AST;
  return cat;
}

static Expr *add_literal_guards(Expr *guard, std::vector<CSTElement> literals) {
  for (size_t i = 0; i < literals.size(); ++i) {
    CSTElement literal = literals[i];
    Literal* lit = dst_literal(literal, 0);

    const char *comparison;
    if (lit->litType == &Data::typeString) {
      comparison = "scmp";
    } else if (lit->litType == &Data::typeInteger) {
      comparison = "icmp";
    } else if (lit->litType == &Data::typeRegExp) {
      comparison = "rcmp";
    } else if (lit->litType == &Data::typeDouble) {
      comparison = "dcmp_nan_lt";
    } else {
      comparison = nullptr;
      assert(0);
    }

    if (!guard) guard = new VarRef(lit->fragment, "True@wake");

    Match *match = new Match(lit->fragment);
    match->args.emplace_back(new App(lit->fragment, new App(lit->fragment,
        new Lambda(lit->fragment, "_", new Lambda(lit->fragment, "_", new Prim(lit->fragment, comparison), " ")),
        lit), new VarRef(lit->fragment, "_ k" + std::to_string(i))));
    match->patterns.emplace_back(AST(lit->fragment, "LT@wake"), new VarRef(lit->fragment, "False@wake"), nullptr);
    match->patterns.emplace_back(AST(lit->fragment, "GT@wake"), new VarRef(lit->fragment, "False@wake"), nullptr);
    match->patterns.emplace_back(AST(lit->fragment, "EQ@wake"), guard, nullptr);
    guard = match;
  }
  return guard;
}

static Expr *dst_match(CSTElement match) {
  FileFragment fragment = match.fragment();
  Match *out = new Match(fragment);

  CSTElement child;
  for (child = match.firstChildNode(); !child.empty() && child.id() != CST_CASE; child.nextSiblingNode()) {
    auto rhs = dst_expr(child);
    out->args.emplace_back(rhs);
  }

  // Process the patterns
  for (; !child.empty(); child.nextSiblingNode()) {
    CSTElement casee;

    std::vector<CSTElement> guards;
    std::vector<AST> args;
    for (casee = child.firstChildNode(); casee.id() != CST_GUARD; casee.nextSiblingNode()) {
      args.emplace_back(dst_pattern(casee, &guards));
    }

    AST pattern = args.size()==1 ? std::move(args[0]) : AST(child.fragment(), "", std::move(args));

    CSTElement guarde = casee.firstChildNode();
    Expr *guard = guarde.empty() ? nullptr : relabel_anon(dst_expr(guarde));
    casee.nextSiblingNode();

    guard = add_literal_guards(guard, guards);

    Expr *expr = relabel_anon(dst_expr(casee));
    out->patterns.emplace_back(std::move(pattern), expr, guard);
  }

  return out;
}

static Expr *dst_block(CSTElement block) {
  DefMap *map = new DefMap(block.fragment());

  std::unique_ptr<Expr> body;
  for (CSTElement child = block.firstChildNode(); !child.empty(); child.nextSiblingNode()) {
    switch (child.id()) {
      case CST_IMPORT: dst_import(child, *map); break;
      case CST_DEF:    dst_def(child, *map, nullptr, nullptr); break;
      default:         map->body = std::unique_ptr<Expr>(relabel_anon(dst_expr(child))); break;
    }
  }

  return map;
}

static Expr *dst_require(CSTElement require) {
  CSTElement child = require.firstChildNode();

  std::vector<CSTElement> guards;
  AST ast = dst_pattern(child, &guards);
  child.nextSiblingNode();

  Expr *rhs = relabel_anon(dst_expr(child));
  child.nextSiblingNode();

  Expr *otherwise = nullptr;
  if (child.id() == CST_REQ_ELSE) {
    otherwise = relabel_anon(dst_expr(child.firstChildNode()));
    child.nextSiblingNode();
  }

  Expr *block = relabel_anon(dst_expr(child));

  Match *out = new Match(require.fragment(), true);
  out->args.emplace_back(rhs);
  out->patterns.emplace_back(std::move(ast), block, add_literal_guards(nullptr, guards));
  out->otherwise = std::unique_ptr<Expr>(otherwise);

  return out;
}

static Expr *dst_expr(CSTElement expr) {
  switch (expr.id()) {
    case CST_ASCRIBE: {
      CSTElement child = expr.firstChildNode();
      Expr *lhs = dst_expr(child);
      child.nextSiblingNode();
      if (lhs->type == &Ascribe::type) {
        ERROR(child.fragment().location(), "expression " << lhs->fragment.segment() << " already has a type");
        return lhs;
      } else {
        AST signature = dst_type(child);
        return new Ascribe(expr.fragment(), std::move(signature), lhs, lhs->fragment);
      }
    }
    case CST_BINARY: {
      CSTElement child = expr.firstChildNode();
      Expr *lhs = dst_expr(child);
      child.nextSiblingNode();
      std::string opStr = getIdentifier(child);
      Expr *op = new VarRef(child.fragment(), "binary " + opStr);
      op->flags |= FLAG_AST;
      child.nextSiblingNode();
      Expr *rhs = dst_expr(child);
      FileFragment l = expr.fragment();
      App *out = new App(l, new App(l, op, lhs), rhs);
      out->flags |= FLAG_AST;
      return out;
    }
    case CST_UNARY: {
      CSTElement child = expr.firstChildNode();
      Expr *body = nullptr;
      if (child.id() != CST_OP) {
        body = dst_expr(child);
        child.nextSiblingNode();
      }
      Expr *op = new VarRef(child.fragment(), "unary " + getIdentifier(child));
      op->flags |= FLAG_AST;
      child.nextSiblingNode();
      if (!body) body = dst_expr(child);
      App *out = new App(expr.fragment(), op, body);
      out->flags |= FLAG_AST;
      return out;
    }
    case CST_ID: {
      VarRef *out = new VarRef(expr.fragment(), getIdentifier(expr));
      out->flags |= FLAG_AST;
      return out;
    }
    case CST_PAREN: {
      return relabel_anon(dst_expr(expr.firstChildNode()));
    }
    case CST_APP: {
      CSTElement child = expr.firstChildNode();
      Expr *lhs = dst_expr(child);
      child.nextSiblingNode();
      Expr *rhs = dst_expr(child);
      App *out = new App(expr.fragment(), lhs, rhs);
      out->flags |= FLAG_AST;
      return out;
    }
    case CST_HOLE: {
      VarRef *out = new VarRef(expr.fragment(), "_");
      out->flags |= FLAG_AST;
      return out;
    }
    case CST_SUBSCRIBE: {
      Subscribe *out = new Subscribe(expr.fragment(), getIdentifier(expr.firstChildNode()));
      out->flags |= FLAG_AST;
      return out;
    }
    case CST_PRIM: {
      FileFragment fragment = expr.firstChildNode().firstChildElement().fragment();
      Prim *out = new Prim(expr.fragment(), relex_string(fragment));
      out->flags |= FLAG_AST;
      return out;
    }
    case CST_IF: {
      CSTElement child = expr.firstChildNode();
      Expr *condE = relabel_anon(dst_expr(child));
      child.nextSiblingNode();
      Expr *thenE = relabel_anon(dst_expr(child));
      child.nextSiblingNode();
      Expr *elseE = relabel_anon(dst_expr(child));
      Match *out = new Match(expr.fragment());
      out->args.emplace_back(condE);
      out->patterns.emplace_back(AST(FRAGMENT_CPP_LINE, "True@wake"),  thenE, nullptr);
      out->patterns.emplace_back(AST(FRAGMENT_CPP_LINE, "False@wake"), elseE, nullptr);
      out->flags |= FLAG_AST;
      return out;
    }
    case CST_LAMBDA: {
      CSTElement child = expr.firstChildNode();
      AST ast = dst_pattern(child, nullptr);
      child.nextSiblingNode();
      Expr *body = dst_expr(child);
      Lambda *out;
      FileFragment l = expr.fragment();
      if (lex_kind(ast.name) != LOWER) {
        Match *match = new Match(l);
        match->patterns.emplace_back(std::move(ast), body, nullptr);
        match->args.emplace_back(new VarRef(ast.region, "_ xx"));
        out = new Lambda(l, "_ xx", match);
      } else if (ast.type) {
        DefMap *dm = new DefMap(l);
        dm->body = std::unique_ptr<Expr>(body);
        dm->defs.insert(std::make_pair(ast.name, DefValue(ast.region, std::unique_ptr<Expr>(
          new Ascribe(FRAGMENT_CPP_LINE, std::move(*ast.type), new VarRef(FRAGMENT_CPP_LINE, "_ typed"), ast.region)))));
        out = new Lambda(l, "_ typed", dm);
      } else {
        out = new Lambda(l, ast.name, body);
        out->token = ast.token;
      }
      out->flags |= FLAG_AST;
      return out;
    }
    case CST_MATCH:       return dst_match(expr);
    case CST_LITERAL:     return dst_literal(expr, MultiLineStringIndentationFSM::analyze(expr));
    case CST_INTERPOLATE: return dst_interpolate(expr);
    case CST_BLOCK:       return dst_block(expr);
    case CST_REQUIRE:     return dst_require(expr);
    default:
      ERROR(expr.fragment().location(), "unexpected expression: " << expr.segment());
    case CST_ERROR: {
      FileFragment l = expr.fragment();
      return new App(l,
        new Lambda(l, "_", new Prim(l, "unreachable")),
        new Literal(l, "bad-expression", &Data::typeString));
    }
  }
}

const char *dst_top(CSTElement root, Top &top) {
  std::unique_ptr<Package> package(new Package);
  package->files.resize(1);
  File &file = package->files.back();
  file.content = std::unique_ptr<DefMap>(new DefMap(root.fragment()));
  DefMap &map = *file.content;
  Symbols globals;

  for (CSTElement topdef = root.firstChildNode(); !topdef.empty(); topdef.nextSiblingNode()) {
    switch (topdef.id()) {
    case CST_PACKAGE: dst_package(topdef, *package); break;
    case CST_IMPORT:  dst_import (topdef, map);      break;
    case CST_EXPORT:  dst_export (topdef, *package); break;
    case CST_TOPIC:   dst_topic  (topdef, *package, &globals); break;
    case CST_DATA:    dst_data   (topdef, *package, &globals); break;
    case CST_TUPLE:   dst_tuple  (topdef, *package, &globals); break;
    case CST_DEF:
    case CST_PUBLISH:
    case CST_TARGET:
      dst_def(topdef, *package->files.back().content, package.get(), &globals);
      break;
    }
  }

  // Set a default import
  if (file.content->imports.empty())
    file.content->imports.import_all.emplace_back("wake", file.content->fragment);

  // Set a default package name
  if (package->name.empty()) {
    package->name = file.content->fragment.location().filename;
  }

  package->exports.setpkg(package->name);
  globals.setpkg(package->name);

  top.globals.join(globals, "global");

  // localize all top-level symbols
  DefMap::Defs defs(std::move(map.defs));
  map.defs.clear();
  for (auto &def : defs) {
    auto name = def.first + "@" + package->name;
    auto it = file.local.defs.insert(std::make_pair(def.first, SymbolSource(def.second.fragment, name, SYM_LEAF)));
    if (!it.second) {
      if (it.first->second.qualified == name) {
        it.first->second.fragment = def.second.fragment;
        it.first->second.flags |= SYM_LEAF;
        auto jt = package->exports.defs.find(def.first);
        jt->second.flags |= SYM_LEAF;
        it.first->second.origin = jt->second.origin = def.second.fragment;
      } else {
        ERROR(def.second.fragment.location(),
          "definition '" << def.first
          << "' was previously defined at " << it.first->second.fragment.location());
      }
    }
    map.defs.insert(std::make_pair(std::move(name), std::move(def.second)));
  }

  // localize all topics
  for (auto &topic : file.topics) {
    auto name = topic.first + "@" + package->name;
    auto it = file.local.topics.insert(std::make_pair(topic.first, SymbolSource(topic.second.fragment, name, SYM_LEAF)));
    if (!it.second) {
      if (it.first->second.qualified == name) {
        it.first->second.fragment = topic.second.fragment;
        it.first->second.flags |= SYM_LEAF;
        auto jt = package->exports.topics.find(topic.first);
        jt->second.flags |= SYM_LEAF;
        it.first->second.origin = jt->second.origin = topic.second.fragment;
      } else {
        ERROR(topic.second.fragment.location(),
          "topic '" << topic.first
          << "' was previously defined at " << it.first->second.fragment.location());
      }
    }
  }

  // localize all types
  for (auto &type : package->package.types) {
    auto name = type.first + "@" + package->name;
    auto it = file.local.types.insert(std::make_pair(type.first, SymbolSource(type.second.fragment, name, SYM_LEAF)));
    if (!it.second) {
      if (it.first->second.qualified == name) {
        it.first->second.fragment = type.second.fragment;
        it.first->second.flags |= SYM_LEAF;
        auto jt = package->exports.types.find(type.first);
        jt->second.flags |= SYM_LEAF;
        it.first->second.origin = jt->second.origin = type.second.fragment;
      } else {
        ERROR(type.second.fragment.location(),
          "type '" << type.first
          << "' was previously defined at " << it.first->second.fragment.location());
      }
    }
  }

  auto it = top.packages.insert(std::make_pair(package->name, nullptr));
  if (it.second) {
    package->package = file.local;
    it.first->second = std::move(package);
  } else {
    it.first->second->package.join(file.local, "package-local");
    it.first->second->exports.join(package->exports, nullptr);
    // duplicated export already reported as package-local duplicate
    it.first->second->files.emplace_back(std::move(file));
  }

  return it.first->second->name.c_str();
}


ExprParser::ExprParser(const std::string &content)
 : file("<command-line>", "def _ = " + content) { }

std::unique_ptr<Expr> ExprParser::expr(DiagnosticReporter &reporter) {
  CST cst(file, reporter);
  CSTElement topdef = cst.root().firstChildNode();
  CSTElement defcontent = topdef.firstChildNode();
  defcontent.nextSiblingNode(); // skip pattern
  return std::unique_ptr<Expr>(dst_expr(defcontent));
}
