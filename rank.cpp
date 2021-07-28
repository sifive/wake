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

#include "rank.h"

void RankMap::set(uint32_t x) {
    size_t off = x/64;
    size_t bit = x%64;

    if (off >= bitmap.size()) {
        bitmap.resize(off+1, 0);
        sums.resize(off+1, sums.empty()?0:sums.back());
    }

    bitmap[off] |= UINT64_C(1) << bit;
    ++sums[off];
}

bool RankMap::get(uint32_t x) const {
    size_t off = x/64;
    size_t bit = x%64;
    if (off >= bitmap.size()) return false;
    return (bitmap[off] >> bit) & 1;
}

uint32_t RankMap::rank(uint32_t offset) const {
    size_t off = offset/64;
    size_t bit = offset%64;
    uint64_t mask = UINT64_C(0xffffffffffffffff) << bit;
    return (off?sums[off-1]:0) + __builtin_popcountll(~mask & bitmap[off]);
}

uint32_t RankMap::next(uint32_t offset) const {
    ++offset;
    size_t off = offset/64;
    size_t bit = offset%64;
    uint64_t mask = UINT64_C(0xffffffffffffffff) << bit;
    uint64_t reg = bitmap[off] & mask;
    if (reg) {
        return off*64 + __builtin_ctzll(reg);
    } else {
        while (0 == (reg = bitmap[++off])) { }
        return off*64 + __builtin_ctzll(reg);
    }
}
