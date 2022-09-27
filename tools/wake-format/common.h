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

#include "types.h"

inline void space(wcl::doc_builder& builder, uint8_t count) {
  for (uint8_t i = 0; i < count; i++) {
    builder.append(SPACE_STR);
  }
}

inline void newline(wcl::doc_builder& builder, uint8_t space_count) {
  builder.append(NL_STR);
  space(builder, space_count);
}

inline void freshline(wcl::doc_builder& builder, ctx_t ctx) {
  auto goal_width = SPACE_PER_INDENT * ctx.nest_level;
  auto merged = ctx.sub(builder);

  // there are non-ws characters on the line, thus a nl is required
  if (merged->last_width() > merged->last_ws_count()) {
    newline(builder, goal_width);
    return;
  }

  // This is a fresh line, but without the right amount of spaces
  if (merged->last_width() < goal_width) {
    space(builder, goal_width - merged->last_width());
    return;
  }

  // If there are too many spaces, then a freshline() was used instead
  // of newline(). Assert to ensure it is fixed.
  if (merged->last_width() > goal_width) {
    assert(false);
  }
}
