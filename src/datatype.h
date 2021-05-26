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

#ifndef DATA_TYPE_H
#define DATA_TYPE_H

#include "location.h"
#include "optional.h"
#include <vector>
#include <string>
#include <map>
#include <ostream>
#include <memory>

struct TypeVar;
struct AST {
  Location token, region;
  std::string name;
  std::string tag;
  optional<AST> type;
  std::vector<AST> args;

  AST(const Location &token_, std::string &&name_, std::vector<AST> &&args_) :
    token(token_), region(token_), name(std::move(name_)), args(std::move(args_)) { }
  AST(const Location &token_, std::string &&name_) :
    token(token_), region(token_), name(std::move(name_)) { }
  AST(const Location &token_) :
    token(token_), region(token_) { }

  bool unify(TypeVar &out, const std::map<std::string, TypeVar*> &ids);
  operator bool() const { return !name.empty(); }
};

std::ostream & operator << (std::ostream &os, const AST &ast);

struct Sum;
struct Expr;
struct Constructor {
  AST ast;
  int index; // sum->members[index] = this
  bool scoped;

  Constructor(AST &&ast_) : ast(ast_), index(0), scoped(false) { }
  static Constructor array;
};

struct Sum {
  std::string name;
  Location token, region;
  std::vector<std::string> args;
  std::vector<Constructor> members;
  bool scoped;

  Sum(AST &&ast);
  void addConstructor(AST &&ast);
};

#endif
