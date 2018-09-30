#ifndef HASH_H
#define HASH_H

#include "MurmurHash3.h"
#include <vector>

struct Hash {
  uint64_t data[2];
  Hash() : data{0,0} { }
  void push(std::vector<uint64_t> &out) {
    out.push_back(data[0]);
    out.push_back(data[1]);
  }
  operator bool () const { return data[0] || data[1]; }
};

static inline bool operator < (const Hash &x, const Hash &y) {
  if (x.data[0] == y.data[0]) return x.data[1] < y.data[1];
  return x.data[0] < y.data[0];
}

#define HASH(key, len, seed, hash) MurmurHash3_x64_128(key, len, seed, &hash.data);

#endif
