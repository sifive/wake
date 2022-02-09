/*
 * Copyright 2021 SiFive, Inc.
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

#ifndef SYMBOL_DEFINITION_H
#define SYMBOL_DEFINITION_H

#include "util/location.h"

#include <string>
#include <utility>
#include <vector>

enum SymbolKind {
  KIND_PACKAGE     = 4,
  KIND_CLASS       = 5,
  KIND_FUNCTION    = 12,
  KIND_VARIABLE    = 13,
  KIND_STRING      = 15,
  KIND_NUMBER      = 16,
  KIND_BOOLEAN     = 17,
  KIND_ARRAY       = 18,
  KIND_ENUM_MEMBER = 22,
  KIND_OPERATOR    = 25
};

struct SymbolDefinition {
    std::string name;
    Location location;
    std::string type;
    SymbolKind symbolKind;
    bool isGlobal;
    std::string documentation;
    std::string outerDocumentation;
    std::vector<std::pair<std::string, std::string>> introduces;

    SymbolDefinition(std::string _name, Location _location, std::string _type, SymbolKind _symbolKind, bool _isGlobal) :
    name(std::move(_name)), location(std::move(_location)), type(std::move(_type)), symbolKind(_symbolKind),
    isGlobal(_isGlobal) {}

    bool operator < (const SymbolDefinition &def) const {
      if (location != def.location) return location < def.location;
      if (name != def.name) return name < def.name;
      return type < def.type;
    }
};

#endif
