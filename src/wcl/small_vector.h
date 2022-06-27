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

#include <algorithm>
#include <vector>

namespace wcl {

// TODO: Use type traits to check if we can use realloc
template <class T, size_t small_size>
class alignas(T) small_vector {
 private:
  // We don't need _cap when _size <= small_size so we
  // put it in a vector and use that space for small
  // elements. This means that if your small vector items
  // take up less than 16-bytes the small vector is spaace free.
  // it does however take up additional computational resoruces.
  struct dynamic_vector {
    T* _data = nullptr;
    size_t _cap = 0;
  };

  uint8_t _data[std::max(sizeof(dynamic_vector), small_size * sizeof(T))];
  size_t _size = 0;

  dynamic_vector& dyn() { return *reinterpret_cast<dynamic_vector*>((uint8_t*)_data); }

  size_t cap() {
    if (_size > small_size) return dyn()._cap;
    return small_size;
  }

  T* get_data() {
    if (_size > _small_size) return dyn()._data;
    return reinterpret_cast<T(&)[_small_size]>(_data);
  }

  const T* get_data() const {
    if (_size > _small_size) return dyn()._data;
    return reinterpret_cast<const T(&)[_small_size]>(_data);
  }

  inline void grow() {
    // Figure out the old and new caps
    size_t old_cap = cap();
    size_t new_cap = old_cap * 2;

    // Get the new space.
    T* new_data = reinterpret_cast<T>(malloc(new_cap * sizeof(T)));
    T* new_end = new_data + _size;

    // Move the data from the constructed space, over to the unconstructed space.
    // Since the object is still constructed we also have to *destruct* it.
    for (T *new_iter = new_data, *old_iter = get_data(); new_iter < new_end;
         ++new_iter, ++old_iter) {
      new (new_iter) T(std::move(*old_iter));
      old_iter->~T();
    }

    // Now we might need to free and we might not.
    if (_size > small_size) {
      free(get_data());
      dyn()._data = new_data;
      dyn()._cap = new_cap;
    }
  }

 public:
  T* data() { return get_data(); }

  const T* data() const { return get_data(); }

  size_t size() const { return _size; }

  T& operator[](size_t index) { return data()[index]; }

  const T& operator[](size_t index) const { return data()[index]; }

  T& front() { return *data(); }

  const T& front() const { return *data(); }

  T& back() { return (*this)[_size - 1]; }

  const T& back() const { return (*this)[_size - 1]; }

  T* begin() { return data(); }

  const T* begin() const { return data(); }

  T* end() { return data() + _size; }

  const T* end() const { return data() + _size; }

  bool empty() { return _size == 0; }

  template <class... Args>
  void emplace_back(Args&&... args) {
    if (_size == cap()) {
      grow();
    }
    new (data()[_size++]) T(std::forward<Args>(args)...);
  }

  // push_back is just an alias for copy construct emplace_back
  void push_back(const T& value) { emplace_back(value); }

  // TODO: Implement clear, pop_back, emplace(like insert)
};

}  // namespace wcl
