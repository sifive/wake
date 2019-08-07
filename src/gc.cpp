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

#define INITIAL_HEAP_SIZE 1024

HeapObject::~HeapObject() { }

bool HeapObject::is_work() const {
  return false;
}

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

void PadObject::format(std::ostream &os, FormatState &state) const {
  os << "PadObject";
}

Hash PadObject::hash() const {
  assert(0 /* unreachable */);
  return Hash();
}

Placement MovedObject::moveto(PadObject *free) {
  return Placement(to, free);
}

Placement MovedObject::descend(PadObject *free) {
  assert(0 /* unreachable */);
  return Placement(0, 0);
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

Heap::Heap(bool profile_heap_, double heap_factor_)
 : profile_heap(profile_heap_),
   heap_factor(heap_factor_),
   begin(static_cast<PadObject*>(::malloc(sizeof(PadObject)*INITIAL_HEAP_SIZE))),
   end(begin + INITIAL_HEAP_SIZE),
   free(begin),
   last_pads(0),
   most_pads(0),
   roots(),
   finalize(nullptr) {
}

Heap::~Heap() {
  GC(0);
  assert (free == begin);
  ::free(begin);
  if (profile_heap) {
    std::stringstream s;
    s << "GC: max_kept=" << most_pads << std::endl;
    auto str = s.str();
    status_write(2, str.data(), str.size());
  }
}

void Heap::GC(size_t requested_pads) {
  size_t no_gc_overrun = (free-begin) + requested_pads;
  size_t estimate_desired_size = heap_factor*last_pads + requested_pads;
  size_t elems = std::max(no_gc_overrun, estimate_desired_size);
  PadObject *newbegin = static_cast<PadObject*>(::malloc(elems*sizeof(PadObject)));
  Placement progress(newbegin, newbegin);

  for (RootRing *root = roots.next; root != &roots; root = root->next) {
    if (!root->root) continue;
    auto out = root->root->moveto(progress.free);
    progress.free = out.free;
    root->root = out.obj;
  }

  while (progress.obj != progress.free) {
    progress = progress.obj->descend(progress.free);
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
  if (last_pads > most_pads) {
    most_pads = last_pads;
  }

  // Contain heap growth due to no_gc_overrun pessimism
  size_t desired_sized = heap_factor*last_pads + requested_pads;
  if (desired_sized < elems) {
    end = newbegin + desired_sized;
  }

  if (profile_heap) {
    std::stringstream s;
    s << "GC: kept=" << last_pads << " desire=" << desired_sized << " size=" << elems << std::endl;
    auto str = s.str();
    status_write(2, str.data(), str.size());
  }
}

DestroyableObject::DestroyableObject(Heap &h) : next(h.finalize) {
  h.finalize = this;
}
