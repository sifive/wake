#ifndef BIND_H
#define BIND_H

#include <memory>
#include "prim.h"

struct Top;
struct Expr;

// Eliminate DefMap + Top + Subscribe expressions
std::unique_ptr<Expr> bind_refs(std::unique_ptr<Top> top, const PrimMap &pmap);

#endif
