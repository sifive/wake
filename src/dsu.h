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

#ifndef DSU_H
#define DSU_H

#include <memory>

template <typename T>
class DSU {
public:
  // Construct a new member in a new set
  DSU(T *payload_);

  // Access the payload of the set (common to all members)
  T *get();
  const T* get() const;

  T *operator ->() { return get(); }
  const T* operator ->() const { return get(); }

  // Union the two sets (this affects all members)
  // Note: other's payload will be destroyed
  void union_consume(DSU &other) const;

private:
  struct Imp {
    std::unique_ptr<T> payload;
    std::shared_ptr<Imp> parent;
    Imp(T *payload_) : payload(payload_) { }
  };
  void compress() const;
  mutable std::shared_ptr<Imp> imp;
};

template <typename T>
DSU<T>::DSU(T *payload_) : imp(std::make_shared<Imp>(payload_)) {
}

template <typename T>
void DSU<T>::compress() const {
  if (std::shared_ptr<Imp> p = std::move(imp->parent)) {
    std::shared_ptr<Imp> x = std::move(imp);
    while (std::shared_ptr<Imp> pp = std::move(p->parent)) {
      x->parent = pp;             // ++
      x = std::move(p);           // --
      p = std::move(pp);          //
    }
    x->parent = p;                // ++
    imp = std::move(p);           //
  }
}

template <typename T>
T *DSU<T>::get() {
  compress();
  return imp->payload.get();
}

template <typename T>
const T *DSU<T>::get() const {
  compress();
  return imp->payload.get();
}

template <typename T>
void DSU<T>::union_consume(DSU &other) const {
  compress();
  other.compress();
  if (imp != other.imp) {
    other.imp->payload.reset();
    other.imp->parent = imp;
  }
}

#endif
