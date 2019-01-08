#include "prim.h"
#include "value.h"
#include "heap.h"
#include "type.h"
#include <sstream>

static PRIMTYPE(type_test) {
  return args.size() == 1 &&
    // leave arg0 free
    out->unify(Data::typeBoolean);
}

static PRIMFN(prim_test) {
  (void)data; // silence unused variable warning (EXPECT not called)
  if (args.size() != 1) {
    Receiver::receive(queue, std::move(completion),
      std::make_shared<Exception>("prim_test called on " + std::to_string(args.size()) + "; was exepecting 1", binding));
  } else {
    Receiver::receive(queue, std::move(completion), make_bool(args[0]->type == Exception::type));
  }
}

static PRIMTYPE(type_catch) {
  TypeVar list;
  Data::typeList.clone(list);
  list[0].unify(String::typeVar);
  return args.size() == 1 &&
    // leave arg0 free
    out->unify(list);
}

static PRIMFN(prim_catch) {
  (void)data; // silence unused variable warning (EXPECT not called)
  if (args.size() != 1 || args[0]->type != Exception::type) {
    Receiver::receive(queue, std::move(completion),
      std::make_shared<Exception>("prim_catch not called on an exception", binding));
  } else {
    Exception *exception = reinterpret_cast<Exception*>(args[0].get());
    std::vector<std::shared_ptr<Value> > out;
    for (auto &i : exception->causes)
      out.emplace_back(std::make_shared<String>(i->reason));
    Receiver::receive(queue, std::move(completion), make_list(std::move(out)));
  }
}

static PRIMTYPE(type_raise) {
  return args.size() == 1 &&
    args[0]->unify(String::typeVar);
    // leave prim free
}

static PRIMFN(prim_raise) {
  EXPECT(1);
  STRING(arg0, 0);
  Receiver::receive(queue, std::move(completion), std::make_shared<Exception>(arg0->value, binding));
}

static PRIMTYPE(type_id) {
  return args.size() == 1 &&
    out->unify(*args[0]);
}

static PRIMFN(prim_id) {
  EXPECT(1);
  RETURN(args[0]);
}

void prim_register_exception(PrimMap &pmap) {
  pmap.emplace("test",  PrimDesc(prim_test,  type_test,  0, PRIM_PURE));
  pmap.emplace("catch", PrimDesc(prim_catch, type_catch, 0, PRIM_PURE));
  pmap.emplace("raise", PrimDesc(prim_raise, type_raise, 0, PRIM_PURE));

  pmap.emplace("wait_one", PrimDesc(prim_id, type_id, 0, PRIM_PURE|PRIM_SHALLOW));
  pmap.emplace("wait_all", PrimDesc(prim_id, type_id, 0, PRIM_PURE));
}
