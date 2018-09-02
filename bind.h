#ifndef BIND_H
#define BIND_H

#include <map>
#include <string>
#include <vector>

struct Expr;
struct Action;
struct Value;

/* Primitive functions must call resume once they are done their work */
void resume(Action *completion, Value *return_value);
void stack_trace(Action *completion);
typedef void (*PrimFn)(void *data, const std::vector<Value*> &args, Action *completion);

typedef std::map<std::string, std::pair<PrimFn, void* > > PrimMap;
bool bind_refs(Expr *expr, const PrimMap& pmap);

#endif
