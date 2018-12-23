#ifndef PRIMFN_H
#define PRIMFN_H

#include <memory>
#include <vector>

struct Binding;
struct Receiver;
struct Value;
struct WorkQueue;
struct TypeVar;

typedef bool (*PrimType)(void **data, const std::vector<TypeVar*> &args, TypeVar *out);
typedef void (*PrimFn)(void *data, WorkQueue &queue, std::unique_ptr<Receiver> completion, std::shared_ptr<Binding> &&binding, std::vector<std::shared_ptr<Value> > &&args);
#define PRIMTYPE(name) bool name(void **data, const std::vector<TypeVar*> &args, TypeVar *out)
#define PRIMFN(name) void name(void *data, WorkQueue &queue, std::unique_ptr<Receiver> completion, std::shared_ptr<Binding> &&binding, std::vector<std::shared_ptr<Value> > &&args)

#endif
