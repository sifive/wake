/*
 * Copyright 2023 SiFive, Inc.
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

// Open Group Base Specifications Issue 7
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

/* Wake vfork exec shim */
#include <errno.h>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <iostream>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <future>
#include <thread>

#include "wcl/xoshiro_256.h"
#include "blake2/blake2.h"
#include "compat/nofollow.h"

// Can increase to 64 if needed
#define HASH_BYTES 32

static inline uint8_t hex_to_nibble(char hex) {
  if (hex >= '0' && hex <= '9') return hex - '0';
  if (hex >= 'a' && hex <= 'f') return hex - 'a' + 10;
  if (hex >= 'A' && hex <= 'F') return hex - 'A' + 10;
  // This case is invalid but I suspect it will blow
  // up the quickest if violated.
  return 0xFF;
}

template <size_t size>
static inline void get_hex_data(const std::string& s, uint8_t (*data)[size]) {
  uint8_t* start = *data;
  const uint8_t* end = start + size;
  for (size_t i = 0; start < end && i < s.size(); i += 2) {
    // Bytes are in little endian but nibbles are in big endian...
    // I could put the entire number in big endian but that's extremely
    // frustrating to work with...but also if the nibbles aren't in big
    // endian it gets really confusing to look at certain things when you
    // use the pure-little endian to_hex. So to make to_hex and get_hex_data
    // match and to make to_hex results easy to read, we do this.
    start[0] = (hex_to_nibble(s[i]) << 4) & 0xF0;
    if (i + 1 < s.size()) start[0] |= hex_to_nibble(s[i + 1]) & 0xF;
    ++start;
  }
}

struct Hash256 {
  uint64_t data[4] = {0};

  Hash256() {
    data[0] = 0;
    data[1] = 0;
    data[2] = 0;
    data[3] = 0;
  }
  Hash256(const Hash256& other) {
    data[0] = other.data[0];
    data[1] = other.data[1];
    data[2] = other.data[2];
    data[3] = other.data[3];
  }

  static Hash256 from_hex(const std::string& hash) {
    assert(hash.size() == 64);
    Hash256 out;
    get_hex_data(hash, reinterpret_cast<uint8_t(*)[32]>(&out.data));
    return out;
  }

  static Hash256 from_hash(uint8_t (*data)[32]) {
    Hash256 out;
    uint64_t(&data64)[4] = *reinterpret_cast<uint64_t(*)[4]>(data);
    out.data[0] = data64[0];
    out.data[1] = data64[1];
    out.data[2] = data64[2];
    out.data[3] = data64[3];
    return out;
  }

  std::string to_hex() const { return wcl::to_hex(&data); }

  bool operator==(Hash256 other) {
    return data[0] == other.data[0] && data[1] == other.data[1] && data[2] == other.data[2] &&
           data[3] == other.data[3];
  }

  bool operator!=(Hash256 other) { return !(*this == other); }
};

static Hash256 hash_dir() {
  return Hash256();
}

static Hash256 hash_link(const char *link) {
  blake2b_state S;
  uint8_t hash[HASH_BYTES];
  std::vector<char> buffer(8192, 0);

  while (true) {
    int bytes_read = readlink(link, buffer.data(), buffer.size());
    if (bytes_read < 0) {
      std::cerr << "wake-hash: readlink(" << link << "): " << strerror(errno) << std::endl;
      exit(1);
    }
    if (static_cast<size_t>(bytes_read) != buffer.size()) break;
    buffer.resize(2 * buffer.size(), 0);
  }

  blake2b_init(&S, sizeof(hash));
  blake2b_update(&S, reinterpret_cast<uint8_t*>(buffer.data()), buffer.size());
  blake2b_final(&S, hash, sizeof(hash));

  return Hash256::from_hash(&hash);
}

static Hash256 hash_file(const char *file, int fd) {
  blake2b_state S;
  uint8_t hash[HASH_BYTES], buffer[8192];
  ssize_t got;

  blake2b_init(&S, sizeof(hash));
  while ((got = read(fd, &buffer[0], sizeof(buffer))) > 0) blake2b_update(&S, &buffer[0], got);
  blake2b_final(&S, &hash[0], sizeof(hash));

  if (got < 0) {
    std::cerr << "wake-hash read(" << file << "): " << strerror(errno) << std::endl;
    exit(1);
  }

  return Hash256::from_hash(&hash);
}

static Hash256 do_hash(const char *file) {
  struct stat stat;
  int fd = open(file, O_RDONLY | O_NOFOLLOW);

  if (fd == -1) {
    if (errno == EISDIR) return hash_dir();
    if (errno == ELOOP || errno == EMLINK) return hash_link(file);
    std::cerr << "wake-hash open(" << file << "): " << strerror(errno);
    exit(1);
  }

  if (fstat(fd, &stat) != 0) {
    if (errno == EISDIR) return hash_dir();
    std::cerr << "wake-hash fstat(" << file << "): " << strerror(errno);
    exit(1);
  }

  if (S_ISDIR(stat.st_mode)) return hash_dir();
  if (S_ISLNK(stat.st_mode)) return hash_link(file);

  return hash_file(file, fd);
}

int main(int argc, char **argv) {
  // Find all the files we want to hash
  std::vector<const char*> files_to_hash;
  for (int i = 1; i < argc; ++i) {
    files_to_hash.push_back(argv[i]);
  }

  // Now hash them in parallel
  size_t num_vcores = std::thread::hardware_concurrency();
  size_t threads = 2 * num_vcores; // Unclear what the optimal number is, we can play with it
  size_t files_per_thread = std::max(16UL, files_to_hash.size() / threads + !!(files_to_hash.size() % threads));
  std::vector<std::future<std::vector<Hash256>>> to_join;
  for (size_t start_index = 0; start_index < files_to_hash.size(); start_index += files_per_thread) {
    to_join.emplace_back(std::async([&files_to_hash, files_per_thread, start_index]() {
      std::vector<Hash256> out;
      for (size_t i = 0; i < files_per_thread; ++i) {
        if (start_index + i >= files_to_hash.size()) break;
        out.emplace_back(do_hash(files_to_hash[start_index + i]));
      }
      return out;
    }));
  }

  // Now join them outputting the hashes in the same order we received them
  for (auto& fut : to_join) {
    fut.wait();
    std::vector<Hash256> result = fut.get(); // NOTE: This moves so we cannot call get again
    for (auto& hash : result) {
      std::cout << hash.to_hex() << std::endl;
    }
  }

  return 0;
}
