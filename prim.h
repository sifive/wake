#ifndef PRIM_H
#define PRIM_H

#include <map>
#include <string>
#include <vector>
#include <memory>

struct Receiver;
struct Value;
struct String;
struct Integer;

/* Primitive functions must call resume once they are done their work */
void resume(std::unique_ptr<Receiver> completion, std::shared_ptr<Value> &&return_value);

/* Macros for handling inputs from wake */
#define RETURN(val) do {						\
  resume(std::move(completion), std::move(val));			\
  return;								\
} while (0)

#define REQUIRE(b, str) do {						\
  if (!(b)) {								\
    resume(std::move(completion), std::make_shared<Exception>(str));	\
    return;								\
} } while (0)

std::unique_ptr<Receiver> expect_args(const char *fn, std::unique_ptr<Receiver> completion, const std::vector<std::shared_ptr<Value> > &args, int expect);
#define EXPECT(num) do { 							\
  completion = expect_args(__FUNCTION__, std::move(completion), args, num);	\
  if (!completion) return;							\
} while (0)

std::unique_ptr<Receiver> cast_string(std::unique_ptr<Receiver> completion, const std::shared_ptr<Value> &value, String **str);
#define STRING(arg, i) 								\
  String *arg;									\
  do {										\
    completion = cast_string(std::move(completion), args[i], &arg);		\
    if (!completion) return;							\
  } while(0)

std::unique_ptr<Receiver> cast_integer(std::unique_ptr<Receiver> completion, const std::shared_ptr<Value> &value, Integer **str);
#define INTEGER(arg, i) 							\
  Integer *arg;									\
  do {										\
    completion = cast_integer(std::move(completion), args[i], &arg);		\
    if (!completion) return;							\
  } while(0)

/* Useful expressions for primitives */
std::shared_ptr<Value> make_true();
std::shared_ptr<Value> make_false();
std::shared_ptr<Value> make_list(std::vector<std::shared_ptr<Value> > &&values);

/* Register primitive functions */
typedef void (*PrimFn)(void *data, std::vector<std::shared_ptr<Value> > &&args, std::unique_ptr<Receiver> completion);
typedef std::map<std::string, std::pair<PrimFn, void *> > PrimMap;
struct JobTable;

void prim_register_string(PrimMap &pmap);
void prim_register_integer(PrimMap &pmap);
void prim_register_polymorphic(PrimMap &pmap);
void prim_register_regexp(PrimMap &pmap);
void prim_register_job(JobTable *jobtable, PrimMap &pmap);

#endif
