#include "prim.h"
#include "value.h"
#include "heap.h"
#include "hash.h"
#include <sstream>
#include <fstream>
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>
#include <cstring>

struct CatStream : public Value {
  std::stringstream str;
  static const char *type;
  CatStream() : Value(type) { }

  void format(std::ostream &os, int depth) const;
  Hash hash() const;
};
const char *CatStream::type = "CatStream";

void CatStream::format(std::ostream &os, int depth) const {
  (void)depth;
  os << "CatStream(" << str.str() << ")" << std::endl;
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

static PRIMFN(prim_catopen) {
  EXPECT(0);
  auto out = std::make_shared<CatStream>();
  RETURN(out);
}

static PRIMFN(prim_catadd) {
  EXPECT(2);
  CATSTREAM(arg0, 0);
  STRING(arg1, 1);
  arg0->str << arg1->value;
  RETURN(args[0]);
}

static PRIMFN(prim_catclose) {
  EXPECT(1);
  CATSTREAM(arg0, 0);
  auto out = std::make_shared<String>(arg0->str.str());
  RETURN(out);
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

static PRIMFN(prim_write) {
  EXPECT(2);
  STRING(arg0, 0);
  STRING(arg1, 1);
  std::ofstream t(arg0->value, std::ios_base::trunc);
  REQUIRE(!t.fail(), "Could not write " + arg0->value);
  t << arg1->value;
  RETURN(args[0]);
}

static PRIMFN(prim_getenv) {
  EXPECT(1);
  STRING(arg0, 0);
  const char *env = getenv(arg0->value.c_str());
  REQUIRE(env, arg0->value + " is unset in the environment");
  auto out = std::make_shared<String>(env);
  RETURN(out);
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

void prim_register_string(PrimMap &pmap) {
  pmap["catopen" ].first = prim_catopen;
  pmap["catadd"  ].first = prim_catadd;
  pmap["catclose"].first = prim_catclose;
  pmap["write"   ].first = prim_write;
  pmap["read"    ].first = prim_read;
  pmap["getenv"  ].first = prim_getenv;
  pmap["mkdir"   ].first = prim_mkdir;
}
