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

#ifndef HASH_H
#define HASH_H

#include <vector>
#include <string>
#include <cstdint>
#include <cstring>

extern uint64_t sip_key[2];
int siphash(const void *in, unsigned long inlen, uint64_t *out);

struct Hash {
  uint64_t data[2];

  Hash() : data{0,0} { }
  Hash(const void *in, unsigned long inlen) { siphash(in, inlen, &data[0]); }
  Hash(const std::vector<uint64_t> &out) { siphash(out.data(), out.size()*sizeof(uint64_t), &data[0]); }
  Hash(const std::string &str) { siphash(str.data(), str.size(), &data[0]); }

  void push(std::vector<uint64_t> &out) const {
    out.push_back(data[0]);
    out.push_back(data[1]);
  }
};

static inline bool operator < (Hash x, Hash y) {
  if (x.data[0] == y.data[0]) return x.data[1] < y.data[1];
  return x.data[0] < y.data[0];
}

static inline Hash operator + (Hash a, Hash b) {
  uint64_t stuff[4];
  stuff[0] = a.data[0];
  stuff[1] = a.data[1];
  stuff[2] = b.data[0];
  stuff[3] = b.data[1];
  return Hash(&stuff[0], 32);
}

struct TypeDescriptor {
  const char *name;
  Hash hashcode; // NOTE: computed with sip_key = 0 before main()
  TypeDescriptor(const char *name_) : name(name_), hashcode(name_, std::strlen(name_)) { }
};

#endif
