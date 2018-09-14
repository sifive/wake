#ifndef PRIMFN_H
#define PRIMFN_H

#include <memory>
#include <vector>

struct Binding;
struct Receiver;
struct Value;

typedef void (*PrimFn)(void *data, std::unique_ptr<Receiver> completion, std::shared_ptr<Binding> &&binding, std::vector<std::shared_ptr<Value> > &&args);
#define PRIMFN(name) void name(void *data, std::unique_ptr<Receiver> completion, std::shared_ptr<Binding> &&binding, std::vector<std::shared_ptr<Value> > &&args)

#endif
