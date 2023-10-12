/*
 * Copyright 2022 SiFive, Inc.
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

#pragma once

// Open Group Base Specifications Issue 7
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <iostream>
#include <memory>
#include <string>
#include <thread>

namespace job_cache {

struct EvictionPolicy {
  virtual void init(const std::string& cache_dir) = 0;
  virtual void read(int id) = 0;
  virtual void write(int id) = 0;
  virtual ~EvictionPolicy() {}
};

struct NilEvictionPolicy : EvictionPolicy {
  virtual void init(const std::string& cache_dir) override {
    std::cerr << "NilEvictionPolicy::init()" << std::endl;
  }

  virtual void read(int id) override {
    std::cerr << "NilEvictionPolicy::read(" << id << ")" << std::endl;
  }

  virtual void write(int id) override {
    std::cerr << "NilEvictionPolicy::write(" << id << ")" << std::endl;
  }
};

struct LRUEvictionPolicyImpl;

class LRUEvictionPolicy : public EvictionPolicy {
  // We need to touch the database so we use pimpl to hide the implementation
  std::unique_ptr<LRUEvictionPolicyImpl> impl;
  uint64_t max_cache_size;
  uint64_t low_cache_size;
  std::thread gc_thread;

 public:
  explicit LRUEvictionPolicy(uint64_t max_cache_size, uint64_t low_cache_size);
  LRUEvictionPolicy() = delete;
  LRUEvictionPolicy(const LRUEvictionPolicy&) = delete;
  LRUEvictionPolicy(LRUEvictionPolicy&&) = delete;
  virtual ~LRUEvictionPolicy();

  virtual void init(const std::string& cache_dir) override;

  virtual void read(int id) override;

  virtual void write(int id) override;
};

struct TTLEvictionPolicyImpl;

class TTLEvictionPolicy : public EvictionPolicy {
  // We need to touch the database so we use pimpl to hide the implementation
  std::unique_ptr<TTLEvictionPolicyImpl> impl;
  uint64_t low_cache_size;
  uint64_t seconds_to_live;
  std::thread gc_thread;

 public:
  explicit TTLEvictionPolicy(uint64_t seconds_to_live);
  TTLEvictionPolicy() = delete;
  TTLEvictionPolicy(const TTLEvictionPolicy&) = delete;
  TTLEvictionPolicy(TTLEvictionPolicy&&) = delete;
  virtual ~TTLEvictionPolicy();

  virtual void init(const std::string& cache_dir) override;

  virtual void read(int id) override;

  virtual void write(int id) override;

    
};

int eviction_loop(const std::string& cache_dir, std::unique_ptr<EvictionPolicy> policy);

}  // namespace job_cache
