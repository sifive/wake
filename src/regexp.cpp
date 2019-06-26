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

#include "prim.h"
#include "value.h"
#include "heap.h"
#include "hash.h"
#include "type.h"
#include "sfinae.h"
#include <iostream>
#include <string>
#include <iosfwd>

static PRIMTYPE(type_re2) {
  TypeVar result;
  Data::typeResult.clone(result);
  result[0].unify(RegExp::typeVar);
  result[1].unify(String::typeVar);
  return args.size() == 1 &&
    args[0]->unify(String::typeVar) &&
    out->unify(result);
}

static PRIMFN(prim_re2) {
  EXPECT(1);
  STRING(arg0, 0);
  auto pass = std::make_shared<RegExp>(arg0->value);
  if (pass->exp.ok()) {
    auto out = make_result(true, std::move(pass));
    RETURN(out);
  } else {
    auto out = make_result(false, std::make_shared<String>(pass->exp.error()));
    RETURN(out);
  }
}

static PRIMTYPE(type_re2str) {
  return args.size() == 1 &&
    args[0]->unify(RegExp::typeVar) &&
    out->unify(String::typeVar);
}

TEST_MEMBER(set_dot_nl);

static PRIMFN(prim_re2str) {
  EXPECT(1);
  REGEXP(arg0, 0);
  auto out =
    has_set_dot_nl<RE2::Options>::value
    ? std::make_shared<String>(arg0->exp.pattern())
    : std::make_shared<String>(arg0->exp.pattern().substr(4)); // skip the (?s)
  RETURN(out);
}

static PRIMTYPE(type_quote) {
  return args.size() == 1 &&
    args[0]->unify(String::typeVar) &&
    out->unify(String::typeVar);
}

static PRIMFN(prim_quote) {
  EXPECT(1);
  STRING(arg0, 0);
  auto out = std::make_shared<String>(RE2::QuoteMeta(arg0->value));
  RETURN(out);
}

static PRIMTYPE(type_match) {
  return args.size() == 2 &&
    args[0]->unify(RegExp::typeVar) &&
    args[1]->unify(String::typeVar) &&
    out->unify(Data::typeBoolean);
}

static PRIMFN(prim_match) {
  EXPECT(2);
  REGEXP(arg0, 0);
  STRING(arg1, 1);
  auto out = make_bool(RE2::FullMatch(arg1->value, arg0->exp));
  RETURN(out);
}

static PRIMTYPE(type_extract) {
  TypeVar list;
  Data::typeList.clone(list);
  list[0].unify(String::typeVar);
  return args.size() == 2 &&
    args[0]->unify(RegExp::typeVar) &&
    args[1]->unify(String::typeVar) &&
    out->unify(list);
}

static PRIMFN(prim_extract) {
  EXPECT(2);
  REGEXP(arg0, 0);
  STRING(arg1, 1);

  int matches = arg0->exp.NumberOfCapturingGroups() + 1;
  std::vector<re2::StringPiece> submatch(matches, nullptr);
  re2::StringPiece input(arg1->value);

  std::vector<std::shared_ptr<Value> > strings;
  if (arg0->exp.Match(input, 0, arg1->value.size(), RE2::ANCHOR_BOTH, submatch.data(), matches)) {
    strings.reserve(matches);
    for (int i = 1; i < matches; ++i) strings.emplace_back(std::make_shared<String>(submatch[i].as_string()));
  }

  auto out = make_list(std::move(strings));
  RETURN(out);
}

static PRIMTYPE(type_replace) {
  return args.size() == 3 &&
    args[0]->unify(RegExp::typeVar) &&
    args[1]->unify(String::typeVar) &&
    args[2]->unify(String::typeVar) &&
    out->unify(String::typeVar);
}

static PRIMFN(prim_replace) {
  EXPECT(3);
  REGEXP(arg0, 0);
  STRING(arg1, 1);
  STRING(arg2, 2);

  auto out = std::make_shared<String>(arg2->value);
  RE2::GlobalReplace(&out->value, arg0->exp, arg1->value);
  RETURN(out);
}

static PRIMTYPE(type_tokenize) {
  TypeVar list;
  Data::typeList.clone(list);
  list[0].unify(String::typeVar);
  return args.size() == 2 &&
    args[0]->unify(RegExp::typeVar) &&
    args[1]->unify(String::typeVar) &&
    out->unify(list);
}

static PRIMFN(prim_tokenize) {
  EXPECT(2);
  REGEXP(arg0, 0);
  STRING(arg1, 1);
  re2::StringPiece input(arg1->value);
  re2::StringPiece hit;
  std::vector<std::shared_ptr<Value> > tokens;
  while (arg0->exp.Match(input, 0, input.size(), RE2::UNANCHORED, &hit, 1)) {
    if (hit.empty()) break;
    re2::StringPiece token(input.data(), hit.data() - input.data());
    tokens.emplace_back(std::make_shared<String>(token.as_string()));
    input.remove_prefix(token.size() + hit.size());
  }
  tokens.emplace_back(std::make_shared<String>(input.as_string()));
  auto out = make_list(std::move(tokens));
  RETURN(out);
}

void prim_register_regexp(PrimMap &pmap) {
  prim_register(pmap, "re2",      prim_re2,      type_re2,      PRIM_PURE|PRIM_SHALLOW);
  prim_register(pmap, "re2str",   prim_re2str,   type_re2str,   PRIM_PURE|PRIM_SHALLOW);
  prim_register(pmap, "quote",    prim_quote,    type_quote,    PRIM_PURE|PRIM_SHALLOW);
  prim_register(pmap, "match",    prim_match,    type_match,    PRIM_PURE|PRIM_SHALLOW);
  prim_register(pmap, "extract",  prim_extract,  type_extract,  PRIM_PURE|PRIM_SHALLOW);
  prim_register(pmap, "replace",  prim_replace,  type_replace,  PRIM_PURE|PRIM_SHALLOW);
  prim_register(pmap, "tokenize", prim_tokenize, type_tokenize, PRIM_PURE|PRIM_SHALLOW);
}
