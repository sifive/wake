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
#include <assert.h>

HeapObject::~HeapObject() { }

Placement PadObject::moveto(PadObject *free) {
  assert (0 /* unreachable */);
  return Placement(0, 0);
}

Placement PadObject::descend(PadObject *free) {
  return Placement(this + 1, free);
}

Placement MovedObject::moveto(PadObject *free) {
  return Placement(to, free);
}

Placement MovedObject::descend(PadObject *free) {
  assert(0 /* unreachable */);
  return Placement(0, 0);
}

Heap::Heap() {
  begin = static_cast<PadObject*>(malloc(sizeof(PadObject)*1024));
  end = begin + 1024;
  free = begin;
  last_pads = 0;
}

Heap::~Heap() {
  // !!! destruct objects
  ::free(begin);
}

void Heap::GC(size_t requested_pads) {
  size_t elems = (free - begin) * 2 + requested_pads;
  if (4*last_pads > elems) elems = 4*last_pads;

  PadObject *newbegin = static_cast<PadObject*>(malloc(elems*sizeof(PadObject)));
  Placement progress(newbegin, newbegin);

  for (RootRing *root = roots.next; root != &roots; root = root->next) {
    auto out = root->root->moveto(progress.free);
    progress.free = out.free;
    root->root = out.obj;
  }

  while (progress.obj != progress.free) {
    progress = progress.obj->descend(progress.free);
  }

  ::free(begin);
  begin = newbegin;
  end = newbegin + elems;
  free = progress.free;
  last_pads = free - begin;
}

struct Tree final : public GCObject<Tree> {
  HeapPointer<Tree> l, r;
  HeapPointer<HeapObject> value;

  Tree(Tree *l_, Tree *r_, HeapObject *value_) : l(l_), r(r_), value(value_) { }
  Placement descend(PadObject *free) override;
};

Placement Tree::descend(PadObject *free) {
  free = l.moveto(free);
  free = r.moveto(free);
  free = value.moveto(free);
  return Placement(next(), free);
}

struct String final : public GCObject<String> {
  String(size_t length_) : length(length_) { }
  String(const String &s);

  const char *c_str() const { return static_cast<const char*>(data()); }

  static String* make(Heap &h, const char *str, size_t length);
  static String* make(Heap &h, const char *str);
  static String* make(Heap &h, size_t length);

  size_t length;
  PadObject *next() { return Parent::next() + 1 + length/sizeof(PadObject); }
};

String* String::make(Heap &h, size_t length) {
  return new (h.alloc((sizeof(String) + length) / sizeof(PadObject) + 1)) String(length);
}

String *String::make(Heap &h, const char *str) {
  auto out = make(h, strlen(str));
  memcpy(out->data(), str, out->length+1);
  return out;
}

String *String::make(Heap &h, const char *str, size_t length) {
  auto out = make(h, length);
  memcpy(out->data(), str, length);
  static_cast<char*>(out->data())[length] = 0;
  return out;
}

String::String(const String &s) : length(s.length) {
  memcpy(data(), s.data(), length+1);
}

/*
#include <iostream>

int main() {
  Heap h;
  String::make(h, "Useless");
  String *s = String::make(h, "Hello world!");
  Tree *t = Tree::make(h, nullptr, nullptr, s);
  Tree *u = Tree::make(h, t, t, s);
  auto root = h.root(u);
  std::cout << "Consumed: " << h.used() << std::endl;
  h.GC(0);
  std::cout << "Consumed: " << h.used() << std::endl;
  std::cout << static_cast<String*>(root->l->value.get())->c_str() << std::endl;
  return 0;
}
*/
