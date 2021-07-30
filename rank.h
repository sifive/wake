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

#ifndef RANK_H
#define RANK_H

#include <stdint.h>
#include <vector>

typedef long long rs_u64x8  __attribute__ ((vector_size (64)));
typedef long long rs_u64x4  __attribute__ ((vector_size (32)));
typedef short     rs_u16x16 __attribute__ ((vector_size (32)));
typedef int       rs_u32x8  __attribute__ ((vector_size (32)));

struct RankLevel0 {
    union {
        rs_u64x8 a;
        uint64_t v[8];
    };
} __attribute__ ((aligned (64)));

struct RankLevel1 {
    union {
        rs_u16x16 a[2];
        uint16_t  v[32];
    };
} __attribute__ ((aligned (64)));

struct RankLevel2 {
    union {
        rs_u32x8 a[2];
        uint32_t v[16];
    };
} __attribute__ ((aligned (64)));

class RankBuilder {
public:
    void set(uint32_t x);
    bool get(uint32_t x) const;

protected:
    mutable std::vector<uint64_t> bitmap;

friend class RankMap;
friend class RankSelect1Map;
};

class RankMap {
public:
    RankMap(const RankBuilder &builder);

    // Is the bit at 'offset' a 1?
    bool get(uint32_t offset) const;

    // Number of 1-bits in the range [0, offset)
    uint32_t rank1(uint32_t offset) const;

    // Number of 0-bits in the range [0, offset)
    uint32_t rank0(uint32_t offset) const { return offset - rank1(offset); }

protected:
    std::vector<RankLevel0> level0; // raw bit vector
    std::vector<RankLevel1> level1; // level1[x].v[i] = popcount 512*[32*x, 32*x+i)
    std::vector<RankLevel2> level2; // level2[x].v[i] = popcount [0, 512*32*(16*x+i))
};

class RankSelect1Map : public RankMap {
public:
    RankSelect1Map(const RankBuilder &builder);

    // Return the offset of the x-th 1-bit, counting from x=0.
    //   get(select1(x)) = true
    //   rank1(select1(x)) = x
    //   select1(rank1(x)) >= x; equal if-and-only-if get(x)=true
    uint32_t select1(uint32_t x) const;

    // The number of 1s in the bitvector
    uint32_t ones() const { return num1s; }

    // Return the first index >= offset with a bit=1
    //   next1(offset) = select1(rank1(offset))
    uint32_t next1(uint32_t offset) const { return select1(rank1(offset)); }

protected:
    uint32_t num1s;
    std::vector<uint16_t> sample1; // Every 1024-st 0 bit
};

class RankSelect01Map : public RankSelect1Map {
public:
    RankSelect01Map(const RankBuilder &builder);

    uint32_t select0(uint32_t rank0) const;

protected:
    std::vector<uint16_t> sample0;
};

#endif
