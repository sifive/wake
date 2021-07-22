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

// Open Group Base Specifications Issue 7
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <sys/stat.h>
#include <sys/utsname.h>
#include <errno.h>
#include <string.h>
#include <utf8proc.h>
#include <unistd.h>
#include <fcntl.h>

#include <sstream>
#include <fstream>
#include <iostream>

#include "runtime/prim.h"
#include "runtime/value.h"
#include "types/type.h"
#include "cli/status.h"
#include "utf8.h"
#include "runtime/gc.h"
#include "shell.h"
#include "unlink.h"

static PRIMTYPE(type_vcat) {
  bool ok = out->unify(String::typeVar);
  for (auto x : args) ok &= x->unify(String::typeVar);
  return ok;
}

static PRIMFN(prim_vcat) {
  (void)data;

  size_t size = 0;
  for (size_t i = 0; i < nargs; ++i) {
    STRING(s, i);
    size += s->size();
  }

  String *out = String::alloc(runtime.heap, size);
  out->c_str()[size] = 0;

  size = 0;
  for (size_t i = 0; i < nargs; ++i) {
    String *s = static_cast<String*>(args[i]);
    memcpy(out->c_str() + size, s->c_str(), s->size());
    size += s->size();
  }

  RETURN(out);
}

static PRIMTYPE(type_strlen) {
  return args.size() == 1 &&
    args[0]->unify(String::typeVar) &&
    out->unify(Integer::typeVar);
}

static PRIMFN(prim_strlen) {
  EXPECT(1);
  STRING(arg, 0);
  MPZ out(arg->size());
  RETURN(Integer::alloc(runtime.heap, out));
}

static PRIMTYPE(type_lcat) {
  TypeVar list;
  Data::typeList.clone(list);
  list[0].unify(String::typeVar);
  return args.size() == 1 &&
    args[0]->unify(list) &&
    out->unify(String::typeVar);
}

struct CCat final : public GCObject<CCat, Continuation> {
  HeapPointer<Record> list;
  HeapPointer<Record> progress;
  HeapPointer<Scope> scope;
  size_t output;

  CCat(Record *list_, Scope *scope_, size_t output_) : list(list_), progress(list_), scope(scope_), output(output_) { }

  template <typename T, T (HeapPointerBase::*memberfn)(T x)>
  T recurse(T arg) {
    arg = Continuation::recurse<T, memberfn>(arg);
    arg = (list.*memberfn)(arg);
    arg = (progress.*memberfn)(arg);
    arg = (scope.*memberfn)(arg);
    return arg;
  }

  void execute(Runtime &runtime) override;
};

void CCat::execute(Runtime &runtime) {
  while (progress->size() == 2 && *progress->at(0) && *progress->at(1))
    progress = progress->at(1)->coerce<Record>();

  if (progress->size() == 2) {
    next = nullptr; // reschedule
    if (*progress->at(0)) {
      progress->at(1)->await(runtime, this);
    } else {
      progress->at(0)->await(runtime, this);
    }
  } else {
    size_t size = 0;
    for (Record *scan = list.get(); scan->size() == 2; scan = scan->at(1)->coerce<Record>())
      size += scan->at(0)->coerce<String>()->size();

    String *out = String::alloc(runtime.heap, size);
    out->c_str()[size] = 0;

    size = 0;
    for (Record *scan = list.get(); scan->size() == 2; scan = scan->at(1)->coerce<Record>()) {
      String *s = scan->at(0)->coerce<String>();
      memcpy(out->c_str() + size, s->c_str(), s->size());
      size += s->size();
    }

    scope->at(output)->fulfill(runtime, out);
  }
}

static PRIMFN(prim_lcat) {
  EXPECT(1);
  RECORD(list, 0);
  runtime.schedule(CCat::alloc(runtime.heap, list, scope, output));
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
  size_t need = reserve_list(arg0->size()) + arg0->size() * String::reserve(4);
  runtime.heap.reserve(need);

  std::vector<Value*> vals;
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

  size_t max_error = path->size() + 100;
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
      t.read(out->c_str(), out->size());
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
  size_t max_error = path->size() + 100;
  size_t need = reserve_result() + String::reserve(max_error);
  runtime.heap.reserve(need);

  REQUIRE(mpz_cmp_si(mode, 0) >= 0);
  REQUIRE(mpz_cmp_si(mode, 0x1ff) <= 0);
  long mask = mpz_get_si(mode);

  deep_unlink(AT_FDCWD, path->c_str());
  std::ofstream t(path->c_str(), std::ios_base::trunc);
  if (!t.fail()) {
    t.write(body->c_str(), body->size());
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
    Value *out = String::claim(runtime.heap, env, len);
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
  size_t max_error = path->size() + 100;
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

struct CFormat final : public GCObject<CFormat, Continuation> {
  HeapPointer<HeapObject> obj;
  HeapPointer<Continuation> cont;

  CFormat(HeapObject *obj_, Continuation *cont_) : obj(obj_), cont(cont_) { }

  template <typename T, T (HeapPointerBase::*memberfn)(T x)>
  T recurse(T arg) {
    arg = Continuation::recurse<T, memberfn>(arg);
    arg = (obj.*memberfn)(arg);
    arg = (cont.*memberfn)(arg);
    return arg;
  }

  void execute(Runtime &runtime) override;
};

void CFormat::execute(Runtime &runtime) {
  std::stringstream buffer;
  buffer << obj.get();
  cont->resume(runtime, String::alloc(runtime.heap, buffer.str()));
}

static PRIMFN(prim_format) {
  EXPECT(1);
  runtime.heap.reserve(Tuple::fulfiller_pads + reserve_hash() + CFormat::reserve());
  runtime.schedule(claim_hash(runtime.heap, args[0],
    CFormat::claim(runtime.heap, args[0],
      scope->claim_fulfiller(runtime, output))));
}

static PRIMTYPE(type_colour) {
  return args.size() == 2 &&
    args[0]->unify(String::typeVar) &&
    args[1]->unify(Integer::typeVar) &&
    out->unify(Data::typeUnit);
}

static PRIMFN(prim_colour) {
  EXPECT(2);
  STRING(stream, 0);
  INTEGER_MPZ(code, 1);
  runtime.heap.reserve(reserve_unit());
  status_set_colour(stream->c_str(), mpz_get_si(code));
  RETURN(claim_unit(runtime.heap));
}

static PRIMTYPE(type_print) {
  return args.size() == 2 &&
    args[0]->unify(String::typeVar) &&
    args[1]->unify(String::typeVar) &&
    out->unify(Data::typeUnit);
}

static PRIMFN(prim_print) {
  EXPECT(2);
  STRING(stream, 0);
  STRING(message, 1);
  runtime.heap.reserve(reserve_unit());
  status_write(stream->c_str(), message->c_str(), message->size());
  RETURN(claim_unit(runtime.heap));
}

static PRIMTYPE(type_version) {
  return args.size() == 0 &&
    out->unify(String::typeVar);
}

static PRIMFN(prim_version) {
  EXPECT(0);
  StringInfo *info = static_cast<StringInfo*>(data);
  RETURN(String::alloc(runtime.heap, info->version));
}

static PRIMTYPE(type_level) {
  return args.size() == 0 &&
    out->unify(Integer::typeVar);
}

static PRIMFN(prim_level) {
  EXPECT(0);
  StringInfo *info = static_cast<StringInfo*>(data);

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
      in->size(),
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
  if (ok) {
    RETURN(String::alloc(runtime.heap, c, 1));
  } else {
    RETURN(String::alloc(runtime.heap, ""));
  }
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

static PRIMTYPE(type_cwd) {
  return args.size() == 0 &&
    out->unify(String::typeVar);
}

static PRIMFN(prim_cwd) {
  EXPECT(0);
  StringInfo *info = static_cast<StringInfo*>(data);
  RETURN(String::alloc(runtime.heap, info->wake_cwd));
}

static PRIMTYPE(type_cmdline) {
  TypeVar list;
  Data::typeList.clone(list);
  list[0].unify(String::typeVar);
  return args.size() == 0 &&
    out->unify(list);
}

static PRIMFN(prim_cmdline) {
  EXPECT(0);
  StringInfo *info = static_cast<StringInfo*>(data);

  size_t need = 0, len = 0;
  for (char **arg = info->cmdline; *arg; ++arg) {
    need += String::reserve(strlen(*arg));
    ++len;
  }
  need += reserve_list(len);
  runtime.heap.reserve(need);

  std::vector<Value*> vals;
  for (char **arg = info->cmdline; *arg; ++arg)
    vals.push_back(String::claim(runtime.heap, *arg));
  RETURN(claim_list(runtime.heap, vals.size(), vals.data()));
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

static PRIMTYPE(type_shell_str) {
  return args.size() == 1 &&
    args[0]->unify(String::typeVar) &&
    out->unify(String::typeVar);
}

static PRIMFN(prim_shell_str) {
  EXPECT(1);
  STRING(str, 0);
  RETURN(String::alloc(runtime.heap, shell_escape(str->c_str())));
}

void prim_register_string(PrimMap &pmap, StringInfo *info) {
  prim_register(pmap, "strlen",   prim_strlen,   type_strlen,    PRIM_PURE);
  prim_register(pmap, "vcat",     prim_vcat,     type_vcat,      PRIM_PURE);
  prim_register(pmap, "lcat",     prim_lcat,     type_lcat,      PRIM_PURE);
  prim_register(pmap, "explode",  prim_explode,  type_explode,   PRIM_PURE);
  prim_register(pmap, "getenv",   prim_getenv,   type_getenv,    PRIM_PURE);
  prim_register(pmap, "format",   prim_format,   type_format,    PRIM_PURE);
  prim_register(pmap, "version",  prim_version,  type_version,   PRIM_PURE, (void*)info);
  prim_register(pmap, "level",    prim_level,    type_level,     PRIM_PURE, (void*)info);
  prim_register(pmap, "cwd",      prim_cwd,      type_cwd,       PRIM_PURE, (void*)info);
  prim_register(pmap, "cmdline",  prim_cmdline,  type_cmdline,   PRIM_PURE, (void*)info);
  prim_register(pmap, "scmp",     prim_scmp,     type_scmp,      PRIM_PURE);
  prim_register(pmap, "sNFC",     prim_sNFC,     type_normalize, PRIM_PURE);
  prim_register(pmap, "sNFKC",    prim_sNFKC,    type_normalize, PRIM_PURE);
  prim_register(pmap, "scaseNFKC",prim_scaseNFKC,type_normalize, PRIM_PURE);
  prim_register(pmap, "code2str", prim_code2str, type_code2str,  PRIM_PURE);
  prim_register(pmap, "bin2str",  prim_bin2str,  type_code2str,  PRIM_PURE);
  prim_register(pmap, "str2code", prim_str2code, type_str2code,  PRIM_PURE);
  prim_register(pmap, "str2bin",  prim_str2bin,  type_str2code,  PRIM_PURE);
  prim_register(pmap, "uname",    prim_uname,    type_uname,     PRIM_PURE);
  prim_register(pmap, "shell_str",prim_shell_str,type_shell_str, PRIM_PURE);
  prim_register(pmap, "colour",   prim_colour,   type_colour,    PRIM_IMPURE);
  prim_register(pmap, "print",    prim_print,    type_print,     PRIM_IMPURE);
  prim_register(pmap, "mkdir",    prim_mkdir,    type_mkdir,     PRIM_IMPURE);
  prim_register(pmap, "unlink",   prim_unlink,   type_unlink,    PRIM_IMPURE);
  prim_register(pmap, "write",    prim_write,    type_write,     PRIM_IMPURE);
  prim_register(pmap, "read",     prim_read,     type_read,      PRIM_ORDERED);
}
