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

Category MovedObject::category() const {
  // invoked by ~Target
  return to->category();
}

struct HeapStats {
  const char *type;
  size_t objects, pads;
  HeapStats() : type(nullptr), objects(0), pads(0) { }
};

struct Space {
  size_t size;
  size_t alloc;
  PadObject *array;

  Space(size_t size_ = INITIAL_HEAP_SIZE);
  ~Space();

  void resize(size_t size_);
};

Space::Space(size_t size_)
 : size(size_),
   alloc(size_),
   array(static_cast<PadObject*>(::malloc(sizeof(PadObject)*size))) {
  assert(array);
}

Space::~Space() {
  ::free(array);
}

void Space::resize(size_t size_) {
  if (alloc < size_ || 3*size_ < alloc) {
    alloc = size_ + (size_ >> 1);
    void *tmp = ::realloc(static_cast<void*>(array), sizeof(PadObject)*alloc);
    assert(tmp);
    array = static_cast<PadObject*>(tmp);
  }
  size = size_;
}

struct Heap::Imp {
  int profile_heap;
  double heap_factor;
  Space spaces[2];
  int space;
  size_t last_pads;
  size_t most_pads;
  HeapStats peak[10];
  HeapObject *finalize;

  Imp(int profile_heap_, double heap_factor_)
   : profile_heap(profile_heap_),
     heap_factor(heap_factor_),
     spaces(),
     space(0),
     last_pads(0),
     most_pads(0),
     peak(),
     finalize(nullptr) { }
};

Heap::Heap(int profile_heap_, double heap_factor_)
 : imp(new Imp(profile_heap_, heap_factor_)),
   roots(),
   free(imp->spaces[imp->space].array),
   end(free + imp->spaces[imp->space].size) {
}

Heap::~Heap() {
  GC(0);
  assert (free == imp->spaces[imp->space].array);
}

size_t Heap::used() const {
  return (free - imp->spaces[imp->space].array) * sizeof(PadObject);
}

size_t Heap::alloc() const {
  return (end - imp->spaces[imp->space].array) * sizeof(PadObject);
}

size_t Heap::avail() const {
  return (end - free) * sizeof(PadObject);
}

void *Heap::scratch(size_t bytes) {
  size_t size = (bytes+sizeof(PadObject)-1)/sizeof(PadObject);
  Space &idle = imp->spaces[imp->space ^ 1];
  if (idle.alloc < size) idle.resize(size);
  return idle.array;
}

void Heap::report() const {
  if (imp->profile_heap) {
    std::stringstream s;
    s << "------------------------------------------" << std::endl;
    s << "Peak live heap " << (imp->most_pads*8) << " bytes" << std::endl;
    s << "------------------------------------------" << std::endl;
    s << "  Object type          Objects       Bytes" << std::endl;
    s << "  ----------------------------------------" << std::endl;
    for (size_t i = 0; i < sizeof(imp->peak)/sizeof(imp->peak[0]); ++i) {
      const HeapStats &x = imp->peak[i];
      if (!x.type) continue;
      s << "  "
        << std::setw(20) << std::left  << x.type
        << std::setw(8)  << std::right << x.objects
        << std::setw(12) << std::right << (x.pads*sizeof(PadObject))
        << std::endl;
    }
    s << "------------------------------------------" << std::endl;
    status_write(STREAM_LOG, s.str());
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
  Space &from = imp->spaces[imp->space];
  size_t no_gc_overrun = (free-from.array) + requested_pads;
  size_t estimate_desired_size = imp->heap_factor*imp->last_pads + requested_pads;
  size_t elems = std::max(no_gc_overrun, estimate_desired_size);

  imp->space ^= 1;
  Space &to = imp->spaces[imp->space];
  to.resize(elems);

  Placement progress(to.array, to.array);
  std::map<const char *, ObjectStats> stats;

  for (RootRing *root = roots.next; root != &roots; root = root->next) {
    if (!root->root) continue;
    auto out = root->root->moveto(progress.free);
    progress.free = out.free;
    root->root = out.obj;
  }

  int profile = imp->profile_heap;
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
  for (HeapObject *obj = imp->finalize; obj; obj = next) {
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
  imp->finalize = tail;

  end = to.array + elems;
  free = progress.free;
  imp->last_pads = free - to.array;
  // Contain heap growth due to no_gc_overrun pessimism
  size_t desired_sized = imp->heap_factor*imp->last_pads + requested_pads;
  if (desired_sized < elems) {
    end = to.array + desired_sized;
  }

  if (imp->profile_heap) {
    std::stringstream s;
    StatOrder order;
    std::vector<StatOrder::Kind> top(stats.begin(), stats.end());
    std::sort(top.begin(), top.end(), order);

    if (imp->profile_heap > 1 && !top.empty()) {
      s << "------------------------------------------" << std::endl;
      s << "Live heap " << (imp->last_pads*8) << " bytes" << std::endl;
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
      status_write(STREAM_LOG, s.str());
    }

    if (imp->last_pads > imp->most_pads) {
      imp->most_pads = imp->last_pads;
      size_t max = top.size();
      if (max > sizeof(imp->peak)/sizeof(imp->peak[0]))
        max = sizeof(imp->peak)/sizeof(imp->peak[0]);
      for (size_t i = 0; i < max; ++i) {
        StatOrder::Kind &x = top[i];
        HeapStats &hs = imp->peak[i];
        hs.type = x.first;
        hs.objects = x.second.objects;
        hs.pads = x.second.pads;
      }
    }
  }
}

Category Value::category() const {
  return VALUE;
}

DestroyableObject::DestroyableObject(Heap &h) : next(h.imp->finalize) {
  h.imp->finalize = this;
}
