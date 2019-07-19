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
  size_t info;

  Future *at(size_t i);
  const Future *at(size_t i) const;

  BigTuple(size_t size_, size_t cons_);
  BigTuple(const BigTuple &b);

  PadObject *next();
  Placement descend(PadObject *free) override;

  size_t size() const override;
  size_t cons() const override;
  Future & operator [] (size_t i) override;
  const Future & operator [] (size_t i) const override;
};

Future *BigTuple::at(size_t i) {
  return static_cast<Future*>(data()) + i;
}

const Future *BigTuple::at(size_t i) const {
  return static_cast<const Future*>(data()) + i;
}

BigTuple::BigTuple(size_t size_, size_t cons_) : info(size_ << 16 | cons_) {
  for (size_t i = 0; i < size_; ++i)
    new (at(i)) Future();
}

BigTuple::BigTuple(const BigTuple &b) : info(b.info) {
  size_t lim = size();
  for (size_t i = 0; i < lim; ++i)
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
  return info >> 16;
}

size_t BigTuple::cons() const {
  return info & 0xFFFF;
}

Future & BigTuple::operator [] (size_t i) {
  return *at(i);
}

const Future & BigTuple::operator [] (size_t i) const {
  return *at(i);
}

template <size_t tsize, size_t tcons>
struct SmallTuple final : public GCObject<SmallTuple<tsize, tcons>, Tuple> {
  typedef GCObject<SmallTuple<tsize, tcons>, Tuple> Parent;

  Future *at(size_t i);
  const Future *at(size_t i) const;

  SmallTuple();
  SmallTuple(const SmallTuple &b);

  PadObject *next();
  Placement descend(PadObject *free) override;

  size_t size() const override;
  size_t cons() const override;
  Future & operator [] (size_t i) override;
  const Future & operator [] (size_t i) const override;
};

template <size_t tsize, size_t tcons>
Future *SmallTuple<tsize, tcons>::at(size_t i) {
  return static_cast<Future*>(Parent::data()) + i;
}

template <size_t tsize, size_t tcons>
const Future *SmallTuple<tsize, tcons>::at(size_t i) const {
  return static_cast<const Future*>(Parent::data()) + i;
}

template <size_t tsize, size_t tcons>
SmallTuple<tsize, tcons>::SmallTuple() {
  for (size_t i = 0; i < tsize; ++i)
    new (at(i)) Future();
}

template <size_t tsize, size_t tcons>
SmallTuple<tsize, tcons>::SmallTuple(const SmallTuple &b) {
  for (size_t i = 0; i < tsize; ++i)
    new (at(i)) Future(*b.at(i));
}

template <size_t tsize, size_t tcons>
PadObject *SmallTuple<tsize, tcons>::next() {
  return Parent::next() + tsize * (sizeof(Future)/sizeof(PadObject));
}

template <size_t tsize, size_t tcons>
Placement SmallTuple<tsize, tcons>::descend(PadObject *free) {
  for (size_t i = 0; i < tsize; ++i)
    free = (*this)[i].moveto(free);
  return Placement(next(), free);
}

template <size_t tsize, size_t tcons>
size_t SmallTuple<tsize, tcons>::size() const {
  return tsize;
}

template <size_t tsize, size_t tcons>
size_t SmallTuple<tsize, tcons>::cons() const {
  return tcons;
}

template <size_t tsize, size_t tcons>
Future & SmallTuple<tsize, tcons>::operator [] (size_t i) {
  return *at(i);
}

template <size_t tsize, size_t tcons>
const Future & SmallTuple<tsize, tcons>::operator [] (size_t i) const {
  return *at(i);
}

size_t Tuple::reserve(size_t size, size_t cons) {
  bool big = cons >= 4 || size == 0 || size > 4;
  if (big) {
    return sizeof(BigTuple)/sizeof(PadObject) + size * (sizeof(Future)/sizeof(PadObject));
  } else {
    return sizeof(SmallTuple<0,0>)/sizeof(PadObject) + size * (sizeof(Future)/sizeof(PadObject));
  }
}

Tuple *Tuple::claim(Heap &h, size_t size, size_t cons) {
  // size=0 must be big (otherwise it's smaller than a MovedObject)
  bool big = cons >= 4 || size == 0 || size > 4;
  if (big) {
    return new (h.claim(reserve(size,cons))) BigTuple(size, cons);
  } else {
    PadObject *dest = h.claim(reserve(size, cons));
    switch (size) {
      case 1: switch (cons) {
        case 0:  return new (dest) SmallTuple<1,0>();
        case 1:  return new (dest) SmallTuple<1,1>();
        case 2:  return new (dest) SmallTuple<1,2>();
        default: return new (dest) SmallTuple<1,3>();
      }
      case 2: switch (cons) {
        case 0:  return new (dest) SmallTuple<2,0>();
        case 1:  return new (dest) SmallTuple<2,1>();
        case 2:  return new (dest) SmallTuple<2,2>();
        default: return new (dest) SmallTuple<2,3>();
      }
      case 3: switch (cons) {
        case 0:  return new (dest) SmallTuple<3,0>();
        case 1:  return new (dest) SmallTuple<3,1>();
        case 2:  return new (dest) SmallTuple<3,2>();
        default: return new (dest) SmallTuple<3,3>();
      }
      default: switch (cons) {
        case 0:  return new (dest) SmallTuple<4,0>();
        case 1:  return new (dest) SmallTuple<4,1>();
        case 2:  return new (dest) SmallTuple<4,2>();
        default: return new (dest) SmallTuple<4,3>();
      }
    }
  }
}

Tuple *Tuple::alloc(Heap &h, size_t size, size_t cons) {
  h.reserve(reserve(size, cons));
  return claim(h, size, cons);
}
