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

#include "utf8.h"

#include <string>

enum {
  Bit1 = 7,
  Bitx = 6,
  Bit2 = 5,
  Bit3 = 4,
  Bit4 = 3,
  Bit5 = 2,

  T1 = ((1 << (Bit1 + 1)) - 1) ^ 0xFF, /* 0000 0000 */
  Tx = ((1 << (Bitx + 1)) - 1) ^ 0xFF, /* 1000 0000 */
  T2 = ((1 << (Bit2 + 1)) - 1) ^ 0xFF, /* 1100 0000 */
  T3 = ((1 << (Bit3 + 1)) - 1) ^ 0xFF, /* 1110 0000 */
  T4 = ((1 << (Bit4 + 1)) - 1) ^ 0xFF, /* 1111 0000 */
  T5 = ((1 << (Bit5 + 1)) - 1) ^ 0xFF, /* 1111 1000 */

  Rune1 = (1 << (Bit1 + 0 * Bitx)) - 1, /*                     0111 1111 */
  Rune2 = (1 << (Bit2 + 1 * Bitx)) - 1, /*                0111 1111 1111 */
  Rune3 = (1 << (Bit3 + 2 * Bitx)) - 1, /*           1111 1111 1111 1111 */
  Rune4 = (1 << (Bit4 + 3 * Bitx)) - 1, /* 0001 1111 1111 1111 1111 1111 */

  Maskx = (1 << Bitx) - 1, /* 0011 1111 */
  Testx = Maskx ^ 0xFF     /* 1100 0000 */
};

#define LOW_SURROGATE 0xD800
#define HIGH_SURROGATE 0xDC00
#define END_SURROGATE 0xE000

bool push_utf8(std::string &result, uint32_t c) {
  if (c <= Rune1) {
    result.push_back(static_cast<unsigned char>(c));
  } else if (c <= Rune2) {
    result.push_back(T2 | static_cast<unsigned char>(c >> 1 * Bitx));
    result.push_back(Tx | (c & Maskx));
  } else if (c <= Rune3) {
    if (LOW_SURROGATE <= c && c < END_SURROGATE) return false;
    result.push_back(T3 | static_cast<unsigned char>(c >> 2 * Bitx));
    result.push_back(Tx | ((c >> 1 * Bitx) & Maskx));
    result.push_back(Tx | (c & Maskx));
  } else if (c <= Rune4) {
    result.push_back(T4 | static_cast<unsigned char>(c >> 3 * Bitx));
    result.push_back(Tx | ((c >> 2 * Bitx) & Maskx));
    result.push_back(Tx | ((c >> 1 * Bitx) & Maskx));
    result.push_back(Tx | (c & Maskx));
  } else {
    return false;
  }
  return true;
}

int pop_utf8(uint32_t *rune, const char *str) {
  const unsigned char *s = reinterpret_cast<const unsigned char *>(str);
  int c, c1, c2, c3;
  long l;

  /*
   * one character sequence
   *  00000-0007F => T1
   */
  c = s[0];
  if (c < Tx) {
    *rune = c;
    return 1;
  }

  /*
   * two character sequence
   *  0080-07FF => T2 Tx
   */
  c1 = s[1] ^ Tx;
  if (c1 & Testx) return -1;
  if (c < T3) {
    if (c < T2) return -1;
    l = ((c << Bitx) | c1) & Rune2;
    if (l <= Rune1) return -1;
    *rune = l;
    return 2;
  }

  /*
   * three character sequence
   *  0800-FFFF => T3 Tx Tx
   */
  c2 = s[2] ^ Tx;
  if (c2 & Testx) return -1;
  if (c < T4) {
    l = ((((c << Bitx) | c1) << Bitx) | c2) & Rune3;
    if (l <= Rune2) return -1;
    *rune = l;
    return 3;
  }

  /*
   * four character sequence (21-bit value)
   *  10000-1FFFFF => T4 Tx Tx Tx
   */
  c3 = s[3] ^ Tx;
  if (c3 & Testx) return -1;
  if (c < T5) {
    l = ((((((c << Bitx) | c1) << Bitx) | c2) << Bitx) | c3) & Rune4;
    if (l <= Rune3) return -1;
    *rune = l;
    return 4;
  }

  return -1;
}
