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

// Open Group Base Specifications Issue 7
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include "lexint.h"

uint32_t lex_oct(const unsigned char *s, const unsigned char *e)
{
  uint32_t u = 0;
  for (++s; s < e; ++s) u = u*8 + *s - '0';
  return u;
}

uint32_t lex_hex(const unsigned char *s, const unsigned char *e)
{
  uint32_t u = 0;
  for (s += 2; s < e; ++s) {
    unsigned char c = *s;
    if      (c < 'A') { u = u*16 + c - '0' +  0; continue; }
    else if (c < 'a') { u = u*16 + c - 'A' + 10; continue; }
    else              { u = u*16 + c - 'a' + 10; continue; }
  }
  return u;
}
