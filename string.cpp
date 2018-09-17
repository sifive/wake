#include "prim.h"
#include "value.h"
#include "heap.h"
#include "MurmurHash3.h"
#include <sstream>

struct CatStream : public Value {
  std::stringstream str;
  static const char *type;
  CatStream() : Value(type) { }

  void stream(std::ostream &os) const;
  void hash(std::unique_ptr<Hasher> hasher);
};
const char *CatStream::type = "CatStream";

void CatStream::stream(std::ostream &os) const { os << "CatStream(" << str.str() << ")"; }
void CatStream::hash(std::unique_ptr<Hasher> hasher) {
  uint64_t payload[2];
  std::string data = str.str();
  MurmurHash3_x64_128(data.data(), data.size(), (long)type, payload);
  hasher->receive(payload);
}

static std::unique_ptr<Receiver> cast_catstream(std::unique_ptr<Receiver> completion, const std::shared_ptr<Binding> &binding, const std::shared_ptr<Value> &value, CatStream **cat) {
  if (value->type != CatStream::type) {
    resume(std::move(completion), std::make_shared<Exception>(value->to_str() + " is not a CatStream", binding));
    return std::unique_ptr<Receiver>();
  } else {
    *cat = reinterpret_cast<CatStream*>(value.get());
    return completion;
  }
}
#define CATSTREAM(arg, i) 								\
  CatStream *arg;									\
  do {											\
    completion = cast_catstream(std::move(completion), binding, args[i], &arg);		\
    if (!completion) return;								\
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

void prim_register_string(PrimMap &pmap) {
  pmap["catopen" ].first = prim_catopen;
  pmap["catadd"  ].first = prim_catadd;
  pmap["catclose"].first = prim_catclose;
}
