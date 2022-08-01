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

#include <cstdint>
#include <functional>
#include <utility>

inline uint64_t hash_combine(uint64_t a, uint64_t b) {
  return a ^ (b + 0x9e3779b9 + (a << 6) + (a >> 2));
}

// struct pair_hash
// {
//     template <class T1, class T2>
//     std::size_t operator() (const std::pair<T1, T2> &pair) const {
//         return hash_combine(std::hash<T1>{}(pair.first), std::hash<T2>{}(pair.second));
//     }
// };
