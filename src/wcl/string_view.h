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

#include <string>

namespace wcl {

// behave's like std::string_view or llvm's StringRef
class string_view {
 private:
  const char* _begin = nullptr;
  const char* _end = nullptr;

 public:
  // Constructors
  string_view() = default;
  string_view(const string_view&) = default;
  string_view(string_view&&) = default;
  string_view(const char& elem) : _begin(&elem), _end(&elem + 1) {}
  string_view(const char* begin, const char* end) : _begin(begin), _end(end) {}
  string_view(const char* data, size_t size) : _begin(data), _end(data + size) {}
  string_view(const std::string& str) : _begin(str.data()), _end(str.data() + str.size()) {}
  template <size_t size>
  string_view(const char (&arr)[size]) : _begin(arr), _end(arr + size) {}
  string_view(const std::initializer_list<char>& init_list)
      : _begin(init_list.begin()), _end(init_list.end()) {}

  // assignment
  string_view& operator=(const string_view&) = default;
  string_view& operator=(string_view&&) = default;
  string_view& operator=(const std::string& str) {
    string_view new_ref(str);
    return *this = new_ref;
  }
  template <size_t size>
  string_view& operator=(const char (&arr)[size]) {
    string_view<T> new_ref(arr);
    return *this = new_ref;
  }

  // standard accessors
  size_t size() const { return _end - _begin; }

  const char& operator[](size_t index) const { return _begin[index]; }

  const char* begin() const { return _begin; }

  const char* end() const { return _end; }

  const char& front() const { return *_begin; }

  const char& back() const { return *(_end - 1); }

  // sub views
  string_view sub(size_t start, size_t size) const {
    return string_view(_begin + start, _begin + start + size);
  }

  string_view remove_prefix(size_t num = 1) const { return string_view(_begin + num, _end); }

  string_view remove_suffix(size_t num = 1) const { return string_view(_begin, _end - num); }

  string_view first(size_t num = 1) const { return string_view(_begin, _begin + num); }

  string_view last(size_t num = 1) const { return string_view(_end - num, _end); }

  // convert to string
  std::string str() const { return std::string(_begin, _end); }
};

}  // namespace wcl
