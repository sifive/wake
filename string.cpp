#include "prim.h"
#include "value.h"
#include "heap.h"
#include <sstream>

struct CatStream : public Value {
  std::stringstream str;
  static const char *type;
  CatStream() : Value(type) { }
};
const char *CatStream::type = "CatStream";

static std::unique_ptr<Receiver> cast_catstream(std::unique_ptr<Receiver> completion, const std::shared_ptr<Value> &value, CatStream **cat) {
  if (value->type != CatStream::type) {
    resume(std::move(completion), std::make_shared<Exception>(value->to_str() + " is not a CatStream"));
    return std::unique_ptr<Receiver>();
  } else {
    *cat = reinterpret_cast<CatStream*>(value.get());
    return completion;
  }
}
#define CATSTREAM(arg, i) 							\
  CatStream *arg;								\
  do {										\
    completion = cast_catstream(std::move(completion), args[i], &arg);		\
    if (!completion) return;							\
  } while(0)

static void prim_catopen(void *data, std::vector<std::shared_ptr<Value> > &&args, std::unique_ptr<Receiver> completion) {
  EXPECT(0);
  auto out = std::make_shared<CatStream>();
  RETURN(out);
}

static void prim_catadd(void *data, std::vector<std::shared_ptr<Value> > &&args, std::unique_ptr<Receiver> completion) {
  EXPECT(2);
  CATSTREAM(arg0, 0);
  STRING(arg1, 1);
  arg0->str << arg1->value;
  RETURN(args[0]);
}

static void prim_catclose(void *data, std::vector<std::shared_ptr<Value> > &&args, std::unique_ptr<Receiver> completion) {
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
