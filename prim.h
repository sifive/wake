#ifndef PRIM_H
#define PRIM_H

#include <map>
#include <string>
#include <vector>

struct Expr;
struct Action;
struct Value;
struct String;
struct Integer;

/* Primitive functions must call resume once they are done their work */
void resume(Action *completion, Value *return_value);
void stack_trace(Action *completion);

typedef void (*PrimFn)(void *data, const std::vector<Value*> &args, Action *completion);
typedef std::map<std::string, std::pair<PrimFn, void *> > PrimMap;

void expect_args(const char *fn, Action *completion, const std::vector<Value*> &args, int expect);
String *expect_string(const char *fn, Action *completion, Value *value, int index);
Integer *expect_integer(const char *fn, Action *completion, Value *value, int index);

#define EXPECT_ARGS(num) expect_args(__FUNCTION__, completion, args, num)
#define GET_STRING(index) expect_string(__FUNCTION__, completion, args[index], index+1)
#define GET_INTEGER(index) expect_integer(__FUNCTION__, completion, args[index], index+1)

Value *make_true();
Value *make_false();
Value *make_list(const std::vector<Value*>& values);

struct JobTable;

void prim_register_string(PrimMap &pmap);
void prim_register_integer(PrimMap &pmap);
void prim_register_polymorphic(PrimMap &pmap);
void prim_register_job(JobTable *jobtable, PrimMap &pmap);

#endif
