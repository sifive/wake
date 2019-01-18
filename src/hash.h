#ifndef HASH_H
#define HASH_H

#include <vector>
#include <cstdint>

struct Hash {
  uint64_t data[2];
  Hash() : data{0,0} { }
  void push(std::vector<uint64_t> &out) {
    out.push_back(data[0]);
    out.push_back(data[1]);
  }
};

static inline bool operator < (const Hash &x, const Hash &y) {
  if (x.data[0] == y.data[0]) return x.data[1] < y.data[1];
  return x.data[0] < y.data[0];
}

extern uint64_t sip_key[2];
int siphash(const void *in, unsigned long inlen, uint64_t *out);

static inline void hash3(const void *in, unsigned long inlen, Hash &out) {
  siphash(in, inlen, &out.data[0]);
}

static inline void rehash(const Hash &ihash, const void *extra, Hash &out) {
  uint64_t stuff[3];
  stuff[0] = (long)extra;
  stuff[1] = ihash.data[0];
  stuff[2] = ihash.data[1];
  siphash(&stuff[0], 24, &out.data[0]);
}

static inline void hash4(const void *in, unsigned long inlen, const void *extra, Hash &out) {
  uint64_t stuff[3];
  stuff[0] = (long)extra;
  siphash(in, inlen, &stuff[1]);
  siphash(&stuff[0], 24, &out.data[0]);
}

#endif
