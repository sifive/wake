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

    ctx_t nest() {
      ctx_t copy = *this;
      copy.nest_level++;
      return copy;
    }
  };

  Emitter(std::ostream* ostream) : ostream(ostream) { assert(ostream != nullptr); }

  // Walks the CST, formats it, and writes it to ostream
  void layout(CST cst);

 private:
  // Top level tree walk. Dispatches out the calls for various nodes
  wcl::optional<wcl::rope> walk_node(ctx_t ctx, CSTElement node);
  wcl::rope walk_token(ctx_t ctx, CSTElement node);

  wcl::optional<wcl::rope> walk_block(ctx_t ctx, CSTElement node);
  wcl::rope walk_def(ctx_t ctx, CSTElement node);

  // Emits a newline and any indentation needed
  wcl::rope newline(ctx_t ctx);

  // Emits a space character `count` times
  wcl::rope space(ctx_t ctx, uint8_t count = 1);

  // TODO: memoize this function
  // returns the subtree with all newlines and indentation removed
  // if possible, None otherwise
  wcl::optional<wcl::rope> flat(ctx_t ctx, CSTElement node);

  // Attempts to flatten `node` with flat. On failure returns the full node
  wcl::rope try_flat(ctx_t ctx, CSTElement node);

  std::ostream* ostream;
  const uint8_t space_per_indent = 4;
};
