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

#include <functional>

#include "optional.h"

// NOTE: defer requires a dynamic memory allocation,
//       a non-trivial amount of indirection, and
//       vtable accesses. Prefer using only on
//       expensive resources like file IO.
namespace wcl {
class defer {
 private:
  wcl::optional<std::function<void()>> f;

 public:
  defer(const defer&) = delete;
  defer(defer&& d) = default;
  template <class F>
  defer(F&& f) : f(std::forward<F>(f)) {}
  ~defer() {
    if (f) (*f)();
  }
};

template <class F>
defer make_defer(F&& f) {
  return defer(std::forward<F>(f));
}

}  // namespace wcl
