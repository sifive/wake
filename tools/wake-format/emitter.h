/*
 * Copyright 2022 SiFive, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You should have received a copy of LICENSE.Apache2 along with
 * this software. If not, you may obtain a copy at
 *
 *    https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <wcl/optional.h>
#include <wcl/rope.h>

#include <cassert>
#include <iostream>
#include <vector>

#include "parser/cst.h"

class Emitter {
 public:
  struct ctx_t {
    int nest_level = 0;
    bool is_flat = false;

    ctx_t flat() {
      ctx_t copy = *this;
      copy.is_flat = true;
      return copy;
    }
  };

  struct nest_t {
    ctx_t* ctx;

    nest_t(ctx_t* ctx) : ctx(ctx) { ctx->nest_level++; }

    ~nest_t() { ctx->nest_level--; }
  };

  Emitter(std::ostream* ostream) : ostream(ostream) { assert(ostream != nullptr); }

  nest_t nest(ctx_t ctx);
  wcl::rope newline(ctx_t ctx);
  wcl::rope space(ctx_t ctx, uint8_t count = 1);

  // takes a CST and flattens it, should be memoized
  wcl::optional<wcl::rope> flat(ctx_t ctx, CSTElement node);
  void layout(CST cst);

 private:
  wcl::optional<wcl::rope> walk_top(ctx_t ctx, CSTElement node);

  std::ostream* ostream;
  const uint8_t space_per_indent = 4;

  friend nest_t;
};
