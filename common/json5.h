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

#ifndef JSON5_H
#define JSON5_H

#include "location.h"
#include <memory>
#include <string>
#include <vector>
#include <ostream>

enum SymbolJSON {
  // appear in JAST and JSymbol
  JSON_NULLVAL, JSON_TRUE, JSON_FALSE, JSON_NAN,
  JSON_INTEGER, JSON_DOUBLE, JSON_INFINITY, JSON_STR,
  // appear only in JAST
  JSON_OBJECT, JSON_ARRAY,
  // appear only in JSymbol
  JSON_ERROR, JSON_END,
  JSON_SOPEN, JSON_SCLOSE, JSON_BOPEN, JSON_BCLOSE,
  JSON_COLON, JSON_ID, JSON_COMMA
};

extern const char *jsymbolTable[];

struct JAST;
typedef std::vector<std::pair<std::string, JAST> > JChildren;

struct JAST {
  SymbolJSON kind;
  std::string value;
  JChildren children;

  JAST() : kind(JSON_ERROR) { }
  JAST(SymbolJSON kind_) : kind(kind_) { }
  JAST(SymbolJSON kind_, std::string &&value_) : kind(kind_), value(std::move(value_)) { }
  JAST(SymbolJSON kind_, JChildren &&children_) : kind(kind_), children(std::move(children_)) { }

  static bool parse(const char *file,  std::ostream& errs, JAST &out);
  static bool parse(std::string &body, std::ostream& errs, JAST &out);
};

struct JSymbol {
  SymbolJSON type;
  Location location;
  std::string value;

  JSymbol(SymbolJSON type_, const Location &location_) : type(type_), location(location_) { }
  JSymbol(SymbolJSON type_, const Location &location_, std::string &&value_) : type(type_), location(location_), value(std::move(value_)) { }
};

struct jinput_t;

struct JLexer {
  std::unique_ptr<jinput_t> engine;
  JSymbol next;
  bool fail;

  JLexer(const char *file);
  JLexer(const std::string &body);
  ~JLexer();

  void consume();
};

#endif
