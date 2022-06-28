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

#include <utility>
#include <cstdint>

namespace wcl {

template <class T>
class alignas(T) optional {
 private:
  uint8_t data[sizeof(T)];
  bool none = true;

 public:
  // Default construct is none
  optional() = default;

  optional(const optional& other) : none(other.none) {
    if (other.none) return;
    new (reinterpret_cast<T*>(data)) T(*other);
  }

  optional(optional&& other) : none(other.none) {
    if (other.none) return;
    new (reinterpret_cast<T*>(data)) T(std::move(*other));
  }

  // Placement new, perfect forwarding construction
  template <class... Args>
  optional(Args&&... args) : none(false) {
    new (reinterpret_cast<T*>(data)) T(std::forward<Args>(args)...);
  }

  ~optional() { reinterpret_cast<T*>(data)->~T(); }

  optional& operator=(const optional& other) {
    if (!none) reinterpret_cast<T*>(data)->~T();
    none = other.none;
    if (none) return *this;
    new (reinterpret_cast<T*>(data)) T(*other);
    return *this;
  }

  optional& operator=(optional&& other) {
    if (!none) reinterpret_cast<T*>(data)->~T();
    none = other.none;
    if (none) return *this;
    new (reinterpret_cast<T*>(data)) T(std::move(*other));
    return *this;
  }

  explicit operator bool() const { return !none; }

  T& operator*() { return *reinterpret_cast<T*>(data); }

  const T& operator*() const { return *reinterpret_cast<const T*>(data); }

  T* operator->() { return reinterpret_cast<const T*>(data); }

  const T* operator->() const { return reinterpret_cast<const T*>(data); }
};

}  // namespace wcl
