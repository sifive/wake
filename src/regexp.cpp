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
#include "type.h"
#include "sfinae.h"
#include <string>

static re2::StringPiece sp(String *s) {
  return re2::StringPiece(s->c_str(), s->size());
}

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
  size_t need = reserve_result() + RegExp::reserve();
  runtime.heap.reserve(need);
  RegExp *regexp = RegExp::claim(runtime.heap, runtime.heap, sp(arg0));
  if (regexp->exp->ok()) {
    RETURN(claim_result(runtime.heap, true, regexp));
  } else {
    // claim for RegExp again to guarantee forward progress
    need += String::reserve(regexp->exp->error().size());
    runtime.heap.reserve(need);
    String *fail = String::claim(runtime.heap, regexp->exp->error());
    RETURN(claim_result(runtime.heap, false, fail));
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
    ? arg0->exp->pattern()
    : arg0->exp->pattern().substr(4);
  RETURN(String::alloc(runtime.heap, out));
}

static PRIMTYPE(type_quote) {
  return args.size() == 1 &&
    args[0]->unify(String::typeVar) &&
    out->unify(String::typeVar);
}

static PRIMFN(prim_quote) {
  EXPECT(1);
  STRING(arg0, 0);
  RETURN(String::alloc(runtime.heap, RE2::QuoteMeta(sp(arg0))));
}

static bool check_re2_bug(size_t size) {
  re2::StringPiece sp;
  bool has_bug = sizeof(sp.size()) != sizeof(size_t);
  bool big = (size >> (sizeof(sp.size())*8-1)) != 0;
  return has_bug && big;
}

const char re2_bug[] = "The re2 library is too old (< 2016-09) to be used on inputs larger than 2GiB\n";

#define RE2_BUG(str) do {						\
  if (check_re2_bug(str->size())) {					\
    require_fail(re2_bug, sizeof(re2_bug), runtime, scope);		\
    return;								\
  }									\
} while (0)

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
  RE2_BUG(arg1);

  runtime.heap.reserve(reserve_bool());
  auto out = RE2::FullMatch(sp(arg1), *arg0->exp);
  RETURN(claim_bool(runtime.heap, out));
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
  RE2_BUG(arg1);

  int matches = arg0->exp->NumberOfCapturingGroups();
  re2::StringPiece submatch[matches+1];
  re2::StringPiece input = sp(arg1);

  if (arg0->exp->Match(input, 0, input.size(), RE2::ANCHOR_BOTH, submatch, matches+1)) {
    size_t need = reserve_list(matches);
    for (int i = 0; i < matches; ++i)
      need += String::reserve(submatch[i+1].size());
    runtime.heap.reserve(need);
    // NOTE: if there is not enough space, this routine will be re-entered.
    // This means submatches is recomputed with fresh/correct heap locations.
    HeapObject *out[matches];
    for (int i = 0; i < matches; ++i) {
      re2::StringPiece &p = submatch[i+1];
      out[i] = String::claim(runtime.heap, p.data(), p.size());
    }
    RETURN(claim_list(runtime.heap, matches, out));
  } else {
    RETURN(alloc_nil(runtime.heap));
  }
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

  RE2_BUG(arg1);
  RE2_BUG(arg2);

  std::string buffer = arg2->as_str();
  RE2::GlobalReplace(&buffer, *arg0->exp, sp(arg1));
  RETURN(String::alloc(runtime.heap, buffer));
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
  RE2_BUG(arg1);

  re2::StringPiece input = sp(arg1);
  re2::StringPiece hit;
  std::vector<re2::StringPiece> tokens;
  size_t need = 0;

  while (arg0->exp->Match(input, 0, input.size(), RE2::UNANCHORED, &hit, 1)) {
    if (hit.empty()) break;
    re2::StringPiece token(input.data(), hit.data() - input.data());
    tokens.emplace_back(token);
    need += String::reserve(token.size());
    input.remove_prefix(token.size() + hit.size());
  }
  tokens.emplace_back(input);
  need += String::reserve(input.size());

  need += reserve_list(tokens.size());
  runtime.heap.reserve(need);

  // NOTE: if there is not enough space, this routine will be re-entered.
  // This means tokens is recomputed with fresh/correct heap locations.

  HeapObject *out[tokens.size()];
  for (size_t i = 0; i < tokens.size(); ++i) {
    re2::StringPiece &p = tokens[i];
    out[i] = String::claim(runtime.heap, p.data(), p.size());
  }

  RETURN(claim_list(runtime.heap, tokens.size(), out));
}

void prim_register_regexp(PrimMap &pmap) {
  prim_register(pmap, "re2",      prim_re2,      type_re2,      PRIM_PURE);
  prim_register(pmap, "re2str",   prim_re2str,   type_re2str,   PRIM_PURE);
  prim_register(pmap, "quote",    prim_quote,    type_quote,    PRIM_PURE);
  prim_register(pmap, "match",    prim_match,    type_match,    PRIM_PURE);
  prim_register(pmap, "extract",  prim_extract,  type_extract,  PRIM_PURE);
  prim_register(pmap, "replace",  prim_replace,  type_replace,  PRIM_PURE);
  prim_register(pmap, "tokenize", prim_tokenize, type_tokenize, PRIM_PURE);
}
