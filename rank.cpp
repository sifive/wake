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

#include <immintrin.h>
#include <assert.h>

#include "rank.h"

#define W_SIZE   64
#define L0_SIZE  512	// number of bits spanned by an L0 block
#define L1_SIZE  16384	// number of bits spanned by an L1 block
#define L0_COUNT 8	// number of entries in an L0 block
#define L1_COUNT 32	// number of entries in an L1 block
#define L2_COUNT 16	// number of entries in an L2 block
#define L12_COUNT (L1_COUNT*L2_COUNT)

#define SAMPLE_RATE 1024 // sampling rate

#define DIV_UP(x, y) (((x)+(y)-1)/(y))
#define MOD_UP(x, y) (((x)+(y)-1)%(y))

void RankBuilder::set(uint32_t x) {
    size_t off = x/64;
    size_t bit = x%64;

    if (off >= bitmap.size())
        bitmap.resize(off+1, 0);

    bitmap[off] |= UINT64_C(1) << bit;
}

bool RankBuilder::get(uint32_t x) const {
    size_t off = x/64;
    size_t bit = x%64;
    if (off >= bitmap.size()) return false;
    return (bitmap[off] >> bit) & 1;
}

RankMap::RankMap(const RankBuilder &builder) {
    // In order to not exceed array bounds while computing rank1, we need the vector to end with a 0
    if ((builder.bitmap.back() >> (W_SIZE-1)) != 0)
        builder.bitmap.push_back(0);

    // Round builder up to L0-block alignment
    size_t len = DIV_UP(builder.bitmap.size(), L0_COUNT);
    builder.bitmap.resize(len*L0_COUNT, 0);

    level0.resize(len);
    level1.resize(DIV_UP(len, L1_COUNT));
    level2.resize(DIV_UP(len, L12_COUNT));

    size_t sum2 = 0, sum1 = 0;
    for (size_t i = 0; i < len; ++i) {
        if (i%L1_COUNT == 0) {
            sum2 += sum1;
            sum1 = 0;
            level2[i/L12_COUNT].v[(i%L12_COUNT)/L1_COUNT] = sum2;
        }
        level1[i/L1_COUNT].v[i%L1_COUNT] = sum1;
        for (size_t j = 0; j < L0_COUNT; ++j) {
            size_t x = builder.bitmap[i*L0_COUNT+j];
            level0[i].v[j] = x;
            sum1 += __builtin_popcountll(x);
        }
    }

    // Fill the tail of the last level-1 block
    if (len%L1_COUNT != 0) {
        for (size_t i = len%L1_COUNT; i < L1_COUNT; ++i)
            level1.back().v[i] = sum1;
    }

    // Fill the tail of the last level-2 block
    sum2 += sum1;
    if (len%L12_COUNT != 0) {
        size_t i = ((len-1)%L12_COUNT)/L1_COUNT;
        for (++i; i < L2_COUNT; ++i)
            level2.back().v[i] = sum2;
    }

    // Add an extra L2 block for strided select1 access
    level2.resize(level2.size()+1);
    for (size_t i = 0; i < L2_COUNT; ++i)
        level2.back().v[i] = sum2;
}

bool RankMap::get(uint32_t x) const {
    if (x/L0_SIZE >= level0.size()) return false;
    return ((&level0[0].v[0])[x/W_SIZE] >> (x%W_SIZE)) & 1;
}

#ifdef __AVX2__
static inline rs_u64x4 vpop(rs_u8x32 x) {
// -mavx512vpopcntdq -mavx512vl
#if defined(__AVX512VPOPCNTDQ__)
    return _mm256_popcnt_epi64((__m256i)x);
#else
    rs_u8x32 table = {
    //   0  1  2  3  4  5  6  7
         0, 1, 1, 2, 1, 2, 2, 3,
    //   8  9  a  b  c  d  e  f
         1, 2, 2, 3, 2, 3, 3, 4,
    // repeated for high lane:
         0, 1, 1, 2, 1, 2, 2, 3,
         1, 2, 2, 3, 2, 3, 3, 4
    };
    rs_u8x32 mask = {
         15, 15, 15, 15,    15, 15, 15, 15,
         15, 15, 15, 15,    15, 15, 15, 15,
         15, 15, 15, 15,    15, 15, 15, 15,
         15, 15, 15, 15,    15, 15, 15, 15
    };
    rs_u8x32 zero = {
         0, 0, 0, 0,    0, 0, 0, 0,
         0, 0, 0, 0,    0, 0, 0, 0,
         0, 0, 0, 0,    0, 0, 0, 0,
         0, 0, 0, 0,    0, 0, 0, 0
    };
    rs_u8x32 shr4 = (rs_u8x32)_mm256_srli_epi16((__m256i)x, 4) & mask;
    rs_u8x32 pop8 =
        (rs_u8x32)_mm256_shuffle_epi8((__m256i)table, (__m256i)(x & mask)) +
        (rs_u8x32)_mm256_shuffle_epi8((__m256i)table, (__m256i)shr4);
    return (rs_u64x4)_mm256_sad_epu8((__m256i)pop8, (__m256i)zero);
#endif
}
#endif

uint32_t RankMap::rank1(uint32_t offset) const {
    size_t lim = level0.size()*L0_SIZE;
    if (offset >= lim) offset = lim-1;

    size_t i2 = offset/L1_SIZE;
    size_t i1 = offset/L0_SIZE;
    size_t i0 = offset/W_SIZE;
    size_t bit = offset%W_SIZE;

    uint64_t mask = UINT64_C(0xffffffffffffffff) << bit;
    auto out =
        (&level2[0].v[0])[i2] +
        (&level1[0].v[0])[i1] +
        __builtin_popcountll(~mask & (&level0[0].v[0])[i0]);

    const RankLevel0 *l0 = &level0[i1];

#ifdef __AVX2__
    int sel = i0%8;
    rs_u32x8 sel4 = { sel, sel, sel, sel, sel, sel, sel, sel };
    rs_u32x8 idx0 = { 0, 0, 1, 1, 2, 2, 3, 3 };
    rs_u32x8 idx1 = { 4, 4, 5, 5, 6, 6, 7, 7 };
    rs_u64x4 sum4 = vpop(l0->a[0] & (rs_u8x32)(idx0 < sel4)) + vpop(l0->a[1] & (rs_u8x32)(idx1 < sel4));
    rs_u64x4 sum2 = _mm256_bsrli_epi128(sum4, 8) + sum4;
    out += sum2[0] + sum2[2];
    return out;
#else
    for (size_t i = 0; i < L0_COUNT; ++i)
        out += __builtin_popcountll(l0->v[i]) * (i < i0%L0_COUNT);
#endif

    return out;
}

RankSelect1Map::RankSelect1Map(const RankBuilder &builder)
 : RankMap(builder)
{
    sample1.push_back(0);

    size_t sum = 0;
    for (size_t i = 0; i < builder.bitmap.size(); ++i) {
        auto x = builder.bitmap[i];
        auto pop = __builtin_popcountll(x);
        if (pop + MOD_UP(sum,SAMPLE_RATE) >= SAMPLE_RATE)
            sample1.push_back(i/L0_COUNT/L1_COUNT/(L2_COUNT/2));
        sum += pop;
    }

    num1s = sum;
}

// Return the offset of the n-th 1-bit (counting from n=0)
static int select(uint64_t mask, int n) {
#ifdef __BMI2__
    return __builtin_ctzll(__builtin_ia32_pdep_di(UINT64_C(1) << n, mask));
#else
    int t, i = n, r = 0;
    const uint64_t m1 = 0x5555555555555555ULL; // even bits
    const uint64_t m2 = 0x3333333333333333ULL; // even 2-bit groups
    const uint64_t m4 = 0x0f0f0f0f0f0f0f0fULL; // even nibbles
    const uint64_t m8 = 0x00ff00ff00ff00ffULL; // even bytes
    uint64_t c1 = mask;
    uint64_t c2 = c1 - ((c1 >> 1) & m1);
    uint64_t c4 = ((c2 >> 2) & m2) + (c2 & m2);
    uint64_t c8 = ((c4 >> 4) + c4) & m4;
    uint64_t c16 = ((c8 >> 8) + c8) & m8;
    uint64_t c32 = (c16 >> 16) + c16;
    int c64 = (int)(((c32 >> 32) + c32) & 0x7f);
    t = (c32    ) & 0x3f; if (i >= t) { r += 32; i -= t; }
    t = (c16>> r) & 0x1f; if (i >= t) { r += 16; i -= t; }
    t = (c8 >> r) & 0x0f; if (i >= t) { r +=  8; i -= t; }
    t = (c4 >> r) & 0x07; if (i >= t) { r +=  4; i -= t; }
    t = (c2 >> r) & 0x03; if (i >= t) { r +=  2; i -= t; }
    t = (c1 >> r) & 0x01; if (i >= t) { r +=  1;         }
    if (n >= c64) r = -1;
    return r;
#endif
}

uint32_t RankSelect1Map::select1(uint32_t rank1) const {
    assert (rank1 < num1s);

    uint32_t sample = rank1/SAMPLE_RATE;
    uint32_t hl2_off = sample1[sample]; // L2 half-block containing (sample*1024)-th bit
    assert (sample >= sample1.size() || sample1[sample+1] <= hl2_off+1); // !!! fixme

    // Add in the L1 offset from L2 position
    uint32_t l1_off = hl2_off*(L2_COUNT/2);

#ifdef __AVX2__
    int l2_cutoff = static_cast<int>(rank1);
    rs_u32x8 l2_filter = {
        l2_cutoff, l2_cutoff, l2_cutoff, l2_cutoff,
        l2_cutoff, l2_cutoff, l2_cutoff, l2_cutoff
    };

    // It's critical that __builtin_ctz(0) = tzcnt(0) = 32
    uint32_t l2le =
        __builtin_ctz(_mm256_movemask_epi8((&level2[0].a[0])[hl2_off+0] > l2_filter)) +
        __builtin_ctz(_mm256_movemask_epi8((&level2[0].a[0])[hl2_off+1] > l2_filter));

    // -1 is for the 0th entry which always compares true
    l1_off += (l2le/4) - 1;
#else
    auto l2_base = &(&level2[0].v[0])[l1_off];
    assert (l2_base[0] <= rank1);
    for (size_t i = 1; i < L2_COUNT; ++i) // each L2 entry = one L1 block
        l1_off += l2_base[i] <= rank1;
#endif

    rank1 -= (&level2[0].v[0])[l1_off];
    const RankLevel1 *l1 = &level1[l1_off];

    // Add in the L0 offset from L1 position
    uint32_t l0_off = l1_off*L1_COUNT;

#ifdef __AVX2__
    short l1_cutoff = static_cast<short>(rank1);
    rs_u16x16 l1_filter = {
        l1_cutoff, l1_cutoff, l1_cutoff, l1_cutoff,
        l1_cutoff, l1_cutoff, l1_cutoff, l1_cutoff,
        l1_cutoff, l1_cutoff, l1_cutoff, l1_cutoff,
        l1_cutoff, l1_cutoff, l1_cutoff, l1_cutoff
    };

    uint32_t l1le =
        __builtin_ctz(_mm256_movemask_epi8(l1->a[0] > l1_filter)) +
        __builtin_ctz(_mm256_movemask_epi8(l1->a[1] > l1_filter));

    // Divide by 2 because we extracted 2x 1s per comparison and -1 for useless 0 entry
    l0_off += (l1le/2) - 1;
#else
    assert (l1->v[0] <= rank1);
    for (size_t i = 1; i < L1_COUNT; ++i)
        l0_off += l1->v[i] <= rank1;
#endif

    rank1 -= (&level1[0].v[0])[l0_off];
    const RankLevel0 *l0 = &level0[l0_off];

#ifdef __AVX2__
    rs_u32x8 idx  = { 0, 4, 1, 5, 2, 6, 3, 7 };
    short l0_cutoff = static_cast<short>(rank1);
    rs_u16x16 l0_filter = {
        l0_cutoff, l0_cutoff, l0_cutoff, l0_cutoff,
        l0_cutoff, l0_cutoff, l0_cutoff, l0_cutoff,
        l0_cutoff, l0_cutoff, l0_cutoff, l0_cutoff,
        l0_cutoff, l0_cutoff, l0_cutoff, l0_cutoff
    };

    rs_u64x4 elt0 = vpop(l0->a[0]); // { e0, e1, e2, e3 }
    rs_u64x4 elt1 = vpop(l0->a[1]); // { e4, e5, e6, e7 }

    // pack1 = { e0, -, e1, -, e4, -, e5, -, e2, -, e3, -, e6, -, e7, - }
    rs_u16x16 pack1 = (rs_u16x16)_mm256_packus_epi32((__m256i)elt0, (__m256i)elt1);
    // pack2 = { e0, e1, e4, e5, e0, e1, e4, e5, e2, e3, e6, e7, e2, e3, e6, e7 }
    rs_u16x16 pack2 = (rs_u16x16)_mm256_packus_epi32((__m256i)pack1, (__m256i)pack1);
    // { e0, e1, e4, e5, e2, e3, e6, e7, e0, e1, e2, e3, e4, e5, e6, e7 }
    rs_u16x16 pack3 = (rs_u16x16)_mm256_permutevar8x32_epi32((__m256i)pack2, (__m256i)idx);
    // Prefix-sum
    rs_u16x16 sum1 = pack3;
    rs_u16x16 sum2 = sum1 + (rs_u16x16)_mm256_bslli_epi128((__m256i)sum1, 2);
    rs_u16x16 sum4 = sum2 + (rs_u16x16)_mm256_bslli_epi128((__m256i)sum2, 4);
    rs_u16x16 sum8 = sum4 + (rs_u16x16)_mm256_bslli_epi128((__m256i)sum4, 8);
    // Guaranteed to have a sum8[15] > cutoff, since we know the term is contained in the block
    rs_u16x16 pick0 = (rs_u16x16)_mm256_bslli_epi128((__m256i)sum8, 2);
    // Low-order zeroes (cases where sum8 <= l0_filter) are doubled by 8-bit sampling (ie: /2)
    int num = __builtin_ctz(_mm256_movemask_epi8((__m256i)(sum8 > l0_filter)))/2;
    uint32_t outsum = pick0[num];
#else
    uint32_t count = 0;
    int num = 0;
    uint32_t outsum = 0;
    for (size_t i = 0; i < L0_COUNT-1; ++i) {
        count += __builtin_popcountll(l0->v[i]);
        if (count <= rank1) {
            num = i+1;
            outsum = count;
        }
    }
#endif

    return l0_off*L0_SIZE + W_SIZE*num + select(l0->v[num], rank1 - outsum);
}
