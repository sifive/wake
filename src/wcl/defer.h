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

namespace wcl {
template <class F>
class defer {
private:
  F f;
  bool moved = false;

public:
  defer(const defer&) = delete;
  defer(defer&& d) : f(std::move(d.f)) {
    d.moved = true;
  }
  defer(F&& f) : f(std::move(f)) {}
  defer(const F& f) : f(f) {}
  ~defer() {
    if(!moved) f();
  }
};

template <class F>
auto make_defer(F&& f) -> defer<F> {
  return defer<F>(f);
}

}
