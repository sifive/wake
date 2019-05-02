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
#include "expr.h"
#include "type.h"
#include "symbol.h"
#include "status.h"
#include <sstream>
#include <fstream>
#include <iostream>
#include <iosfwd>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <errno.h>
#include <cstring>
#include <utf8proc.h>

struct CatStream : public Value {
  std::stringstream str;
  static const TypeDescriptor type;
  static TypeVar typeVar;
  CatStream() : Value(&type) { }

  void format(std::ostream &os, FormatState &state) const;
  TypeVar &getType();
  Hash hash() const;
};

const TypeDescriptor CatStream::type("CatStream");

void CatStream::format(std::ostream &os, FormatState &state) const {
  if (APP_PRECEDENCE < state.p()) os << "(";
  os << "CatStream ";
  String(str.str()).format(os, state);
  if (APP_PRECEDENCE < state.p()) os << ")";
}

TypeVar CatStream::typeVar("CatStream", 0);
TypeVar &CatStream::getType() {
  return typeVar;
}

Hash CatStream::hash() const {
  return Hash(str.str()) + type.hashcode;
}

static std::unique_ptr<Receiver> cast_catstream(WorkQueue &queue, std::unique_ptr<Receiver> completion, const std::shared_ptr<Binding> &binding, const std::shared_ptr<Value> &value, CatStream **cat) {
  if (value->type != &CatStream::type) {
    Receiver::receive(queue, std::move(completion), std::make_shared<Exception>(value->to_str() + " is not a CatStream", binding));
    return std::unique_ptr<Receiver>();
  } else {
    *cat = reinterpret_cast<CatStream*>(value.get());
    return completion;
  }
}

#define CATSTREAM(arg, i) 									\
  CatStream *arg;										\
  do {												\
    completion = cast_catstream(queue, std::move(completion), binding, args[i], &arg);		\
    if (!completion) return;									\
  } while(0)

static PRIMTYPE(type_catopen) {
  return args.size() == 0 &&
    out->unify(CatStream::typeVar);
}

static PRIMFN(prim_catopen) {
  EXPECT(0);
  auto out = std::make_shared<CatStream>();
  RETURN(out);
}

static PRIMTYPE(type_catadd) {
  return args.size() == 2 &&
    args[0]->unify(CatStream::typeVar) &&
    args[1]->unify(String::typeVar) &&
    out->unify(CatStream::typeVar);
}

static PRIMFN(prim_catadd) {
  EXPECT(2);
  CATSTREAM(arg0, 0);
  STRING(arg1, 1);
  arg0->str << arg1->value;
  RETURN(args[0]);
}

static PRIMTYPE(type_catclose) {
  return args.size() == 1 &&
    args[0]->unify(CatStream::typeVar) &&
    out->unify(String::typeVar);
}

static PRIMFN(prim_catclose) {
  EXPECT(1);
  CATSTREAM(arg0, 0);
  auto out = std::make_shared<String>(arg0->str.str());
  RETURN(out);
}

static PRIMTYPE(type_explode) {
  TypeVar list;
  Data::typeList.clone(list);
  list[0].unify(String::typeVar);
  return args.size() == 1 &&
    args[0]->unify(String::typeVar) &&
    out->unify(list);
}

static PRIMFN(prim_explode) {
  EXPECT(1);
  STRING(arg0, 0);
  std::vector<std::shared_ptr<Value> > vals;
  uint32_t rune;

  int got;
  for (const char *ptr = arg0->value.c_str(); *ptr; ptr += got) {
    got = pop_utf8(&rune, ptr);
    REQUIRE(got > 0, "Invalid Unicode");
    vals.emplace_back(std::make_shared<String>(std::string(ptr, got)));
  }

  auto out = make_list(std::move(vals));
  RETURN(out);
}

static PRIMTYPE(type_read) {
  return args.size() == 1 &&
    args[0]->unify(String::typeVar) &&
    out->unify(String::typeVar);
}

static PRIMFN(prim_read) {
  EXPECT(1);
  STRING(arg0, 0);
  std::ifstream t(arg0->value);
  REQUIRE(!t.fail(), "Could not read " + arg0->value);
  std::stringstream buffer;
  buffer << t.rdbuf();
  REQUIRE(!t.bad(), "Could not read " + arg0->value);
  auto out = std::make_shared<String>(buffer.str());
  RETURN(out);
}

static PRIMTYPE(type_write) {
  return args.size() == 3 &&
    args[0]->unify(Integer::typeVar) &&
    args[1]->unify(String::typeVar) &&
    args[2]->unify(String::typeVar) &&
    out->unify(String::typeVar);
}

static PRIMFN(prim_write) {
  EXPECT(3);
  INTEGER(mode, 0);
  STRING(path, 1);
  STRING(body, 2);

  REQUIRE(mpz_cmp_si(mode->value, 0) >= 0, "mode must be >= 0");
  REQUIRE(mpz_cmp_si(mode->value, 0xffff) <= 0, "mode must be <= 0xffff");
  long mask = mpz_get_si(mode->value);

  std::ofstream t(path->value, std::ios_base::trunc);
  REQUIRE(!t.fail(), "Could not write " + path->value);
  t << body->value;
  chmod(path->value.c_str(), mask);
  REQUIRE(!t.bad(), "Could not write " + path->value);
  RETURN(args[1]);
}

static PRIMTYPE(type_getenv) {
  return args.size() == 1 &&
    args[0]->unify(String::typeVar) &&
    out->unify(String::typeVar);
}

static PRIMFN(prim_getenv) {
  EXPECT(1);
  STRING(arg0, 0);
  const char *env = getenv(arg0->value.c_str());
  REQUIRE(env, arg0->value + " is unset in the environment");
  auto out = std::make_shared<String>(env);
  RETURN(out);
}

static PRIMTYPE(type_mkdir) {
  return args.size() == 2 &&
    args[0]->unify(Integer::typeVar) &&
    args[1]->unify(String::typeVar) &&
    out->unify(String::typeVar);
}

static PRIMFN(prim_mkdir) {
  EXPECT(2);
  INTEGER(mode, 0);
  STRING(path, 1);

  REQUIRE(mpz_cmp_si(mode->value, 0) >= 0, "mode must be >= 0");
  REQUIRE(mpz_cmp_si(mode->value, 0xffff) <= 0, "mode must be <= 0xffff");
  long mask = mpz_get_si(mode->value);

  if (mkdir(path->value.c_str(), mask) != 0 && errno != EEXIST && errno != EISDIR) {
    std::stringstream str;
    str << path->value << ": " << strerror(errno);
    RAISE(str.str());
  }

  RETURN(args[1]);
}

static PRIMTYPE(type_format) {
  return args.size() == 1 &&
    // don't unify args[0] => allow any
    out->unify(String::typeVar);
}

static PRIMFN(prim_format) {
  REQUIRE(args.size() == 1, "prim_format expects 1 argument");
  (void)data;
  std::stringstream buffer;
  buffer << args[0].get();
  auto out = std::make_shared<String>(buffer.str());
  RETURN(out);
}

static PRIMTYPE(type_print) {
  return args.size() == 1 &&
    args[0]->unify(String::typeVar) &&
    out->unify(Data::typeUnit);
}

static PRIMFN(prim_print) {
  EXPECT(1);
  STRING(arg0, 0);
  status_write(2, arg0->value.data(), arg0->value.size());
  auto out = make_unit();
  RETURN(out);
}

static PRIMTYPE(type_version) {
  return args.size() == 0 &&
    out->unify(String::typeVar);
}

static PRIMFN(prim_version) {
  EXPECT(0);
  auto out = std::make_shared<String>((const char *)data);
  RETURN(out);
}

static PRIMTYPE(type_scmp) {
  return args.size() == 2 &&
    args[0]->unify(String::typeVar) &&
    args[1]->unify(String::typeVar) &&
    out->unify(Data::typeOrder);
}

static PRIMFN(prim_scmp) {
  EXPECT(2);
  STRING(arg0, 0);
  STRING(arg1, 1);
  auto out = make_order(arg0->value.compare(arg1->value));
  RETURN(out);
}

static PRIMTYPE(type_normalize) {
  return args.size() == 1 &&
    args[0]->unify(String::typeVar) &&
    out->unify(String::typeVar);
}

static PRIMFN(prim_sNFC) {
  EXPECT(1);
  STRING(arg0, 0);
  utf8proc_uint8_t *dst;
  ssize_t len = utf8proc_map(
    reinterpret_cast<const utf8proc_uint8_t*>(arg0->value.c_str()),
    arg0->value.size(),
    &dst,
    static_cast<utf8proc_option_t>(
      UTF8PROC_COMPOSE |
      UTF8PROC_REJECTNA));
  REQUIRE(len >= 0, std::string("Could not normalize string ") + utf8proc_errmsg(len));
  auto out = std::make_shared<String>(std::string(
    reinterpret_cast<const char*>(dst),
    static_cast<size_t>(len)));
  free(dst);
  RETURN(out);
}

static PRIMFN(prim_sNFKC) {
  EXPECT(1);
  STRING(arg0, 0);
  utf8proc_uint8_t *dst;
  ssize_t len = utf8proc_map(
    reinterpret_cast<const utf8proc_uint8_t*>(arg0->value.c_str()),
    arg0->value.size(),
    &dst,
    static_cast<utf8proc_option_t>(
      UTF8PROC_COMPOSE   |
      UTF8PROC_COMPAT    |
      UTF8PROC_IGNORE    |
      UTF8PROC_LUMP      |
      UTF8PROC_REJECTNA));
  REQUIRE(len >= 0, std::string("Could not normalize string ") + utf8proc_errmsg(len));
  auto out = std::make_shared<String>(std::string(
    reinterpret_cast<const char*>(dst),
    static_cast<size_t>(len)));
  free(dst);
  RETURN(out);
}

static PRIMFN(prim_scaseNFKC) {
  EXPECT(1);
  STRING(arg0, 0);
  utf8proc_uint8_t *dst;
  ssize_t len = utf8proc_map(
    reinterpret_cast<const utf8proc_uint8_t*>(arg0->value.c_str()),
    arg0->value.size(),
    &dst,
    static_cast<utf8proc_option_t>(
      UTF8PROC_COMPOSE   |
      UTF8PROC_COMPAT    |
      UTF8PROC_IGNORE    |
      UTF8PROC_LUMP      |
      UTF8PROC_CASEFOLD  |
      UTF8PROC_REJECTNA));
  REQUIRE(len >= 0, std::string("Could not normalize string ") + utf8proc_errmsg(len));
  auto out = std::make_shared<String>(std::string(
    reinterpret_cast<const char*>(dst),
    static_cast<size_t>(len)));
  free(dst);
  RETURN(out);
}

static PRIMTYPE(type_code2str) {
  return args.size() == 1 &&
    args[0]->unify(Integer::typeVar) &&
    out->unify(String::typeVar);
}

static PRIMFN(prim_code2str) {
  EXPECT(1);
  INTEGER(arg0, 0);
  std::string str;
  bool ok = mpz_fits_slong_p(arg0->value);
  long x = ok ? mpz_get_si(arg0->value) : 0;
  ok = ok && x >= 0;
  ok = ok && push_utf8(str, x);
  REQUIRE(ok, "Not a valid Unicode codepoint");
  auto out = std::make_shared<String>(std::move(str));
  RETURN(out);
}

static PRIMFN(prim_bin2str) {
  EXPECT(1);
  INTEGER(arg0, 0);
  bool ok = mpz_fits_slong_p(arg0->value);
  long x = ok ? mpz_get_si(arg0->value) : 0;
  ok = ok && x >= 0 && x < 256;
  REQUIRE(ok, "Not a valid byte");
  char c[2] = { static_cast<char>(x), 0 };
  auto out = std::make_shared<String>(std::string(c));
  RETURN(out);
}

static PRIMTYPE(type_str2code) {
  return args.size() == 1 &&
    args[0]->unify(String::typeVar) &&
    out->unify(Integer::typeVar);
}

static PRIMFN(prim_str2code) {
  EXPECT(1);
  STRING(arg0, 0);
  uint32_t rune;
  int x = pop_utf8(&rune, arg0->value.c_str());
  REQUIRE (x >= 1, "Invalid UTF-8");
  auto out = std::make_shared<Integer>(rune);
  RETURN(out);
}

static PRIMFN(prim_str2bin) {
  EXPECT(1);
  STRING(arg0, 0);
  auto out = std::make_shared<Integer>(static_cast<unsigned char>(arg0->value[0]));
  RETURN(out);
}

static PRIMTYPE(type_uname) {
  TypeVar pair;
  Data::typePair.clone(pair);
  pair[0].unify(String::typeVar);
  pair[1].unify(String::typeVar);
  return args.size() == 0 &&
    out->unify(pair);
}

static PRIMFN(prim_uname) {
  EXPECT(0);
  struct utsname uts;
  int ret = uname(&uts);
  REQUIRE (ret == 0, "uname failed");
  auto out = make_tuple2(
    std::make_shared<String>(uts.sysname),
    std::make_shared<String>(uts.machine));
  RETURN(out);
}

void prim_register_string(PrimMap &pmap, const char *version) {
  // cat* use mutation and 'read' execution order can matter => not pure
  prim_register(pmap, "catopen",  prim_catopen,  type_catopen,             PRIM_SHALLOW);
  prim_register(pmap, "catadd",   prim_catadd,   type_catadd,              PRIM_SHALLOW);
  prim_register(pmap, "catclose", prim_catclose, type_catclose,            PRIM_SHALLOW);
  prim_register(pmap, "explode",  prim_explode,  type_explode,   PRIM_PURE|PRIM_SHALLOW);
  prim_register(pmap, "write",    prim_write,    type_write,               PRIM_SHALLOW);
  prim_register(pmap, "read",     prim_read,     type_read,                PRIM_SHALLOW);
  prim_register(pmap, "getenv",   prim_getenv,   type_getenv,    PRIM_PURE|PRIM_SHALLOW);
  prim_register(pmap, "mkdir",    prim_mkdir,    type_mkdir,               PRIM_SHALLOW);
  prim_register(pmap, "format",   prim_format,   type_format,    PRIM_PURE);
  prim_register(pmap, "print",    prim_print,    type_print,               PRIM_SHALLOW);
  prim_register(pmap, "version",  prim_version,  type_version,   PRIM_PURE|PRIM_SHALLOW, (void*)version);
  prim_register(pmap, "scmp",     prim_scmp,     type_scmp,      PRIM_PURE|PRIM_SHALLOW);
  prim_register(pmap, "sNFC",     prim_sNFC,     type_normalize, PRIM_PURE|PRIM_SHALLOW);
  prim_register(pmap, "sNFKC",    prim_sNFKC,    type_normalize, PRIM_PURE|PRIM_SHALLOW);
  prim_register(pmap, "scaseNFKC",prim_scaseNFKC,type_normalize, PRIM_PURE|PRIM_SHALLOW);
  prim_register(pmap, "code2str", prim_code2str, type_code2str,  PRIM_PURE|PRIM_SHALLOW);
  prim_register(pmap, "bin2str",  prim_bin2str,  type_code2str,  PRIM_PURE|PRIM_SHALLOW);
  prim_register(pmap, "str2code", prim_str2code, type_str2code,  PRIM_PURE|PRIM_SHALLOW);
  prim_register(pmap, "str2bin",  prim_str2bin,  type_str2code,  PRIM_PURE|PRIM_SHALLOW);
  prim_register(pmap, "uname",    prim_uname,    type_uname,     PRIM_PURE|PRIM_SHALLOW);
}
