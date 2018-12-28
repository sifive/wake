#include "prim.h"
#include "value.h"
#include "heap.h"
#include "type.h"
#include <gmp.h>
#include <sstream>

static PRIMTYPE(type_lt) {
  return args.size() == 2 &&
    args[0]->unify(*args[1]) &&
    out->unify(Data::typeBoolean);
}

static PRIMFN(prim_lt) {
  EXPECT(2);
  int cmp;
  if (args[0]->type == Integer::type) {
    INTEGER(arg0, 0);
    INTEGER(arg1, 1);
    cmp = mpz_cmp(arg0->value, arg1->value);
  } else if (args[0]->type == String::type) {
    STRING(arg0, 0);
    STRING(arg1, 1);
    cmp = arg0->value < arg1->value ? -1 : 0;
  } else {
    std::stringstream str;
    str << args[0] << " and " << args[0] << "can not be compared";
    RAISE(str.str());
  }
  auto out = cmp < 0 ? make_true() : make_false();
  RETURN(out);
}

static PRIMTYPE(type_eq) {
  return args.size() == 2 &&
    args[0]->unify(*args[1]) &&
    out->unify(Data::typeBoolean);
}

static PRIMFN(prim_eq) {
  EXPECT(2);
  bool eq;
  if (args[0]->type == Integer::type) {
    INTEGER(arg0, 0);
    INTEGER(arg1, 1);
    eq = mpz_cmp(arg0->value, arg1->value) == 0;
  } else if (args[0]->type == String::type) {
    STRING(arg0, 0);
    STRING(arg1, 1);
    eq = arg0->value == arg1->value;
  } else {
    eq = false;
  }
  auto out = eq ? make_true() : make_false();
  RETURN(out);
}

static PRIMTYPE(type_cmp) {
  return args.size() == 2 &&
    args[0]->unify(*args[1]) &&
    out->unify(Integer::typeVar);
}

static PRIMFN(prim_cmp) {
  EXPECT(2);
  int cmp;
  if (args[0]->type == Integer::type) {
    INTEGER(arg0, 0);
    INTEGER(arg1, 1);
    cmp = mpz_cmp(arg0->value, arg1->value);
  } else if (args[0]->type == String::type) {
    STRING(arg0, 0);
    STRING(arg1, 1);
    cmp = arg0->value.compare(arg1->value);
  } else {
    std::stringstream str;
    str << args[0] << " and " << args[0] << "can not be compared";
    RAISE(str.str());
  }
  // Normalize it
  auto out = std::make_shared<Integer>((cmp < 0) ? -1 : (cmp > 0) ? 1 : 0);
  RETURN(out);
}

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
    Receiver::receive(queue, std::move(completion), args[0]->type == Exception::type ? make_true() : make_false());
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

void prim_register_polymorphic(PrimMap &pmap) {
  pmap.emplace("lt",    PrimDesc(prim_lt,    type_lt));
  pmap.emplace("eq",    PrimDesc(prim_eq,    type_eq));
  pmap.emplace("cmp",   PrimDesc(prim_cmp,   type_cmp));
  pmap.emplace("test",  PrimDesc(prim_test,  type_test));
  pmap.emplace("catch", PrimDesc(prim_catch, type_catch));
  pmap.emplace("raise", PrimDesc(prim_raise, type_raise));
}
