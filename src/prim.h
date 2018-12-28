#ifndef PRIM_H
#define PRIM_H

#include "primfn.h"
#include <map>
#include <string>
#include <vector>
#include <memory>

struct Receiver;
struct Value;
struct String;
struct Integer;

/* Macros for handling inputs from wake */
#define RETURN(val) do {						\
  Receiver::receive(queue, std::move(completion), std::move(val));	\
  return;								\
} while (0)

std::unique_ptr<Receiver> require(const char *fn, WorkQueue &queue, std::unique_ptr<Receiver> completion, const std::shared_ptr<Binding> &binding, bool ok, const std::string &str);
#define REQUIRE(b, str) do {								\
  completion = require(__FUNCTION__, queue, std::move(completion), binding, b, str);	\
  if (!completion) return;								\
} while (0)

#define RAISE(str) do {									\
  require(__FUNCTION__, queue, std::move(completion), binding, false, str);		\
  return;										\
} while (0)

std::unique_ptr<Receiver> expect_args(const char *fn, WorkQueue &queue, std::unique_ptr<Receiver> completion, const std::shared_ptr<Binding> &binding, const std::vector<std::shared_ptr<Value> > &args, int expect);
#define EXPECT(num) do { 									\
  (void)data;											\
  completion = expect_args(__FUNCTION__, queue, std::move(completion), binding, args, num);	\
  if (!completion) return;									\
} while (0)

std::unique_ptr<Receiver> cast_string(WorkQueue &queue, std::unique_ptr<Receiver> completion, const std::shared_ptr<Binding> &binding, const std::shared_ptr<Value> &value, String **str);
#define STRING(arg, i) 									\
  String *arg;										\
  do {											\
    completion = cast_string(queue, std::move(completion), binding, args[i], &arg);	\
    if (!completion) return;								\
  } while(0)

std::unique_ptr<Receiver> cast_integer(WorkQueue &queue, std::unique_ptr<Receiver> completion, const std::shared_ptr<Binding> &binding, const std::shared_ptr<Value> &value, Integer **str);
#define INTEGER(arg, i) 								\
  Integer *arg;										\
  do {											\
    completion = cast_integer(queue, std::move(completion), binding, args[i], &arg);	\
    if (!completion) return;								\
  } while(0)

/* Useful expressions for primitives */
std::shared_ptr<Value> make_true();
std::shared_ptr<Value> make_false();
std::shared_ptr<Value> make_tuple(std::shared_ptr<Value> &&first, std::shared_ptr<Value> &&second);
std::shared_ptr<Value> make_list(std::vector<std::shared_ptr<Value> > &&values);

/* Register primitive functions */
typedef std::map<std::string, std::pair<PrimFn, void *> > PrimMap;
struct JobTable;

void prim_register_string(PrimMap &pmap, const char *version);
void prim_register_integer(PrimMap &pmap);
void prim_register_polymorphic(PrimMap &pmap);
void prim_register_regexp(PrimMap &pmap);
void prim_register_job(JobTable *jobtable, PrimMap &pmap);
void prim_register_sources(std::vector<std::shared_ptr<String> > *sources, PrimMap &pmap);

#endif
