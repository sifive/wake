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

#include "lexint.h"

/*!re2c
  re2c:define:YYCTYPE = "unsigned char";
  re2c:flags:8 = 1;
*/

uint32_t lex_oct(const unsigned char *s, const unsigned char *e)
{
  uint32_t u = 0;
  for (++s; s < e; ++s) u = u*8 + *s - '0';
  return u;
}

uint32_t lex_hex(const unsigned char *s, const unsigned char *e)
{
  uint32_t u = 0;
  for (s += 2; s < e;) {
  /*!re2c
      re2c:yyfill:enable = 0;
      re2c:define:YYCURSOR = s;
      *     { u = u*16 + s[-1] - '0' +  0; continue; }
      [a-f] { u = u*16 + s[-1] - 'a' + 10; continue; }
      [A-F] { u = u*16 + s[-1] - 'A' + 10; continue; }
  */
  }
  return u;
}
