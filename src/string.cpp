#include "prim.h"
#include "value.h"
#include "heap.h"
#include "hash.h"
#include "expr.h"
#include <sstream>
#include <fstream>
#include <iostream>
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>
#include <cstring>

struct CatStream : public Value {
  std::stringstream str;
  static const char *type;
  static TypeVar typeVar;
  CatStream() : Value(type) { }

  void format(std::ostream &os, int depth) const;
  TypeVar &getType();
  Hash hash() const;
};
const char *CatStream::type = "CatStream";

void CatStream::format(std::ostream &os, int p) const {
  if (APP_PRECEDENCE < p) os << "(";
  os << "CatStream ";
  String(str.str()).format(os, p);
  if (APP_PRECEDENCE < p) os << ")";
}

TypeVar CatStream::typeVar("CatStream", 0);
TypeVar &CatStream::getType() {
  return typeVar;
}

Hash CatStream::hash() const {
  Hash payload;
  std::string data = str.str();
  HASH(data.data(), data.size(), (long)type, payload);
  return payload;
}

static std::unique_ptr<Receiver> cast_catstream(WorkQueue &queue, std::unique_ptr<Receiver> completion, const std::shared_ptr<Binding> &binding, const std::shared_ptr<Value> &value, CatStream **cat) {
  if (value->type != CatStream::type) {
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
  auto out = std::make_shared<String>(buffer.str());
  RETURN(out);
}

static PRIMTYPE(type_write) {
  return args.size() == 3 &&
    args[0]->unify(String::typeVar) &&
    args[1]->unify(String::typeVar) &&
    args[2]->unify(Integer::typeVar) &&
    out->unify(String::typeVar);
}

static PRIMFN(prim_write) {
  EXPECT(3);
  STRING(arg0, 0);
  STRING(arg1, 1);
  INTEGER(mode, 2);

  REQUIRE(mpz_cmp_si(mode->value, 0) >= 0, "mode must be >= 0");
  REQUIRE(mpz_cmp_si(mode->value, 0xffff) <= 0, "mode must be <= 0xffff");
  long mask = mpz_get_si(mode->value);

  std::ofstream t(arg0->value, std::ios_base::trunc);
  REQUIRE(!t.fail(), "Could not write " + arg0->value);
  t << arg1->value;
  chmod(arg0->value.c_str(), mask);
  RETURN(args[0]);
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
    args[0]->unify(String::typeVar) &&
    args[1]->unify(Integer::typeVar) &&
    out->unify(Data::typeBool);
}

static PRIMFN(prim_mkdir) {
  EXPECT(2);
  STRING(path, 0);
  INTEGER(mode, 1);

  REQUIRE(mpz_cmp_si(mode->value, 0) >= 0, "mode must be >= 0");
  REQUIRE(mpz_cmp_si(mode->value, 0xffff) <= 0, "mode must be <= 0xffff");
  long mask = mpz_get_si(mode->value);

  std::vector<char> scan(path->value.begin(), path->value.end());
  scan.push_back('/');

  for (size_t i = 1; i < scan.size(); ++i) {
    if (scan[i] == '/') {
      scan[i] = 0;
      if (mkdir(scan.data(), mask) != 0 && errno != EEXIST) {
        std::stringstream str;
        str << scan.data() << ": " << strerror(errno);
        RAISE(str.str());
      }
      scan[i] = '/';
    }
  }

  auto out = make_true();
  RETURN(out);
}

static PRIMTYPE(type_format) {
  return args.size() == 1 &&
    // don't unify args[0] => allow any
    out->unify(String::typeVar);
}

static PRIMFN(prim_format) {
  REQUIRE(args.size() == 1, "prim_format expects 1 argument");
  std::stringstream buffer;
  args[0]->format(buffer, 0);
  auto out = std::make_shared<String>(buffer.str());
  RETURN(out);
}

static PRIMTYPE(type_print) {
  return args.size() == 1 &&
    args[0]->unify(String::typeVar) &&
    out->unify(Data::typeBool);
}

static PRIMFN(prim_print) {
  EXPECT(1);
  STRING(arg0, 0);
  std::cerr << arg0->value;
  auto out = make_true();
  RETURN(out);
}

void prim_register_string(PrimMap &pmap) {
  pmap.emplace("catopen", PrimDesc(prim_catopen, type_catopen));
  pmap.emplace("catadd",  PrimDesc(prim_catadd,  type_catadd));
  pmap.emplace("catclose",PrimDesc(prim_catclose,type_catclose));
  pmap.emplace("write",   PrimDesc(prim_write,   type_write));
  pmap.emplace("read",    PrimDesc(prim_read,    type_read));
  pmap.emplace("getenv",  PrimDesc(prim_getenv,  type_getenv));
  pmap.emplace("mkdir",   PrimDesc(prim_mkdir,   type_mkdir));
  pmap.emplace("format",  PrimDesc(prim_format,  type_format));
  pmap.emplace("print",   PrimDesc(prim_print,   type_print));
}
