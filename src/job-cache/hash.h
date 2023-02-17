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

#include <cassert>
#include <cstdint>
#include <tuple>

#include "blake2/blake2.h"

// TODO: This is kind of a hack, we probably want these utility functions
// in wcl in like a "hex" library or something.
#include <wcl/xoshiro_256.h>

static inline uint8_t hex_to_nibble(char hex) {
  if (hex >= '0' && hex <= '9') return hex - '0';
  if (hex >= 'a' && hex <= 'f') return hex - 'a' + 10;
  if (hex >= 'A' && hex <= 'F') return hex - 'A' + 10;
  // This case is invalid but I suspect it will blow
  // up the quickest if violated.
  return 0xFF;
}

template <size_t size>
static inline void get_hex_data(const std::string& s, uint8_t (*data)[size]) {
  uint8_t* start = *data;
  const uint8_t* end = start + size;
  for (size_t i = 0; start < end && i < s.size(); i += 2) {
    // Bytes are in little endian but nibbles are in big endian...
    // I could put the entire number in big endian but that's extremely
    // frustrating to work with...but also if the nibbles aren't in big
    // endian it gets really confusing to look at certain things when you
    // use the pure-little endian to_hex. So to make to_hex and get_hex_data
    // match and to make to_hex results easy to read, we do this.
    start[0] = (hex_to_nibble(s[i]) << 4) & 0xF0;
    if (i + 1 < s.size()) start[0] |= hex_to_nibble(s[i + 1]) & 0xF;
    ++start;
  }
}

struct Hash256 {
  uint64_t data[4] = {0};

  Hash256() {}
  Hash256(const Hash256& other) {
    data[0] = other.data[0];
    data[1] = other.data[1];
    data[2] = other.data[2];
    data[3] = other.data[3];
  }

  static Hash256 blake2b(const std::string& str) {
    Hash256 out;
    blake2b_state S;
    blake2b_init(&S, sizeof(out));
    blake2b_update(&S, reinterpret_cast<const uint8_t*>(str.data()), str.size());
    blake2b_final(&S, reinterpret_cast<uint8_t*>(out.data), sizeof(out));
    return out;
  }

  static Hash256 from_hex(const std::string& hash) {
    assert(hash.size() == 64);
    Hash256 out;
    get_hex_data(hash, reinterpret_cast<uint8_t(*)[32]>(&out.data));
    return out;
  }

  static Hash256 from_hash(uint8_t (*data)[32]) {
    Hash256 out;
    uint64_t(&data64)[4] = *reinterpret_cast<uint64_t(*)[4]>(data);
    out.data[0] = data64[0];
    out.data[1] = data64[1];
    out.data[2] = data64[2];
    out.data[3] = data64[3];
    return out;
  }

  std::string to_hex() const { return wcl::to_hex(&data); }

  bool operator==(Hash256 other) {
    return data[0] == other.data[0] && data[1] == other.data[1] && data[2] == other.data[2] &&
           data[3] == other.data[3];
  }

  bool operator!=(Hash256 other) { return !(*this == other); }
};
