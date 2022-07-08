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

#include <fcntl.h>
#include <unistd.h>

#include <cstdint>
#include <tuple>

namespace wcl {

template <class T>
static std::string to_hex(const T *value) {
  const uint8_t *data = reinterpret_cast<const uint8_t *>(value);
  static const char *hex = "0123456789abcdef";
  char name[2 * sizeof(T) + 1];
  for (size_t i = 0; i < sizeof(T); ++i) {
    name[2 * i + 1] = hex[data[i] & 0xF];
    name[2 * i] = hex[(data[i] >> 4) & 0xF];
  }
  name[2 * sizeof(T)] = '\0';
  return name;
}

// Use /dev/urandom to get a good seed
std::tuple<uint64_t, uint64_t, uint64_t, uint64_t> get_rng_seed();

// Adapted from wikipedia's code, which was adapted from
// the code included on Sebastiano Vigna's website for
// Xoshiro256**. Xoshiro256** is a modern, efficent, and
// highly robust psuedo random number generator. It uses
// a small amount of state for its period and passes the
// CRUSH suite of statistical tests.
class xoshiro_256 {
  uint64_t state[4];

  static uint64_t rol64(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }

 public:
  using result_type = uint64_t;
  static constexpr result_type min() { return 0; }
  static constexpr result_type max() { return ~min(); }

  xoshiro_256() = delete;

  xoshiro_256(std::tuple<uint64_t, uint64_t, uint64_t, uint64_t> seed) {
    state[0] = std::get<0>(seed);
    state[1] = std::get<1>(seed);
    state[2] = std::get<2>(seed);
    state[3] = std::get<3>(seed);
  }

  // Generates a psuedo random number, uniformly distributed
  // over the 64-bit unsigned integers.
  result_type operator()() {
    uint64_t *s = state;
    uint64_t const result = rol64(state[1] * 5, 7) * 9;
    uint64_t const t = s[1] << 17;

    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];

    s[2] ^= t;
    s[3] = rol64(s[3], 45);

    return result;
  }

  // Generates a 16-byte unique name as a 32-character hex string.
  // This can be assumed to be unique assuming this rng was seeded
  // with a high quality source of randomness like /dev/urandom but
  // it should not be assumed to be *secure*, just unique assuming
  // no malicious intent.
  std::string unique_name() {
    uint8_t data[16];
    reinterpret_cast<uint64_t *>(data)[0] = (*this)();
    reinterpret_cast<uint64_t *>(data)[1] = (*this)();
    return to_hex<uint8_t[16]>(&data);
  }
};

}  // namespace wcl
