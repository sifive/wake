#include "prim.h"
#include "value.h"

static void prim_strcat(void *data, const std::vector<Value*> &args, Action *completion) {
  EXPECT_ARGS(2);
  String *arg0 = GET_STRING(0);
  String *arg1 = GET_STRING(1);
  resume(completion, new String(arg0->value + arg1->value));
}

void prim_register_string(PrimMap& pmap) {
  pmap["strcat"].first = prim_strcat;
}
