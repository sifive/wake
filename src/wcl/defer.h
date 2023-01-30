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

#include <functional>

#include "optional.h"

namespace wcl {

// NOTE: defer is much faster and cheaper than
//       opt_defer, allowing fully blown inlining,
//       but it does not allow default construction
//       which makes it clunky to use in conditional
//       cases. You can still however use it often
//       by setting defer unconditionally and then
//       conditionally nullifying it.
template <class F>
class defer {
 private:
  wcl::optional<F> f;

 public:
  defer() = delete;
  defer(const defer&) = delete;
  defer(defer&&) = default;
  defer(F&& f) : f(wcl::inplace_t{}, std::move(f)) {}
  defer(const F& f) : f(wcl::inplace_t{}, f) {}

  void nullify() { f = {}; }

  ~defer() {
    if (f) (*f)();
  }
};

template <class F>
defer<F> make_defer(F&& f) {
  return defer(std::forward<F>(f));
}

// NOTE: opt_defer requires a dynamic memory allocation,
//       a non-trivial amount of indirection, and
//       vtable accesses. Prefer using only on
//       expensive resources like file IO.
class opt_defer {
 private:
  wcl::optional<std::function<void()>> f;

 public:
  opt_defer(const opt_defer&) = delete;
  opt_defer(opt_defer&& d) = default;
  template <class F>
  opt_defer(F&& f) : f(wcl::inplace_t{}, std::forward<F>(f)) {}
  ~opt_defer() {
    if (f) (*f)();
  }
};

template <class F>
opt_defer make_opt_defer(F&& f) {
  return opt_defer(std::forward<F>(f));
}

}  // namespace wcl
