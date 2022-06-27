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

namespace wcl {

// behave's like std::span or llvm's ArrayRef
template <class T>
class span {
 private:
  const T* _begin = nullptr;
  const T* _end = nullptr;

 public:
  // Constructors
  span() = default;
  span(const span&) = default;
  span(span&&) = deafult;
  span(const T& elem) : _begin(&elem), _end(&elem + 1) {}
  span(const T* begin, const T* end) : _begin(begin), _end(end) {}
  span(const T* data, size_t size) : _begin(data), _end(data + size) {}
  span(const std::vector<T>& vec) : _begin(vec.data()), _end(vec.data() + vec.size()) {}
  template <size_t small_size>
  span(const small_vector<T, small_size>& vec)
      : _begin(vec.data()), _end(vec.data() + vec.size()) {}
  template <size_t size>
  span(const T (&arr)[size]) : _begin(arr), _end(arr + size) {}
  span(const std::initializer_list<T>& init_list)
      : _begin(init_list.begin()), _end(init_list.end()) {}

  // assignment
  span& operator=(const span&) = default;
  span& operator=(span&&) = default;
  span& operator=(const std::vector<T>& vec) {
    span<T> new_ref(vec);
    return *this = new_ref;
  }
  template <size_t size>
  span& operator=(const T (&arr)[size]) {
    span<T> new_ref(arr);
    return *this = new_ref;
  }

  // standard accessors
  size_t size() const { return _end - _begin; }

  const T& operator[](size_t index) { return _begin[index]; }

  const T* begin() const { return _begin; }

  const T* end() const { return _end; }

  const T& front() const { return *_begin; }

  const T& back() const { return *(_end - 1); }

  // sub views
  span<T> sub(size_t start, size_t size) const {
    return span<T>(_begin + start, _begin + start + size);
  }

  span<T> remove_prefix(size_t num = 1) const { return span<T>(_begin + num, _end); }

  span<T> remove_suffix(size_t num = 1) const { return span<T>(_begin, _end - num); }

  span<T> first(size_t num = 1) const { return span<T>(_begin, _begin + num); }

  span<T> last(size_t num = 1) const { return span<T>(_end - num, _end); }
};

}  // namespace wcl
