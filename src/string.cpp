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
#include "status.h"
#include "utf8.h"
#include "gc.h"
#include <sstream>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <errno.h>
#include <string.h>
#include <utf8proc.h>
#include <unistd.h>

struct CatStream final : public GCObject<CatStream, DestroyableObject> {
  typedef GCObject<CatStream, DestroyableObject> Parent;

  static TypeVar typeVar;
  std::stringstream str;

  CatStream(Heap &h) : Parent(h) { }
  CatStream(CatStream &&c) = default;

  void format(std::ostream &os, FormatState &state) const;
};

void CatStream::format(std::ostream &os, FormatState &state) const {
  if (APP_PRECEDENCE < state.p()) os << "(";
  os << "_CatStream";
  std::string s = str.str();
  String::cstr_format(os, s.c_str(), s.size());
  if (APP_PRECEDENCE < state.p()) os << ")";
}

TypeVar CatStream::typeVar("_CatStream", 0);

#define CATSTREAM(arg, i) do { HeapObject *arg = args[i]; REQUIRE(typeid(*arg) == typeid(CatStream));  } while(0); CatStream *arg = static_cast<CatStream*>(args[i]);

static PRIMTYPE(type_catopen) {
  return args.size() == 0 &&
    out->unify(CatStream::typeVar);
}

static PRIMFN(prim_catopen) {
  EXPECT(0);
  auto out = CatStream::alloc(runtime.heap, runtime.heap);
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
  arg0->str.write(arg1->c_str(), arg1->length);
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
  RETURN(String::alloc(runtime.heap, arg0->str.str()));
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

  // worst-case estimate
  size_t need = reserve_list(arg0->length) + arg0->length * String::reserve(4);
  runtime.heap.reserve(need);

  std::vector<HeapObject*> vals;
  uint32_t rune;

  int got;
  for (const char *ptr = arg0->c_str(); *ptr; ptr += got) {
    got = pop_utf8(&rune, ptr);
    if (got < 1) got = 1;
    vals.push_back(String::claim(runtime.heap, ptr, got));
  }

  RETURN(claim_list(runtime.heap, vals.size(), vals.data()));
}

static PRIMTYPE(type_read) {
  TypeVar result;
  Data::typeResult.clone(result);
  result[0].unify(String::typeVar);
  result[1].unify(String::typeVar);
  return args.size() == 1 &&
    args[0]->unify(String::typeVar) &&
    out->unify(result);
}

static PRIMFN(prim_read) {
  EXPECT(1);
  STRING(path, 0);

  size_t max_error = path->length + 100;
  size_t need_fail = reserve_result() + String::reserve(max_error);
  runtime.heap.reserve(need_fail);

  std::ifstream t(path->c_str(), std::ios::in | std::ios::binary);
  if (t) {
    t.seekg(0, t.end);
    auto size = t.tellg();

    if (size != -1) {
      size_t need_pass = need_fail + String::reserve(size);
      runtime.heap.reserve(need_pass);

      String *out = String::claim(runtime.heap, size);
      t.seekg(0, t.beg);
      t.read(out->c_str(), out->length);
      if (t) RETURN(claim_result(runtime.heap, true, out));
    }
  }

  std::stringstream str;
  str << "read " << path->c_str() << ": " << strerror(errno);
  std::string s = str.str();

  size_t len = std::min(s.size(), max_error);
  String *out = String::claim(runtime.heap, s.c_str(), len);
  RETURN(claim_result(runtime.heap, false, out));
}

static PRIMTYPE(type_write) {
  TypeVar result;
  Data::typeResult.clone(result);
  result[0].unify(String::typeVar);
  result[1].unify(String::typeVar);
  return args.size() == 3 &&
    args[0]->unify(Integer::typeVar) &&
    args[1]->unify(String::typeVar) &&
    args[2]->unify(String::typeVar) &&
    out->unify(result);
}

static PRIMFN(prim_write) {
  EXPECT(3);
  INTEGER_MPZ(mode, 0);
  STRING(path, 1);
  STRING(body, 2);

  // Reservation must happen first so we don't have re-entrant side-effects
  size_t max_error = path->length + 100;
  size_t need = reserve_result() + String::reserve(max_error);
  runtime.heap.reserve(need);

  REQUIRE(mpz_cmp_si(mode, 0) >= 0);
  REQUIRE(mpz_cmp_si(mode, 0x1ff) <= 0);
  long mask = mpz_get_si(mode);

  std::ofstream t(path->c_str(), std::ios_base::trunc);
  if (!t.fail()) {
    t.write(body->c_str(), body->length);
    if (!t.bad()) {
      chmod(path->c_str(), mask);
      auto out = claim_result(runtime.heap, true, args[1]);
      RETURN(out);
    }
  }

  std::stringstream str;
  str << "write " << path->c_str() << ": " << strerror(errno);
  std::string s = str.str();

  size_t len = std::min(s.size(), max_error);
  String *out = String::claim(runtime.heap, s.c_str(), len);
  RETURN(claim_result(runtime.heap, false, out));
}

static PRIMTYPE(type_unlink) {
  return args.size() == 1 &&
    args[0]->unify(String::typeVar) &&
    out->unify(Data::typeUnit);
}

static PRIMFN(prim_unlink) {
  EXPECT(1);
  STRING(path, 0);

  // Reservation must happen first so we don't have re-entrant side-effects
  runtime.heap.reserve(reserve_unit());

  // don't care if this succeeds
  (void)unlink(path->c_str());

  RETURN(claim_unit(runtime.heap));
}

static PRIMTYPE(type_getenv) {
  TypeVar list;
  Data::typeList.clone(list);
  list[0].unify(String::typeVar);
  return args.size() == 1 &&
    args[0]->unify(String::typeVar) &&
    out->unify(list);
}

static PRIMFN(prim_getenv) {
  EXPECT(1);
  STRING(arg0, 0);
  const char *env = getenv(arg0->c_str());
  if (env) {
    size_t len = strlen(env);
    size_t need = reserve_list(1) + String::reserve(len);
    runtime.heap.reserve(need);
    HeapObject *out = String::claim(runtime.heap, env, len);
    RETURN(claim_list(runtime.heap, 1, &out));
  } else {
    RETURN(alloc_nil(runtime.heap));
  }
}

static PRIMTYPE(type_mkdir) {
  TypeVar result;
  Data::typeResult.clone(result);
  result[0].unify(String::typeVar);
  result[1].unify(String::typeVar);
  return args.size() == 2 &&
    args[0]->unify(Integer::typeVar) &&
    args[1]->unify(String::typeVar) &&
    out->unify(result);
}

static PRIMFN(prim_mkdir) {
  EXPECT(2);
  INTEGER_MPZ(mode, 0);
  STRING(path, 1);

  // Reservation must happen first so we don't have re-entrant side-effects
  size_t max_error = path->length + 100;
  size_t need = reserve_result() + String::reserve(max_error);
  runtime.heap.reserve(need);

  REQUIRE(mpz_cmp_si(mode, 0) >= 0);
  REQUIRE(mpz_cmp_si(mode, 0x1ff) <= 0);
  long mask = mpz_get_si(mode);

  if (mkdir(path->c_str(), mask) != 0 && errno != EEXIST && errno != EISDIR) {
    std::stringstream str;
    str << "mkdir " << path->c_str() << ": " << strerror(errno);
    std::string s = str.str();

    size_t len = std::min(s.size(), max_error);
    String *out = String::claim(runtime.heap, s.c_str(), len);
    RETURN(claim_result(runtime.heap, false, out));
  } else {
    RETURN(claim_result(runtime.heap, true, args[1]));
  }
}

static PRIMTYPE(type_format) {
  return args.size() == 1 &&
    // don't unify args[0] => allow any
    out->unify(String::typeVar);
}

static PRIMFN(prim_format) {
  EXPECT(1);
  std::stringstream buffer;
  buffer << args[0];
  RETURN(String::alloc(runtime.heap, buffer.str()));
}

static PRIMTYPE(type_print) {
  return args.size() == 2 &&
    args[0]->unify(Integer::typeVar) &&
    args[1]->unify(String::typeVar) &&
    out->unify(Data::typeUnit);
}

static PRIMFN(prim_print) {
  EXPECT(2);
  INTEGER_MPZ(fd, 0);
  STRING(message, 1);
  runtime.heap.reserve(reserve_unit());
  status_write(mpz_get_si(fd), message->c_str(), message->length);
  RETURN(claim_unit(runtime.heap));
}

static PRIMTYPE(type_version) {
  return args.size() == 0 &&
    out->unify(String::typeVar);
}

static PRIMFN(prim_version) {
  EXPECT(0);
  StringInfo *info = reinterpret_cast<StringInfo*>(data);
  RETURN(String::alloc(runtime.heap, info->version));
}

static PRIMTYPE(type_level) {
  return args.size() == 0 &&
    out->unify(Integer::typeVar);
}

static PRIMFN(prim_level) {
  EXPECT(0);
  StringInfo *info = reinterpret_cast<StringInfo*>(data);

  int x;
  if (info->quiet) {
    x = 0;
  } else if (info->verbose) {
    if (info->debug) {
      x = 3;
    } else {
      x = 2;
    }
  } else {
    x = 1;
  }

  MPZ out(x);
  RETURN(Integer::alloc(runtime.heap, out));
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
  RETURN(alloc_order(runtime.heap, arg0->compare(*arg1)));
}

static PRIMTYPE(type_normalize) {
  return args.size() == 1 &&
    args[0]->unify(String::typeVar) &&
    out->unify(String::typeVar);
}

struct UTF8Out {
  String *in;
  utf8proc_uint8_t *dst;
  ssize_t len;

  ~UTF8Out() { if (len >= 0) free(dst); }
  UTF8Out(String *in_, unsigned long opt) : in(in_) {
    len = utf8proc_map(
      reinterpret_cast<const utf8proc_uint8_t*>(in->c_str()),
      in->length,
      &dst,
      static_cast<utf8proc_option_t>(opt));
  }

  String *copy(Heap &heap) const {
    if (len < 0) return in;
    return String::alloc(heap,
      reinterpret_cast<const char*>(dst),
      static_cast<size_t>(len));
  }
};

static PRIMFN(prim_sNFC) {
  EXPECT(1);
  STRING(arg0, 0);

  UTF8Out out(arg0,
    UTF8PROC_COMPOSE |
    UTF8PROC_REJECTNA);

  RETURN(out.copy(runtime.heap));
}

static PRIMFN(prim_sNFKC) {
  EXPECT(1);
  STRING(arg0, 0);

  UTF8Out out(arg0,
    UTF8PROC_COMPOSE   |
    UTF8PROC_COMPAT    |
    UTF8PROC_IGNORE    |
    UTF8PROC_LUMP      |
    UTF8PROC_REJECTNA);

  RETURN(out.copy(runtime.heap));
}

static PRIMFN(prim_scaseNFKC) {
  EXPECT(1);
  STRING(arg0, 0);

  UTF8Out out(arg0,
    UTF8PROC_COMPOSE   |
    UTF8PROC_COMPAT    |
    UTF8PROC_IGNORE    |
    UTF8PROC_LUMP      |
    UTF8PROC_CASEFOLD  |
    UTF8PROC_REJECTNA);

  RETURN(out.copy(runtime.heap));
}

static PRIMTYPE(type_code2str) {
  return args.size() == 1 &&
    args[0]->unify(Integer::typeVar) &&
    out->unify(String::typeVar);
}

static PRIMFN(prim_code2str) {
  EXPECT(1);
  INTEGER_MPZ(arg0, 0);
  std::string str;
  bool ok = mpz_fits_slong_p(arg0);
  long x = ok ? mpz_get_si(arg0) : 0;
  ok = ok && x >= 0;
  ok = ok && push_utf8(str, x);
  RETURN(String::alloc(runtime.heap, ok ? str : ""));
}

static PRIMFN(prim_bin2str) {
  EXPECT(1);
  INTEGER_MPZ(arg0, 0);
  bool ok = mpz_fits_slong_p(arg0);
  long x = ok ? mpz_get_si(arg0) : 0;
  ok = ok && x >= 0 && x < 256;
  char c[2] = { static_cast<char>(x), 0 };
  RETURN(String::alloc(runtime.heap, ok ? c : ""));
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
  int x = pop_utf8(&rune, arg0->c_str());
  MPZ out(x >= 1 ? rune : arg0->c_str()[0]);
  RETURN(Integer::alloc(runtime.heap, out));
}

static PRIMFN(prim_str2bin) {
  EXPECT(1);
  STRING(arg0, 0);
  MPZ out(static_cast<unsigned char>(arg0->c_str()[0]));
  RETURN(Integer::alloc(runtime.heap, out));
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
  REQUIRE (ret == 0);

  size_t slen = strlen(uts.sysname);
  size_t mlen = strlen(uts.machine);
  size_t need = reserve_tuple2() + String::reserve(slen) + String::reserve(mlen);
  runtime.heap.reserve(need);

  auto out = claim_tuple2(
    runtime.heap,
    String::claim(runtime.heap, uts.sysname, slen),
    String::claim(runtime.heap, uts.machine, mlen));
  RETURN(out);
}

void prim_register_string(PrimMap &pmap, StringInfo *info) {
  // cat* use mutation and 'read' execution order can matter => not pure
  prim_register(pmap, "catopen",  prim_catopen,  type_catopen,             PRIM_SHALLOW);
  prim_register(pmap, "catadd",   prim_catadd,   type_catadd,              PRIM_SHALLOW);
  prim_register(pmap, "catclose", prim_catclose, type_catclose,            PRIM_SHALLOW);
  prim_register(pmap, "explode",  prim_explode,  type_explode,   PRIM_PURE|PRIM_SHALLOW);
  prim_register(pmap, "unlink",   prim_unlink,   type_unlink,              PRIM_SHALLOW);
  prim_register(pmap, "write",    prim_write,    type_write,               PRIM_SHALLOW);
  prim_register(pmap, "read",     prim_read,     type_read,                PRIM_SHALLOW);
  prim_register(pmap, "getenv",   prim_getenv,   type_getenv,    PRIM_PURE|PRIM_SHALLOW);
  prim_register(pmap, "mkdir",    prim_mkdir,    type_mkdir,               PRIM_SHALLOW);
  prim_register(pmap, "format",   prim_format,   type_format,    PRIM_PURE);
  prim_register(pmap, "print",    prim_print,    type_print,               PRIM_SHALLOW);
  prim_register(pmap, "version",  prim_version,  type_version,   PRIM_PURE|PRIM_SHALLOW, (void*)info);
  prim_register(pmap, "level",    prim_level,    type_level,     PRIM_PURE|PRIM_SHALLOW, (void*)info);
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
