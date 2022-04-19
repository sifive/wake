/*
 * Copyright 2019 SiFive, Inc.
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

#ifndef UTF8_H
#define UTF8_H

#include <string>
#include <stdint.h>

bool push_utf8(std::string &result, uint32_t c);
int pop_utf8(uint32_t *rune, const char *str);

inline int is_utf8_start(uint8_t byte) {
  return (byte >> 6) != 2;
}

inline unsigned num_utf8_starts(uint64_t bytes) {
  uint64_t magic = UINT64_C(0x0101010101010101);
  return static_cast<uint64_t>(((~(bytes >> 7) | (bytes >> 6)) & magic) * magic) >> 56;
}

#endif
