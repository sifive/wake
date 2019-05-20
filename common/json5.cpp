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

#include "location.h"
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

static char hex(unsigned char x) {
  if (x < 10) return '0' + x;
  return 'a' + x - 10;
}

std::string json_escape(const std::string &x) {
  std::string out;
  char escape[] = "\\u0000";
  for (char z : x) {
    unsigned char c = z;
    if (z == '"') out.append("\\\"");
    else if (z == '\\') out.append("\\\\");
    else if (c >= 0x20) {
      out.push_back(c);
    } else {
      escape[4] = hex(c >> 4);
      escape[5] = hex(c & 0xf);
      out.append(escape);
    }
  }
  return out;
}
