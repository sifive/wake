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

class RankMap {
public:
    // set must only be used on an ascending sequence
    void set(uint32_t x);

    bool get(uint32_t x) const;

    uint32_t rank(uint32_t offset) const;
    uint32_t next(uint32_t offset) const;

private:
    std::vector<uint64_t> bitmap;
    std::vector<uint32_t> sums;
};

#endif
