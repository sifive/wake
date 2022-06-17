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

#include <cstdint>
#include <tuple>

// TODO: Make the bloom filter bigger
class BloomFilter {
  uint64_t bits = 0;

 public:
  void add_hash(const uint8_t *data, size_t size) {
    // We unsafely ignore size for now and only look at the low order 5-bits
    (void)size;
    bits |= 1 << (data[0] & 0x1F);
  }
  size_t size() const { return sizeof(bits); }
  const uint8_t *data() const { return reinterpret_cast<const uint8_t *>(&bits); }
};
