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

#include "json5.h"

const char *jsymbolTable[] = {
  // appear in JAST and JSymbol
  "NULLVAL", "TRUE", "FALSE", "NAN",
  "INTEGER", "DOUBLE", "INFINITY", "STR",
  // appear only in JAST
  "OBJECT", "ARRAY",
  // appear only in JSymbol
  "ERROR", "END",
  "SOPEN", "SCLOSE", "BOPEN", "BCLOSE",
  "COLON", "ID", "COMMA"
};

static JAST null(JSON_NULLVAL);

const JAST &JAST::get(const std::string &key) const {
  if (kind == JSON_OBJECT)
    for (auto &x : children)
      if (x.first == key)
        return x.second;
  return null;
}

JAST &JAST::get(const std::string &key) {
  if (kind == JSON_OBJECT)
    for (auto &x : children)
      if (x.first == key)
        return x.second;
  return null;
}

JAST &JAST::add(std::string &&key, SymbolJSON kind, std::string &&value) {
  children.emplace_back(std::move(key), JAST(kind, std::move(value)));
  return children.back().second;
}

static char hex(unsigned char x) {
  if (x < 10) return '0' + x;
  return 'a' + x - 10;
}

std::string json_escape(const char *str, size_t len) {
  std::string out;
  char escape[] = "\\u0000";
  const char *end = str + len;
  for (const char *i = str; i != end; ++i) {
    char z = *i;
    unsigned char c = z;
    if (z == '"') out.append("\\\"");
    else if (z == '\\') out.append("\\\\");
    else if (c >= 0x20) {
      out.push_back(c);
    } else if (z == '\b') {
      out.append("\\b");
    } else if (z == '\f') {
      out.append("\\f");
    } else if (z == '\n') {
      out.append("\\n");
    } else if (z == '\r') {
      out.append("\\r");
    } else if (z == '\t') {
      out.append("\\t");
    } else {
      escape[4] = hex(c >> 4);
      escape[5] = hex(c & 0xf);
      out.append(escape);
    }
  }
  return out;
}

static std::ostream &formatObject(std::ostream &os, const JAST &jast) {
  os << "{";
  for (size_t i = 0; i < jast.children.size(); ++i) {
    if (i != 0) os << ',';
    const JChild &child = jast.children[i];
    os << '"' << json_escape(child.first) << "\":" << child.second;
  }
  return os << "}";
}

static std::ostream &formatArray(std::ostream &os, const JAST &jast) {
  os << "[";
  for (size_t i = 0; i < jast.children.size(); ++i) {
    if (i != 0) os << ',';
    os << jast.children[i].second;
  }
  return os << "]";
}

std::ostream & operator << (std::ostream &os, const JAST &jast) {
  switch (jast.kind) {
    case JSON_NULLVAL:  return os << "null";
    case JSON_TRUE:     return os << "true";
    case JSON_FALSE:    return os << "false";
    case JSON_NAN:      return os << "NaN";
    case JSON_INTEGER:  return os << jast.value;
    case JSON_DOUBLE:   return os << jast.value;
    case JSON_INFINITY: return os << jast.value << "Infinity";
    case JSON_STR:      return os << '"' << json_escape(jast.value) << '"';
    case JSON_OBJECT:   return formatObject(os, jast);
    case JSON_ARRAY:    return formatArray(os, jast);
    default:            return os << "corrupt";
  }
}
