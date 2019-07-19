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

#ifndef TUPLE_H
#define TUPLE_H

#include "runtime.h"

struct Tuple : public HeapObject {
  virtual size_t size() const = 0;
  virtual size_t cons() const = 0;
  virtual Future & operator [] (size_t i) = 0;
  const virtual Future & operator [] (size_t i) const = 0;

  static size_t reserve(size_t size, size_t cons);
  static Tuple *claim(Heap &h, size_t size, size_t cons); // requires prior h.reserve
  static Tuple *alloc(Heap &h, size_t size, size_t cons);
};

#endif
