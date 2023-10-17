/*
 * Copyright 2023 SiFive, Inc.
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

#include <vector>

namespace wcl {
template <class T, class Iter, class F>
auto split_by_fn(const T& v, Iter begin, Iter end, F f) -> std::vector<decltype(f(begin, end))> {
  auto start_part = begin;
  auto end_part = begin;
  std::vector<decltype(f(begin, end))> out;
  while (end_part < end) {
    if (*end_part == v) {
      out.emplace_back(f(start_part, end_part));
      end_part++;
      start_part = end_part;
    } else {
      end_part++;
    }
  }
  out.emplace_back(start_part, end_part);
  return out;
}

}  // namespace wcl
