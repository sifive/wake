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

#include "tuple.h"

struct BigTuple final : public GCObject<BigTuple, Tuple> {
  size_t tsize;

  Future *at(size_t i);
  const Future *at(size_t i) const;

  BigTuple(void *meta, size_t size_);
  BigTuple(const BigTuple &b);

  PadObject *next();
  Placement descend(PadObject *free) override;

  size_t size() const override;
  Future & operator [] (size_t i) override;
  const Future & operator [] (size_t i) const override;
};

Future *BigTuple::at(size_t i) {
  return static_cast<Future*>(data()) + i;
}

const Future *BigTuple::at(size_t i) const {
  return static_cast<const Future*>(data()) + i;
}

BigTuple::BigTuple(void *meta, size_t size_) : GCObject<BigTuple, Tuple>(meta), tsize(size_) {
  for (size_t i = 0; i < size_; ++i)
    new (at(i)) Future();
}

BigTuple::BigTuple(const BigTuple &b) : GCObject<BigTuple, Tuple>(b), tsize(b.tsize) {
  for (size_t i = 0; i < tsize; ++i)
    new (at(i)) Future(*b.at(i));
}

PadObject *BigTuple::next() {
  return Parent::next() + size() * (sizeof(Future)/sizeof(PadObject));
}

Placement BigTuple::descend(PadObject *free) {
  size_t lim = size();
  for (size_t i = 0; i < lim; ++i)
    free = (*this)[i].moveto(free);
  return Placement(next(), free);
}

size_t BigTuple::size() const {
  return tsize;
}

Future & BigTuple::operator [] (size_t i) {
  return *at(i);
}

const Future & BigTuple::operator [] (size_t i) const {
  return *at(i);
}

template <size_t tsize>
struct SmallTuple final : public GCObject<SmallTuple<tsize>, Tuple> {
  typedef GCObject<SmallTuple<tsize>, Tuple> Parent;

  Future *at(size_t i);
  const Future *at(size_t i) const;

  SmallTuple(void *meta);
  SmallTuple(const SmallTuple &b);

  PadObject *next();
  Placement descend(PadObject *free) override;

  size_t size() const override;
  Future & operator [] (size_t i) override;
  const Future & operator [] (size_t i) const override;
};

template <size_t tsize>
Future *SmallTuple<tsize>::at(size_t i) {
  return static_cast<Future*>(Parent::data()) + i;
}

template <size_t tsize>
const Future *SmallTuple<tsize>::at(size_t i) const {
  return static_cast<const Future*>(Parent::data()) + i;
}

template <size_t tsize>
SmallTuple<tsize>::SmallTuple(void *meta) : GCObject<SmallTuple<tsize>, Tuple>(meta) {
  for (size_t i = 0; i < tsize; ++i)
    new (at(i)) Future();
}

template <size_t tsize>
SmallTuple<tsize>::SmallTuple(const SmallTuple &b) : GCObject<SmallTuple<tsize>, Tuple>(b) {
  for (size_t i = 0; i < tsize; ++i)
    new (at(i)) Future(*b.at(i));
}

template <size_t tsize>
PadObject *SmallTuple<tsize>::next() {
  return Parent::next() + tsize * (sizeof(Future)/sizeof(PadObject));
}

template <size_t tsize>
Placement SmallTuple<tsize>::descend(PadObject *free) {
  for (size_t i = 0; i < tsize; ++i)
    free = (*this)[i].moveto(free);
  return Placement(next(), free);
}

template <size_t tsize>
size_t SmallTuple<tsize>::size() const {
  return tsize;
}

template <size_t tsize>
Future & SmallTuple<tsize>::operator [] (size_t i) {
  return *at(i);
}

template <size_t tsize>
const Future & SmallTuple<tsize>::operator [] (size_t i) const {
  return *at(i);
}

size_t Tuple::reserve(void *meta, size_t size) {
  bool big = size > 4;
  if (big) {
    return sizeof(BigTuple)/sizeof(PadObject) + size * (sizeof(Future)/sizeof(PadObject));
  } else {
    return sizeof(SmallTuple<0>)/sizeof(PadObject) + size * (sizeof(Future)/sizeof(PadObject));
  }
}

Tuple *Tuple::claim(Heap &h, void *meta, size_t size) {
  bool big = size > 4;
  if (big) {
    return new (h.claim(reserve(meta, size))) BigTuple(meta, size);
  } else {
    PadObject *dest = h.claim(reserve(meta, size));
    switch (size) {
      case 0:  return new (dest) SmallTuple<0>(meta);
      case 1:  return new (dest) SmallTuple<1>(meta);
      case 2:  return new (dest) SmallTuple<2>(meta);
      case 3:  return new (dest) SmallTuple<3>(meta);
      default: return new (dest) SmallTuple<4>(meta);
    }
  }
}

Tuple *Tuple::alloc(Heap &h, void *meta, size_t size) {
  h.reserve(reserve(meta, size));
  return claim(h, meta, size);
}
