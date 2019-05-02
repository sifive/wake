/*
 * Copyright 2019 SiFive, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
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
#include <vector>
#include <string>
#include <map>
#include <ostream>
#include <memory>

struct TypeVar;
struct AST {
  Location location;
  std::string name;
  std::string tag;
  std::vector<AST> args;

  AST(const Location &location_, std::string &&name_, std::vector<AST> &&args_) :
    location(location_), name(std::move(name_)), args(std::move(args_)) { }
  AST(const Location &location_, std::string &&name_) :
    location(location_), name(std::move(name_)) { }
  AST(const Location &location_) :
    location(location_) { }

  bool unify(TypeVar &out, const std::map<std::string, TypeVar*> &ids);
  operator bool() const { return !name.empty(); }
};

std::ostream & operator << (std::ostream &os, const AST &ast);

struct Sum;
struct Expr;
struct Constructor {
  AST ast;
  int index; // sum->members[index] = this
  std::unique_ptr<Expr> expr; // body of chain in: def chain a b c data fn = fn data a b c
  
  Constructor(AST &&ast_) : ast(ast_) { }
};

struct Sum {
  std::string name;
  Location location;
  std::vector<std::string> args;
  std::vector<Constructor> members;

  Sum(AST &&ast);
  void addConstructor(AST &&ast);
};

#endif
