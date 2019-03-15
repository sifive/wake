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
  REQUIRE(args.size() == 1, "prim_test not called on 1 argument");
  auto out = make_bool(args[0]->type == &Exception::type);
  RETURN(out);
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
  REQUIRE(args.size() == 1, "prim_catch not called on 1 argument");
  REQUIRE(args[0]->type == &Exception::type, "prim_catch not called on an Exception");

  Exception *exception = reinterpret_cast<Exception*>(args[0].get());

  std::vector<std::shared_ptr<Value> > v;
  for (auto &i : exception->causes)
    v.emplace_back(std::make_shared<String>(i->reason));
  auto out = make_list(std::move(v));

  RETURN(out);
}

static PRIMTYPE(type_raise) {
  return args.size() == 1 &&
    args[0]->unify(String::typeVar);
  (void)out; // leave prim free
}

static PRIMFN(prim_raise) {
  EXPECT(1);
  STRING(arg0, 0);
  auto out = std::make_shared<Exception>(arg0->value, binding);
  RETURN(out);
}

static PRIMTYPE(type_cast) {
  return args.size() == 1;
  // leave arg0 free
  (void)out; // leave prim free
}

static PRIMFN(prim_cast) {
  EXPECT(1); // re-raise the exception argument
  RAISE("Attempt to cast a non-exception");
}

static PRIMTYPE(type_unit) {
  return args.size() == 1 &&
    out->unify(Data::typeUnit);
}

static PRIMFN(prim_unit) {
  (void)data; // silence unused variable warning (EXPECT not called)
  (void)binding;
  (void)args;
  auto out = make_unit();
  RETURN(out);
}

void prim_register_exception(PrimMap &pmap) {
  pmap.emplace("test",  PrimDesc(prim_test,  type_test,  0, PRIM_PURE));
  pmap.emplace("catch", PrimDesc(prim_catch, type_catch, 0, PRIM_PURE));
  pmap.emplace("raise", PrimDesc(prim_raise, type_raise, 0, PRIM_PURE));
  pmap.emplace("cast",  PrimDesc(prim_cast,  type_cast,  0, PRIM_PURE));

  pmap.emplace("wait_one", PrimDesc(prim_unit, type_unit, 0, PRIM_PURE|PRIM_SHALLOW));
  pmap.emplace("wait_all", PrimDesc(prim_unit, type_unit, 0, PRIM_PURE));
}
