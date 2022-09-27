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

#include <wcl/doc.h>

#include <iostream>

#include "common.h"
#include "types.h"

// This does nothing, good for kicking off a chain of catters
struct EpsilonCatter {
  ALWAYS_INLINE void cat(wcl::doc_builder& builder, ctx_t ctx) {}
};

struct SpaceCatter {
  uint8_t count;
  SpaceCatter(uint8_t count) : count(count) {}
  ALWAYS_INLINE void cat(wcl::doc_builder& builder, ctx_t ctx) { space(builder, count); }
};

struct NewlineCatter {
  ALWAYS_INLINE void cat(wcl::doc_builder& builder, ctx_t ctx) { newline(builder, 0); }
};

struct FreshlineCatter {
  ALWAYS_INLINE void cat(wcl::doc_builder& builder, ctx_t ctx) { freshline(builder, ctx); }
};

struct LiteralCatter {
  wcl::doc lit;
  LiteralCatter(wcl::doc lit) : lit(lit) {}

  ALWAYS_INLINE void cat(wcl::doc_builder& builder, ctx_t ctx) { builder.append(std::move(lit)); }
};

template <class Catter1, class Catter2>
struct SeqCatter {
  Catter1 catter1;
  Catter2 catter2;
  SeqCatter(Catter1 c1, Catter2 c2) : catter1(c1), catter2(c2) {}

  ALWAYS_INLINE void cat(wcl::doc_builder& builder, ctx_t ctx) {
    catter1.cat(builder, ctx);
    catter2.cat(builder, ctx);
  }
};

template <class CTR>
struct NestCatter {
  CTR catter;

  NestCatter(CTR catter) : catter(catter) {}

  ALWAYS_INLINE void cat(wcl::doc_builder& builder, ctx_t ctx) {
    builder.append(catter.concat(ctx.nest().sub(builder)));
  }
};

template <class CTR>
struct JoinCatter {
  CTR catter;

  JoinCatter(CTR catter) : catter(catter) {}

  ALWAYS_INLINE void cat(wcl::doc_builder& builder, ctx_t ctx) {
    builder.append(catter.concat(ctx.nest().sub(builder)));
  }
};

template <class FMT>
struct FormatCatter {
  FMT formatter;
  CSTElement node;
  const token_traits_map_t& traits;

  FormatCatter(FMT formatter, CSTElement node, const token_traits_map_t& traits)
      : formatter(formatter), node(node), traits(traits) {}

  ALWAYS_INLINE void cat(wcl::doc_builder& builder, ctx_t ctx) {
    builder.append(formatter.compose(ctx.sub(builder), node, traits));
  }
};
