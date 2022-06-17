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

#include "datatype.h"

#include <sstream>

#include "parser/lexer.h"
#include "type.h"
#include "util/diagnostic.h"
#include "util/fragment.h"

static CPPFile cppFile(__FILE__);

Constructor Constructor::array(AST(FRAGMENT_CPP_LINE, "Array"));

Sum::Sum(AST &&ast)
    : name(std::move(ast.name)), token(ast.token), region(ast.region), scoped(false) {
  std::map<std::string, FileFragment> dups;

  for (auto &x : ast.args) {
    if (lex_kind(x.name) != LOWER) {
      ERROR(x.token.location(), "type argument '" << x.name << "' must be lower-case");
    }
    auto insert = dups.insert(std::make_pair(x.name, x.token));
    if (!insert.second) {
      ERROR(x.token.location(), "type argument '" << x.name << "' already defined at "
                                                  << insert.first->second.location());
    }
    args.push_back(std::move(x.name));
  }
}

void Sum::addConstructor(AST &&ast) {
  members.emplace_back(std::move(ast));
  Constructor &cons = members.back();
  cons.index = members.size() - 1;
}

AST::AST(const FileFragment &token_, std::string &&name_, std::vector<AST> &&args_)
    : token(token_),
      region(token_),
      definition(FRAGMENT_CPP_LINE),
      name(std::move(name_)),
      args(std::move(args_)) {}

AST::AST(const FileFragment &token_, std::string &&name_)
    : token(token_), region(token_), definition(FRAGMENT_CPP_LINE), name(std::move(name_)) {}
AST::AST(const FileFragment &token_)
    : token(token_), region(token_), definition(FRAGMENT_CPP_LINE) {}

bool AST::unify(TypeVar &out, const TypeMap &ids) {
  if (lex_kind(name) == LOWER) {
    auto it = ids.find(name);
    if (it == ids.end()) {
      ERROR(token.location(), "unbound type variable '" << name << "'");
      return false;
    } else {
      return out.unify(*it->second, &region);
    }
  } else {  // upper or operator
    TypeVar cons(name.c_str(), args.size());
    bool ok = out.unify(cons);
    bool childok = true;
    if (ok) {
      for (size_t i = 0; i < args.size(); ++i) {
        childok = args[i].unify(out[i], ids) && childok;
        if (!args[i].tag.empty()) out.setTag(i, args[i].tag.c_str());
      }
    }
    return ok && childok;
  }
}

void AST::lowerVars(std::vector<ScopedTypeVar> &out) const {
  if (!name.empty() && lex_kind(name) == LOWER) out.emplace_back(name, token);
  for (auto &arg : args) arg.lowerVars(out);
}

void AST::typeVars(std::vector<ScopedTypeVar> &out) const {
  if (type) type->lowerVars(out);
  for (auto &arg : args) arg.typeVars(out);
}

std::ostream &operator<<(std::ostream &os, const AST &ast) {
  os << ast.name;
  for (auto &x : ast.args) os << " (" << x << ")";
  return os;
}
