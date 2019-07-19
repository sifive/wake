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

#ifndef GC_H
#define GC_H

#include <memory>
#include <stdint.h>

#if __GNUC__ < 4 || (__GNUC__ == 4 && __GNUC_MINOR__ < 8)
#define alignas(x)
#endif

struct Heap;
struct HeapObject;
struct PadObject;
template <typename T> struct HeapPointer;
template <typename T> struct RootPointer;

struct Placement {
  HeapObject *obj;
  PadObject *free;
  Placement(HeapObject *obj_, void *free_) : obj(obj_), free(static_cast<PadObject*>(free_)) { }
};

struct HeapObject {
  virtual Placement moveto(PadObject *free) = 0;
  virtual Placement descend(PadObject *free) = 0;
  virtual ~HeapObject();

  // this overload causes non-placement 'new' to become illegal (which we want)
  void *operator new(size_t size, void *free) { return free; }
};

struct RootRing {
  HeapObject *root;
  RootRing *prev;
  RootRing *next;

  explicit RootRing(HeapObject *root_ = nullptr) : root(root_), prev(this), next(this) { }

  RootRing(const RootRing&) = delete;
  RootRing & operator = (const RootRing&) = delete;

  RootRing(RootRing &&x) {
    root = x.root;
    x.root = nullptr;
    if (x.next == &x) {
      prev = this;
      next = this;
    } else {
      prev = x.prev;
      next = x.next;
      prev->next = this;
      next->prev = this;
      x.prev = &x;
      x.next = &x;
    }
  }

  RootRing(RootRing &x, HeapObject *root_) : root(root_), prev(&x), next(x.next) {
    next->prev = this;
    x.next = this;
  }

  ~RootRing() {
    prev->next = next;
    next->prev = prev;
  }
};

template <typename T>
struct RootPointer {
  // construct using heap.root(ptr)
  RootPointer(RootRing &o, HeapObject *obj) : ring(o, obj) { }

  explicit operator bool() const { return ring.root; }
  void reset() { ring.root = nullptr; }

  T *get() const { return static_cast<T*>(ring.root); }
  T * operator -> () const { return get(); }

  template <typename Y>
  RootPointer & operator = (HeapPointer<Y> x);
  template <typename Y>
  RootPointer & operator = (const RootPointer<Y> &x) { ring.root = static_cast<T*>(x.get()); return *this; }
  RootPointer & operator = (T *x) { ring.root = x; return *this; }

private:
  RootRing ring;
};

template <typename T>
struct HeapPointer {
  template <typename Y>
  HeapPointer(HeapPointer<Y> x) : obj(static_cast<T*>(x.get())) { }
  template <typename Y>
  HeapPointer(const RootPointer<Y> &x) : obj(static_cast<T*>(x.get())) { }
  HeapPointer(T *x = nullptr) : obj(x) { }

  explicit operator bool() const { return obj; }
  void reset() { obj = nullptr; }

  T *get() const { return static_cast<T*>(obj); }
  T * operator -> () const { return get(); }

  template <typename Y>
  HeapPointer & operator = (HeapPointer<Y> x) { obj = static_cast<T*>(x.get()); return *this; }
  template <typename Y>
  HeapPointer & operator = (const RootPointer<Y> &x) { obj = static_cast<T*>(x.get()); return *this; }
  HeapPointer & operator = (T *x) { obj = x; return *this; }

  PadObject *moveto(PadObject *free);

private:
  HeapObject *obj;
};

template <typename T>
PadObject *HeapPointer<T>::moveto(PadObject *free) {
  if (!obj) return free;
  Placement out = obj->moveto(free);
  obj = out.obj;
  return out.free;
}

template <typename T>
template <typename Y>
RootPointer<T> &RootPointer<T>::operator = (HeapPointer<Y> x) {
  ring.root = static_cast<T*>(x.get());
  return *this;
}

struct PadObject final : public HeapObject {
  Placement moveto(PadObject *free) override;
  Placement descend(PadObject *free) override;
  static PadObject *place(PadObject *free) {
    new(free) PadObject();
    return free + 1;
  }
};

struct alignas(PadObject) MovedObject final : public HeapObject {
  MovedObject(HeapObject *to_) : to(to_) { }
  HeapObject *to;
  Placement moveto(PadObject *free) override;
  Placement descend(PadObject *free) override;
};

struct GCNeededException {
  size_t needed;
  GCNeededException(size_t needed_) : needed(needed_) { }
};

struct Heap {
  Heap();
  ~Heap();

  // Call this from main loop (no pointers on stack) when GCNeededException
  void GC(size_t requested_pads);

  // Reserve enough space for a sequence of allocations
  void reserve(size_t requested_pads) {
    if (static_cast<size_t>(end - free) < requested_pads)
      throw GCNeededException(requested_pads);
  }

  // Claim the space previously prepared by 'reserve'
  PadObject *claim(size_t requested_pads) {
    PadObject *out = free;
    free += requested_pads;
    return out;
  }

  // Allocate memory for a single request
  PadObject *alloc(size_t requested_pads) {
    reserve(requested_pads);
    return claim(requested_pads);
  }

  size_t used()  const { return (free - begin) * sizeof(PadObject); }
  size_t alloc() const { return (end - begin) * sizeof(PadObject); }
  size_t avail() const { return (end - free) * sizeof(PadObject); }

  template <typename T>
  RootPointer<T> root(T *obj) { return RootPointer<T>(roots, obj); }
  template <typename T>
  RootPointer<T> root(HeapPointer<T> x) { return RootPointer<T>(roots, x.get()); }

private:
  PadObject *begin;
  PadObject *end;
  PadObject *free;
  size_t last_pads;
  RootRing roots;
};

template <typename T, typename B = HeapObject>
struct alignas(PadObject) GCObject : public B {
  typedef GCObject<T, B> Parent;
  typedef B GrandParent;

  T* self() { return static_cast<T*>(this); }
  const T* self() const { return static_cast<const T*>(this); }

  const void *data() const { return self() + 1; }
  void *data() { return self() + 1; }

  // moving constructor
  Placement moveto(PadObject *free) final override;

  // redefine this if object includes HeapPointers
  Placement descend(PadObject *free) override;

  // redefine these if 'data' extends past sizeof(T)
  PadObject *next() { return static_cast<PadObject*>(static_cast<HeapObject*>(self() + 1)); }
  template <typename ... ARGS>
  static size_t reserve(ARGS&&... args);
  template <typename ... ARGS>
  static T *claim(Heap &h, ARGS&&... args); // require prior h.reserve
  template <typename ... ARGS>
  static T *alloc(Heap &h, ARGS&&... args);
};

template <typename T, typename B>
template <typename ... ARGS>
size_t GCObject<T, B>::reserve(ARGS&&... args) {
  static_assert(sizeof(MovedObject) <= sizeof(T), "HeapObject is too small");
  return sizeof(T)/sizeof(PadObject);
}

template <typename T, typename B>
template <typename ... ARGS>
T *GCObject<T, B>::claim(Heap &h, ARGS&&... args) {
  static_assert(sizeof(MovedObject) <= sizeof(T), "HeapObject is too small");
  return new (h.claim(sizeof(T)/sizeof(PadObject))) T { std::forward<ARGS>(args) ... };
}

template <typename T, typename B>
template <typename ... ARGS>
T *GCObject<T, B>::alloc(Heap &h, ARGS&&... args) {
  static_assert(sizeof(MovedObject) <= sizeof(T), "HeapObject is too small");
  return new (h.alloc(sizeof(T)/sizeof(PadObject))) T { std::forward<ARGS>(args) ... };
}

template <typename T, typename B>
Placement GCObject<T, B>::moveto(PadObject *free) {
  if (alignof(T) > alignof(PadObject))
    while (((uintptr_t)free & (alignof(T)-1)))
      free = PadObject::place(free);
  T *from = self();
  T *to = new(free) T(std::move(*from));
  from->~T();
  new(from) MovedObject(to);
  return Placement(to, to->next());
}

template <typename T, typename B>
Placement GCObject<T, B>::descend(PadObject *free) {
  return Placement(self()->next(), free);
}

#endif
