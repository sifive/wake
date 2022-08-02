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

// filepath_iterator breaks up a filepath
// into its parts. So if you're iterating over
// foo/bar/baz then this iterator iterates over
// the sequence "foo" and then "bar" and then "baz"
class filepath_iterator {
 private:
  // TODO: This should be a string_view
  const std::string& str;
  size_t start = 0, final = 0;

  // This moves `final` to the next '/' or to the end of the path
  // and moves `start` past '/' if its currently on it.
  void next() {
    if (final < str.size() && str[final] == '/') final++;
    start = final;
    while (final < str.size() && str[final] != '/') {
      final++;
    }
  }

 public:
  explicit filepath_iterator(const std::string& str) : str(str), start(), final() { next(); }

  filepath_iterator(const std::string& str, size_t begin) : str(str), start(begin), final(begin) {
    next();
  }

  filepath_iterator operator++(int) {
    auto out = *this;
    next();
    return out;
  }

  filepath_iterator& operator++() {
    next();
    return *this;
  }

  // TODO: This should be a string_view
  std::string operator*() const { return std::string(str.begin() + start, str.begin() + final); }

  bool operator==(const filepath_iterator& iter) const { return start == iter.start; }

  bool operator!=(const filepath_iterator& iter) const { return start != iter.start; }
};

// filepath_range_ref is a range that can be constructed
// from a reference to an std::string that will outlive it.
// it then adapts the string to a range that uses filepath_iterator
// to iterate over the parts of a file path. Only use this if you
// know that the lifetime of the argument will outlive this iterator.
class filepath_range_ref {
 private:
  // TODO: This should be a string_view
  const std::string& str;

 public:
  filepath_range_ref(const std::string& str) : str(str) {}

  filepath_iterator begin() const { return filepath_iterator(str); }

  filepath_iterator end() const { return filepath_iterator(str, str.size()); }
};

// filepath_range is a range that can be constructed
// from anything an std::string can be constructed from.
// It then adapts the string to a range that uses filepath_iterator
// to iterate over the parts of a file path.
class filepath_range {
 private:
  // TODO: This should be a string_view
  const std::string str;

 public:
  filepath_range(const std::string& str) : str(str) {}

  filepath_iterator begin() const { return filepath_iterator(str); }

  filepath_iterator end() const { return filepath_iterator(str, str.size()); }
};

filepath_range make_filepath_range(const std::string& str) { return filepath_range(str); }

filepath_range make_filepath_range(std::string&& str) { return filepath_range(std::move(str)); }

filepath_range_ref make_filepath_range_ref(const std::string& str) {
  return filepath_range_ref(str);
}

std::string join_paths(std::string a, const std::string& b) {
  if (a.back() == '/') {
    if (b.front() == '/') {
      a.insert(a.end(), b.begin() + 1, b.end());
    } else {
      a += b;
    }
  } else {
    if (b.front() == '/') {
      a += b;
    } else {
      a += '/';
      a += b;
    }
  }

  return a;
}

inline std::string join_paths(std::string a, const std::string& b, const std::string& c) {
  return join_paths(join_paths(a, b), c);
}

inline std::string join_paths(std::string a, const std::string& b, const std::string& c,
                              const std::string& d) {
  return join_paths(join_paths(join_paths(a, b), c), d);
}

inline std::string join_paths(std::string a, const std::string& b, const std::string& c,
                              const std::string& d, const std::string& e) {
  return join_paths(join_paths(join_paths(join_paths(a, b), c), d), e);
}

}  // namespace wcl
