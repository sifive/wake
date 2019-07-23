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
#include <string.h>
#include <stdlib.h>
#include <assert.h>

HeapObject::~HeapObject() { }

Placement PadObject::moveto(PadObject *free) {
  assert (0 /* unreachable */);
  return Placement(0, 0);
}

Placement PadObject::descend(PadObject *free) {
  return Placement(this + 1, free);
}

void PadObject::format(std::ostream &os, FormatState &state) const {
  assert (0 /* unreachable */);
}

Placement MovedObject::moveto(PadObject *free) {
  return Placement(to, free);
}

Placement MovedObject::descend(PadObject *free) {
  assert(0 /* unreachable */);
  return Placement(0, 0);
}

void MovedObject::format(std::ostream &os, FormatState &state) const {
  assert (0 /* unreachable */);
}

Heap::Heap() {
  begin = static_cast<PadObject*>(::malloc(sizeof(PadObject)*1024));
  end = begin + 1024;
  free = begin;
  last_pads = 0;
  finalize = nullptr;
}

Heap::~Heap() {
  GC(0);
  assert (free == begin);
  ::free(begin);
}

void Heap::GC(size_t requested_pads) {
  size_t elems = (free - begin) * 2 + requested_pads;
  if (4*last_pads > elems) elems = 4*last_pads;

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
}

DestroyableObject::DestroyableObject(Heap &h) : next(h.finalize) {
  h.finalize = this;
}
