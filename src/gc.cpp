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

#include "gc.h"
#include "status.h"
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <sstream>
#include <iomanip>
#include <map>
#include <vector>
#include <algorithm>

#define INITIAL_HEAP_SIZE 1024

HeapObject::~HeapObject() { }

Placement PadObject::moveto(PadObject *free) {
  assert (0 /* unreachable */);
  return Placement(0, 0);
}

Placement PadObject::descend(PadObject *free) {
  return Placement(this + 1, free);
}

HeapStep PadObject::explore(HeapStep step) {
  assert(0 /* unreachable */);
  return step;
}

const char *PadObject::type() const {
  return "PadObject";
}

void PadObject::format(std::ostream &os, FormatState &state) const {
  os << "PadObject";
}

Hash PadObject::hash() const {
  assert(0 /* unreachable */);
  return Hash();
}

Category PadObject::category() const {
  assert(0 /* unreachable */);
  return VALUE;
}

Placement MovedObject::moveto(PadObject *free) {
  return Placement(to, free);
}

Placement MovedObject::descend(PadObject *free) {
  assert(0 /* unreachable */);
  return Placement(0, 0);
}

const char *MovedObject::type() const {
  return "MovedObject";
}

HeapStep MovedObject::explore(HeapStep step) {
  return to->explore(step);
}

void MovedObject::format(std::ostream &os, FormatState &state) const {
  to->format(os, state);
}

Hash MovedObject::hash() const {
  assert(0 /* unreachable */);
  return Hash();
}

Category MovedObject::category() const {
  // invoked by ~Target
  return to->category();
}

Heap::Heap(int profile_heap_, double heap_factor_)
 : profile_heap(profile_heap_),
   heap_factor(heap_factor_),
   begin(static_cast<PadObject*>(::malloc(sizeof(PadObject)*INITIAL_HEAP_SIZE))),
   end(begin + INITIAL_HEAP_SIZE),
   free(begin),
   last_pads(0),
   most_pads(0),
   peak(),
   roots(),
   finalize(nullptr) {
}

Heap::~Heap() {
  GC(0);
  assert (free == begin);
  ::free(begin);
}

void Heap::report() const {
  if (profile_heap) {
    std::stringstream s;
    s << "------------------------------------------" << std::endl;
    s << "Peak live heap " << (most_pads*8) << " bytes" << std::endl;
    s << "------------------------------------------" << std::endl;
    s << "  Object type          Objects       Bytes" << std::endl;
    s << "  ----------------------------------------" << std::endl;
    for (size_t i = 0; i < sizeof(peak)/sizeof(peak[0]); ++i) {
      const HeapStats &x = peak[i];
      if (!x.type) continue;
      s << "  "
        << std::setw(20) << std::left  << x.type
        << std::setw(8)  << std::right << x.objects
        << std::setw(12) << std::right << (x.pads*sizeof(PadObject))
        << std::endl;
    }
    s << "------------------------------------------" << std::endl;
    auto str = s.str();
    status_write(2, str.data(), str.size());
  }
}

struct ObjectStats {
  size_t objects;
  size_t pads;
  ObjectStats() : objects(0), pads(0) { }
};

struct StatOrder {
  typedef std::pair<const char *, ObjectStats> Kind;
  bool operator ()(Kind a, Kind b) { return a.second.pads > b.second.pads; }
};

void Heap::GC(size_t requested_pads) {
  size_t no_gc_overrun = (free-begin) + requested_pads;
  size_t estimate_desired_size = heap_factor*last_pads + requested_pads;
  size_t elems = std::max(no_gc_overrun, estimate_desired_size);
  PadObject *newbegin = static_cast<PadObject*>(::malloc(elems*sizeof(PadObject)));
  Placement progress(newbegin, newbegin);
  std::map<const char *, ObjectStats> stats;

  for (RootRing *root = roots.next; root != &roots; root = root->next) {
    if (!root->root) continue;
    auto out = root->root->moveto(progress.free);
    progress.free = out.free;
    root->root = out.obj;
  }

  int profile = profile_heap;
  while (progress.obj != progress.free) {
    auto next = progress.obj->descend(progress.free);
    if (profile) {
      ObjectStats &s = stats[progress.obj->type()];
      ++s.objects;
      s.pads += (static_cast<PadObject*>(next.obj) - static_cast<PadObject*>(progress.obj));
    }
    progress = next;
  }

  DestroyableObject *tail = nullptr;
  HeapObject *next;
  for (HeapObject *obj = finalize; obj; obj = next) {
    if (typeid(*obj) == typeid(MovedObject)) {
      MovedObject *mo = static_cast<MovedObject*>(obj);
      DestroyableObject *keep = static_cast<DestroyableObject*>(mo->to);
      next = keep->next;
      keep->next = tail;
      tail = keep;
    } else {
      next = static_cast<DestroyableObject*>(obj)->next;
      obj->~HeapObject();
    }
  }
  finalize = tail;

  ::free(begin);
  begin = newbegin;
  end = newbegin + elems;
  free = progress.free;
  last_pads = free - begin;
  // Contain heap growth due to no_gc_overrun pessimism
  size_t desired_sized = heap_factor*last_pads + requested_pads;
  if (desired_sized < elems) {
    end = newbegin + desired_sized;
  }

  if (profile_heap) {
    std::stringstream s;
    StatOrder order;
    std::vector<StatOrder::Kind> top(stats.begin(), stats.end());
    std::sort(top.begin(), top.end(), order);

    if (profile_heap > 1 && !top.empty()) {
      s << "------------------------------------------" << std::endl;
      s << "Live heap " << (last_pads*8) << " bytes" << std::endl;
      s << "------------------------------------------" << std::endl;
      s << "  Object type          Objects       Bytes" << std::endl;
      s << "  ----------------------------------------" << std::endl;
      size_t max = top.size();
      if (max > 5) max = 5;
      for (size_t i = 0; i < max; ++i) {
        StatOrder::Kind &x = top[i];
        s << "  "
          << std::setw(20) << std::left  << x.first
          << std::setw(8)  << std::right << x.second.objects
          << std::setw(12) << std::right << (x.second.pads*sizeof(PadObject))
          << std::endl;
      }
      s << "------------------------------------------" << std::endl;
      auto str = s.str();
      status_write(2, str.data(), str.size());
    }

    if (last_pads > most_pads) {
      most_pads = last_pads;
      size_t max = top.size();
      if (max > sizeof(peak)/sizeof(peak[0]))
        max = sizeof(peak)/sizeof(peak[0]);
      for (size_t i = 0; i < max; ++i) {
        StatOrder::Kind &x = top[i];
        peak[i].type = x.first;
        peak[i].objects = x.second.objects;
        peak[i].pads = x.second.pads;
      }
    }
  }
}

Category Value::category() const {
  return VALUE;
}

DestroyableObject::DestroyableObject(Heap &h) : next(h.finalize) {
  h.finalize = this;
}
