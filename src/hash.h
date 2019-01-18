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

#define HASH(key, len, ignore, hash) siphash(key, len, &hash.data[0]);

#endif
