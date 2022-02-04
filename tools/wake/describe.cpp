/*
 * Copyright 2019 SiFive, Inc.
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

// Open Group Base Specifications Issue 7
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <re2/re2.h>
#include <assert.h>

#include <iostream>
#include <string>
#include <deque>
#include <unordered_map>

#include "util/shell.h"
#include "util/execpath.h"
#include "runtime/database.h"
#include "describe.h"

#define SHORT_HASH 8

static void indent(const std::string& tab, const std::string& body) {
  size_t i, j;
  for (i = 0; (j = body.find('\n', i)) != std::string::npos; i = j+1) {
    std::cout << "\n" << tab;
    std::cout.write(body.data()+i, j-i);
  }
  std::cout.write(body.data()+i, body.size()-i);
  std::cout << std::endl;
}

static std::string describe_hash(const std::string &hash, bool verbose, bool stale) {
  if (stale) return "<out-of-date>";
  if (verbose) return hash;
  return hash.substr(0, SHORT_HASH);
}

static void describe_human(const std::vector<JobReflection> &jobs, bool debug, bool verbose) {
  for (auto &job : jobs) {
    std::cout << "Job " << job.job;
    if (!job.label.empty())
      std::cout << " (" << job.label << ")";
    std::cout << ":" << std::endl
      << "  Command-line:";
    for (auto &arg : job.commandline) std::cout << " " << shell_escape(arg);
    std::cout
      << std::endl
      << "  Environment:" << std::endl;
    for (auto &env : job.environment)
      std::cout << "    " << shell_escape(env) << std::endl;
    std::cout
      << "  Directory: " << job.directory << std::endl
      << "  Built:     " << job.endtime.as_string() << std::endl
      << "  Runtime:   " << job.usage.runtime << std::endl
      << "  CPUtime:   " << job.usage.cputime << std::endl
      << "  Mem bytes: " << job.usage.membytes << std::endl
      << "  In  bytes: " << job.usage.ibytes << std::endl
      << "  Out bytes: " << job.usage.obytes << std::endl
      << "  Status:    " << job.usage.status << std::endl
      << "  Stdin:     " << job.stdin_file << std::endl;
    if (verbose) {
      std::cout << "  Wake run:  " << job.wake_start << " (" << job.wake_cmdline << ")" << std::endl;
      std::cout << "Visible:" << std::endl;
      for (auto &in : job.visible)
        std::cout << "  " << describe_hash(in.hash, verbose, job.stale)
                  << " " << in.path << std::endl;
    }
    std::cout << "Inputs:" << std::endl;
    for (auto &in : job.inputs)
      std::cout << "  " << describe_hash(in.hash, verbose, job.stale)
                << " " << in.path << std::endl;
    std::cout << "Outputs:" << std::endl;
    for (auto &out : job.outputs)
      std::cout << "  " << describe_hash(out.hash, verbose, false)
                << " " << out.path << std::endl;
    if (debug) {
      std::cout << "Stack:";
      indent("  ", job.stack);
    }
    if (!job.stdout_payload.empty()) {
      std::cout << "Stdout:";
      indent("  ", job.stdout_payload);
    }
    if (!job.stderr_payload.empty()) {
      std::cout << "Stderr:";
      indent("  ", job.stderr_payload);
    }
    if (!job.tags.empty()) {
      std::cout << "Tags:" << std::endl;
      for (auto &x : job.tags) {
        std::cout << "  " << x.uri << ": ";
        indent("    ", x.content);
      }
    }
  }
}

static void describe_shell(const std::vector<JobReflection> &jobs, bool debug, bool verbose) {
  std::cout << "#! /bin/sh -ex" << std::endl;

  for (auto &job : jobs) {
    std::cout << std::endl << "# Wake job " << job.job;
    if (!job.label.empty()) std::cout << " (" << job.label << ")";
    std::cout << ":" << std::endl;
    std::cout << "cd " << shell_escape(get_cwd()) << std::endl;
    if (job.directory != ".") {
      std::cout << "cd " << shell_escape(job.directory) << std::endl;
    }
    std::cout << "env -i \\" << std::endl;
    for (auto &env : job.environment) {
      std::cout << "\t" << shell_escape(env) << " \\" << std::endl;
    }
    for (auto &arg : job.commandline) {
      std::cout << shell_escape(arg) << " \\" << std::endl << '\t';
    }
    std::cout << "< " << shell_escape(job.stdin_file) << std::endl << std::endl;
    std::cout
      << "# When wake ran this command:" << std::endl
      << "#   Built:     " << job.endtime.as_string() << std::endl
      << "#   Runtime:   " << job.usage.runtime << std::endl
      << "#   CPUtime:   " << job.usage.cputime << std::endl
      << "#   Mem bytes: " << job.usage.membytes << std::endl
      << "#   In  bytes: " << job.usage.ibytes << std::endl
      << "#   Out bytes: " << job.usage.obytes << std::endl
      << "#   Status:    " << job.usage.status << std::endl;
    if (verbose) {
      std::cout << "#  Wake run:  " << job.wake_start << " (" << job.wake_cmdline << ")" << std::endl;
      std::cout << "# Visible:" << std::endl;
      for (auto &in : job.visible)
        std::cout << "#  " << describe_hash(in.hash, verbose, job.stale)
                  << " " << in.path << std::endl;
    }
    std::cout
      << "# Inputs:" << std::endl;
    for (auto &in : job.inputs)
      std::cout << "#  " << describe_hash(in.hash, verbose, job.stale)
                << " " << in.path << std::endl;
    std::cout << "# Outputs:" << std::endl;
    for (auto &out : job.outputs)
      std::cout << "#  " << describe_hash(out.hash, verbose, false)
                << " " << out.path << std::endl;
    if (debug) {
      std::cout << "# Stack:";
      indent("#   ", job.stack);
    }
    if (!job.stdout_payload.empty()) {
      std::cout << "# Stdout:";
      indent("#   ", job.stdout_payload);
    }
    if (!job.stderr_payload.empty()) {
      std::cout << "# Stderr:";
      indent("#   ", job.stderr_payload);
    }
    if (!job.tags.empty()) {
      std::cout << "# Tags:" << std::endl;
      for (auto &x : job.tags) {
        std::cout << "   " << x.uri << ": ";
        indent("#     ", x.content);
      }
    }
  }
}

void describe(const std::vector<JobReflection> &jobs, bool script, bool debug, bool verbose, const char *taguri) {
  if (taguri) {
    for (auto &job : jobs)
      for (auto &tag : job.tags)
        if (tag.uri == taguri)
          std::cout << tag.content << std::endl;
  } else if (script) {
    describe_shell(jobs, debug, verbose);
  } else {
    describe_human(jobs, debug, verbose);
  }
}

class BitVector {
public:
  BitVector() : imp() { }
  BitVector(BitVector &&x) : imp(std::move(x.imp)) { }

  bool get(size_t i) const;
  void toggle(size_t i);
  long max() const;

  // Bulk operators
  BitVector& operator |= (const BitVector &o);
  void clear(const BitVector &o);

private:
  mutable std::vector<uint64_t> imp;
};

bool BitVector::get(size_t i) const {
  size_t j = i / 64, k = i % 64;
  if (j >= imp.size()) return false;
  return (imp[j] >> k) & 1;
}

void BitVector::toggle(size_t i) {
  size_t j = i / 64, k = i % 64;
  if (j >= imp.size()) imp.resize(j+1, 0);
  imp[j] ^= static_cast<uint64_t>(1) << k;
}

long BitVector::max() const {
  while (!imp.empty()) {
    uint64_t x = imp.back();
    if (x != 0) {
      // Find the highest set bit
      int best = 0;
      for (int i = 0; i < 64; ++i)
        if (((x >> i) & 1)) best = i;
      return ((imp.size()-1)*64) + best;
    } else {
      imp.pop_back();
    }
  }
  return -1;
}

BitVector& BitVector::operator |= (const BitVector &o) {
  size_t both = std::min(imp.size(), o.imp.size());
  for (size_t i = 0; i < both; ++i)
    imp[i] |= o.imp[i];
  if (imp.size() < o.imp.size())
    imp.insert(imp.end(), o.imp.begin() + both, o.imp.end());
  return *this;
}

void BitVector::clear(const BitVector &o) {
  size_t both = std::min(imp.size(), o.imp.size());
  for (size_t i = 0; i < both; ++i)
    imp[i] &= ~o.imp[i];
}

struct GraphNode {
  size_t usedUp, usesUp;
  std::vector<long> usedBy;
  std::vector<long> uses;
  BitVector closure;
  GraphNode() : usedUp(0), usesUp(0) { }
};

std::ostream & operator << (std::ostream &os, const GraphNode &node) {
  os << "  uses";
  for (auto x : node.uses) os << " " << x;
  os << std::endl;
  os <<"  usedBy";
  for (auto x : node.usedBy) os << " " << x;
  os << std::endl;
  os << "  closure ";
  for (long i = 0; i <= node.closure.max(); ++i)
    os << (node.closure.get(i)?"X":" ");
  return os << std::endl;
}

JAST create_tagdag(Database &db, const std::string &tagExpr) {
  RE2 exp(tagExpr);

  // Pick only those tags that match the RegExp
  std::unordered_multimap<long, JobTag> relevant;
  for (auto &tag : db.get_tags())
    if (RE2::FullMatch(tag.uri, exp))
      relevant.emplace(tag.job, std::move(tag));

  // Create a bidirectional view of the graph
  std::unordered_map<long, GraphNode> graph;
  auto edges = db.get_edges();
  for (auto &x : edges) {
    graph[x.user].uses.push_back(x.used);
    graph[x.used].usedBy.push_back(x.user);
  }

  // Working queue for Job ids
  std::deque<long> queue;
  // Compressed map for tags
  std::vector<JobTag> uris;

  // Explore from all nodes which use nothing (ie: build leafs)
  for (auto &n : graph)
    if (n.second.uses.empty())
      queue.push_back(n.first);

  // As we explore, accumulate the transitive closure of relevant nodes via BitVector
  while (!queue.empty()) {
    long job = queue.front();
    queue.pop_front();
    GraphNode &me = graph[job];

    // Compute the closure over anything relevant we use
    for (auto usesJob : me.uses)
      me.closure |= graph[usesJob].closure;

    // If we are relevant, add us to the bitvector, and top-sort the relevant jobs
    auto rel = relevant.equal_range(job);
    if (rel.first != rel.second) {
      me.closure.toggle(uris.size());
      for (; rel.first != rel.second; ++rel.first)
        uris.emplace_back(std::move(rel.first->second));
    }

    // Enqueue anything for which we are the last dependent
    for (auto userJob : me.usedBy) {
      GraphNode &user = graph[userJob];
      assert (user.usesUp < user.uses.size());
      if (++user.usesUp == user.uses.size())
        queue.push_back(userJob);
    }
  }

  // Explore from nodes used by nothing (ie: build targets)
  for (auto &n : graph)
    if (n.second.usedBy.empty())
      queue.push_back(n.first);

  // As we explore, emit those nodes which are relevant to JSON
  JAST out(JSON_ARRAY);
  while (!queue.empty()) {
    long job = queue.front();
    queue.pop_front();
    GraphNode &me = graph[job];

    // Enqueue anything for which we are the last user
    for (auto usesJob : me.uses) {
      GraphNode &uses = graph[usesJob];
      assert (uses.usedUp < uses.usedBy.size());
      if (++uses.usedUp == uses.usedBy.size())
        queue.push_back(usesJob);
    }

    // If we are a relevant node, compute the closure
    if (relevant.find(job) == relevant.end()) continue;

    // Get our own name
    long max = me.closure.max();
    assert (max != -1 && me.closure.get(max));
    me.closure.toggle(max);

    JAST &entry = out.add(JSON_OBJECT);
    entry.add("job", JSON_INTEGER, std::to_string(uris[max].job));

    JAST &tags = entry.add("tags", JSON_OBJECT);
    for (size_t i = max; i < uris.size() && uris[i].job == job; ++i)
      tags.add(std::move(uris[i].uri), std::move(uris[i].content));

    JAST &deps = entry.add("deps", JSON_ARRAY);
    while ((max = me.closure.max()) != -1) {
      long depJob = uris[max].job;
      GraphNode &dep = graph[depJob];
      // Add this dependency
      deps.add(JSON_INTEGER, std::to_string(depJob));
      // Elminate transitively reachable children
      assert (dep.closure.get(max));
      me.closure.clear(dep.closure);
    }
  }

  return out;
}
