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
#include <string.h>
#include <errno.h>

static bool expect(SymbolJSON type, JLexer &jlex, std::ostream& errs) {
  if (jlex.next.type != type) {
    if (!jlex.fail)
      errs << "Was expecting a "
        << jsymbolTable[type] << ", but got a "
        << jsymbolTable[jlex.next.type] << " at "
        << jlex.next.location.text();
    jlex.fail = true;
    return false;
  }
  return true;
}


static JAST parse_jvalue(JLexer &jlex, std::ostream& errs);

// JSON5Array:
//   []
//   [JSON5ElementList ,opt]
// JSON5ElementList:
//   JSON5Value
//   JSON5ElementList , JSON5Value
static JAST parse_jarray(JLexer &jlex, std::ostream& errs) {
  jlex.consume();

  bool repeat = true;
  JChildren values;

  while (repeat) {
    if (jlex.next.type == JSON_SCLOSE) {
      jlex.consume();
      break;
    }

    values.emplace_back("", parse_jvalue(jlex, errs));
    switch (jlex.next.type) {
      case JSON_COMMA: {
        jlex.consume();
        break;
      }
      case JSON_SCLOSE: {
        jlex.consume();
        repeat = false;
        break;
      }
      default: {
        if (!jlex.fail)
          errs << "Was expecting COMMA/SCLOSE, got a "
            << jsymbolTable[jlex.next.type]
            << " at " << jlex.next.location.text();
        jlex.fail = true;
        repeat = false;
        break;
      }
    }
  }

  return JAST(JSON_ARRAY, std::move(values));
}

// JSON5Object:
//   {}
//   {JSON5MemberList ,opt}
// JSON5MemberList:
//   JSON5Member
//   JSON5MemberList , JSON5Member
// JSON5Member:
//   JSON5MemberName : JSON5Value
// JSON5MemberName:
//   JSON5Identifier
//   JSON5String
static JAST parse_jobject(JLexer &jlex, std::ostream& errs) {
  jlex.consume();

  bool repeat = true;
  JChildren values;

  while (repeat) {
    if (jlex.next.type == JSON_BCLOSE) {
      jlex.consume();
      break;
    }

    // Extract the JSON key
    std::string key;
    switch (jlex.next.type) {
      case JSON_ID:
      case JSON_STR: {
        key = std::move(jlex.next.value);
        jlex.consume();
        break;
      }
      default: {
        if (!jlex.fail)
          errs << "Was expecting ID/STR, got a "
            << jsymbolTable[jlex.next.type]
            << " at " << jlex.next.location.text();
        jlex.fail = true;
        repeat = false;
        break;
      }
    }

    expect(JSON_COLON, jlex, errs);
    jlex.consume();

    values.emplace_back(std::move(key), parse_jvalue(jlex, errs));

    switch (jlex.next.type) {
      case JSON_COMMA: {
        jlex.consume();
        break;
      }
      case JSON_BCLOSE: {
        jlex.consume();
        repeat = false;
        break;
      }
      default: {
        if (!jlex.fail)
          errs << "Was expecting COMMA/BCLOSE, got a "
            << jsymbolTable[jlex.next.type]
            << " at " << jlex.next.location.text();
        jlex.fail = true;
        repeat = false;
        break;
      }
    }
  }

  return JAST(JSON_OBJECT, std::move(values));
}

// JSON5Value:
//   JSON5Null
//   JSON5Boolean
//   JSON5String
//   JSON5Number
//   JSON5Object
//   JSON5Array
static JAST parse_jvalue(JLexer &jlex, std::ostream& errs) {
  switch (jlex.next.type) {
    case JSON_NULLVAL: 
    case JSON_TRUE:
    case JSON_FALSE:
    case JSON_NAN: {
      auto out = JAST(jlex.next.type);
      jlex.consume();
      return out;
    }
    case JSON_INTEGER:
    case JSON_DOUBLE:
    case JSON_INFINITY:
    case JSON_STR: {
      auto out = JAST(jlex.next.type, std::move(jlex.next.value));
      jlex.consume();
      return out;
    }
    case JSON_BOPEN: {
      return parse_jobject(jlex, errs);
    }
    case JSON_SOPEN: {
      return parse_jarray(jlex, errs);
    }
    default: {
      if (!jlex.fail)
        errs << "Unexpected symbol "
          << jsymbolTable[jlex.next.type]
          << " at " << jlex.next.location.text();
      jlex.fail = true;
      return JAST(JSON_ERROR);
    }
  }
}

// JSON5Text:
//   JSON5Value

bool JAST::parse(const char *file, std::ostream& errs, JAST &out) {
  JLexer jlex(file);
  if (jlex.fail) {
    errs << "Open " << file << ": " << strerror(errno);
    return false;
  } else {
    out = parse_jvalue(jlex, errs);
    expect(JSON_END, jlex, errs);
    return !jlex.fail;
  }
}

bool JAST::parse(std::string &body, std::ostream& errs, JAST &out) {
  JLexer jlex(body);
  out = parse_jvalue(jlex, errs);
  expect(JSON_END, jlex, errs);
  return !jlex.fail;
}
