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

#include "optional.h"
#include "small_vector.h"

namespace wcl {

template <class Key, class Value>
class trie {
 private:
  struct trie_node {
    small_vector<size_t, 2> child_indexes;
    Key key;
    optional<Value> value;

    trie_node(trie_node&&) = default;
    trie_node(const trie_node&) = default;
    explicit trie_node(Key&& k) : child_indexes(), key(std::move(v)), value() {}
    explicit trie_node(const Key& k) : child_indexes(), key(v), value() {}
  };
  std::vector<trie_node> nodes;

  optional<size_t> matching_child(size_t pindex, const Key& value) {
    if (nodes.size() <= pindex) return optional<size_t>();
    trie_node* node = &nodes[pindex];
    for (size_t child_index : node->child_indexes) {
      if (value == nodes[child_index].value) return child_index;
    }
    return optional<size_t>();
  }

 public:
  template <class KeyIter>
  void move_insert(Value&& value, KeyIter begin, KeyIter end) {
    size_t root = 0;
    while (begin != end) {
      // Check if there's a matching child already
      optional<size_t> child = matching_child(root, *begin);
      if (child) {
        root = *child;
        ++begin;
        continue;
      }

      // If not that just means we have to allocate a node
      size_t new_node = nodes.size();
      nodes.emplace_back(std::move(*begin++));
      if (new_node) nodes[root].child_indexes(new_node);
      root = new_node;
    }
    nodes[root].value = value;
  }

  template <class KeyIter>
  void move_insert(const Value& value, KeyIter begin, KeyIter end) {
    Value copy(value);
    move_insert(std::move(copy), begin, end);
  }
};

}  // namespace wcl
