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

#include <algorithm>
#include <memory>
#include <ostream>
#include <queue>
#include <set>
#include <vector>

namespace wcl {

enum class diff_type_t { Add, Sub, Keep };

template <class T>
struct diff_t {
  diff_type_t type;
  T value;
};

// This is an implementation detail
template <class Iter1, class Iter2>
class edge_t {
 private:
  // The type, tells us the cost and direction;
  diff_type_t _type;

  // Cost of this edge. This is more of an optimization
  // because we could compute it from previous and
  // type but that would be linear in time and this is constant.
  double total_cost;

  // The start position of this edge
  Iter1 iter1;
  Iter2 iter2;

  std::shared_ptr<edge_t> prev;

  static double type_cost(diff_type_t type) {
    if (type == diff_type_t::Keep) return 0.0;
    return 1.0;
  }

  Iter1 advance1() const {
    if (_type != diff_type_t::Add) return iter1 + 1;
    return iter1;
  }

  Iter2 advance2() const {
    if (_type != diff_type_t::Sub) return iter2 + 1;
    return iter2;
  }

 public:
  edge_t(diff_type_t type, Iter1 i1, Iter2 i2) : _type(type), iter1(i1), iter2(i2), prev(nullptr) {
    total_cost = type_cost(type);
  }

  edge_t(diff_type_t type, std::shared_ptr<edge_t> prev) : _type(type), prev(prev) {
    total_cost = prev->total_cost + type_cost(type);
    iter1 = prev->advance1();
    iter2 = prev->advance2();
  }

  bool operator<(const edge_t& other) const { return total_cost >= other.total_cost; }

  diff_type_t type() const { return _type; }

  std::pair<Iter1, Iter2> from() const { return {iter1, iter2}; }

  std::pair<Iter1, Iter2> to() const { return {advance1(), advance2()}; }

  const edge_t* previous() const { return prev.get(); }

  double cost() const { return total_cost; }
};

template <class Iter1, class Iter2>
struct edge_cmp_t {
  bool operator()(const std::shared_ptr<edge_t<Iter1, Iter2>>& a,
                  const std::shared_ptr<edge_t<Iter1, Iter2>>& b) {
    return *a < *b;
  }
};

template <class T>
using seq_diff_t = std::vector<diff_t<T>>;

// TODO: optimize leading string of keeps
template <class T, class Iter1, class Iter2>
seq_diff_t<T> diff(Iter1 begin1, Iter1 end1, Iter2 begin2, Iter2 end2) {
  // Each node in the implicit graph is defined by a pair of iterators. Each edge in the graph
  // starts at a node, and advances one or both of the iterators to get to the next node.
  // If it subtracts, it advances the first iterator. if it adds, it advances the second iterator.
  // If it keeps it advances both (this is like moving diagonally). The cost of an add/sub is 1.0.
  // The cost of a keep is free but both iterators must be equal for it to be valid.
  // The shortest paths in this graph defines the edit distance of the two sequences and further
  // more tells you how to edit the first sequence to get to the second sequence. In order to
  // memoize the shortest path we store the edge we started from as the previous edge in each.
  // Following these backwards gives the return path in reverse order.
  std::set<std::pair<Iter1, Iter2>> visited;
  using edge_type = std::shared_ptr<edge_t<Iter1, Iter2>>;
  std::priority_queue<edge_type, std::vector<edge_type>, edge_cmp_t<Iter1, Iter2>> edges;
  seq_diff_t<T> out;

  // We want to call this later on just the right node. So
  // its easier to make it a function instead.
  auto populate_out = [&](const edge_t<Iter1, Iter2>* node) {
    while (node != nullptr) {
      diff_t<T> diff;
      diff.type = node->type();
      Iter1 iter1;
      Iter2 iter2;
      std::tie(iter1, iter2) = node->from();
      if (diff.type == diff_type_t::Add) {
        diff.value = *iter2;
      } else {
        diff.value = *iter1;
      }
      out.emplace_back(std::move(diff));
      node = node->previous();
    }
    std::reverse(out.begin(), out.end());
  };

  // It makes the code a little nicer if we can assume that at least
  // one edge will be added below so we check for the case where that
  // isn't true here and return early. In that case we can return an
  // empty diff because both are empty sequences and are already equal.
  if (begin1 == end1 && begin2 == end2) return {};

  // Now add our first 3 starting edges
  if (begin2 != end2) {
    edges.emplace(std::make_unique<edge_t<Iter1, Iter2>>(diff_type_t::Add, begin1, begin2));
  }

  if (begin1 != end1) {
    edges.emplace(std::make_unique<edge_t<Iter1, Iter2>>(diff_type_t::Sub, begin1, begin2));
  }

  if (begin2 != end2 && begin1 != end1 && *begin1 == *begin2) {
    edges.emplace(std::make_unique<edge_t<Iter1, Iter2>>(diff_type_t::Keep, begin1, begin2));
  }

  // Now we loop until we get to the end of both iterators
  while (true) {
    Iter1 iter1;
    Iter2 iter2;
    auto edge = edges.top();
    edges.pop();
    std::tie(iter1, iter2) = edge->to();

    // If we already visisted the node we're about to go down
    // we can just skip it because we already found a shorter path
    // here
    if (visited.count({iter1, iter2})) continue;

    // Now that we're about to visit this node mark it as visited so no one else does.
    visited.emplace(iter1, iter2);

    // We should always hit this condition before edges is empty.
    // If this condition is false, one of the following conditions
    // will be true and will add an edge for us to pop.
    if (iter1 == end1 && iter2 == end2) {
      // Because a priority queue was used, we know this is the
      // final edge in a shortest path.
      populate_out(edge.get());
      return out;
    }

    // We assume that keeping is always better than not keeping
    if (iter2 != end2 && iter1 != end1 && *iter1 == *iter2) {
      edges.emplace(std::make_unique<edge_t<Iter1, Iter2>>(diff_type_t::Keep, edge));
    } else {
      if (iter2 != end2) {
        edges.emplace(std::make_unique<edge_t<Iter1, Iter2>>(diff_type_t::Add, edge));
      }

      if (iter1 != end1) {
        edges.emplace(std::make_unique<edge_t<Iter1, Iter2>>(diff_type_t::Sub, edge));
      }
    }
  }

  // The above is an infinite loop so we never get here
}

template <class T>
void display_diff(std::ostream& out, const seq_diff_t<T>& diff, size_t keep_size = 4) {
  // We need a buffer for keeps so we can display them more sainly. This allows us to
  // iterate through the diff, including the keeps but to only display the keeps in
  // a more compact way.
  std::vector<std::string> keep_buf;
  int64_t cur_in_line = 0;
  int64_t cur_out_line = 0;

  auto flush_keeps = [&]() {
    // The buffer might be empty
    if (keep_buf.empty()) return;

    // If not check if its better to display it all or skip ahead
    if (keep_buf.size() <= keep_size) {
      for (const auto& keep_line : keep_buf) {
        out << "  " << keep_line << std::endl;
      }
    } else {
      // TODO: Make color work correctly at later date using proper term lib
      out << "\033[94m@@ -" << cur_in_line << " +" << cur_out_line << " @@\033[0m" << std::endl;
    }

    keep_buf.clear();
  };

  for (auto iter = diff.begin(); iter < diff.end(); ++iter) {
    auto line = *iter;
    switch (line.type) {
      case wcl::diff_type_t::Add:
        cur_out_line++;
        // TODO: Make color work correctly at later date using proper term lib
        flush_keeps();
        out << "\033[32m+ " << line.value << "\033[0m" << std::endl;
        break;
      case wcl::diff_type_t::Sub:
        cur_in_line++;
        // TODO: Make color work correctly at later date using proper term lib
        flush_keeps();
        out << "\033[31m- " << line.value << "\033[0m" << std::endl;
        break;
      case wcl::diff_type_t::Keep:
        keep_buf.push_back(line.value);
        cur_out_line++;
        cur_in_line++;
        break;
    }
  }

  // Finally we probably have some things to emit in the keep buffer
  flush_keeps();
}

};  // namespace wcl
