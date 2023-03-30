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

struct EvictionPolicy {
  virtual void init() = 0;
  virtual void read(int id) = 0;
  virtual void write(int id) = 0;
  virtual ~EvictionPolicy() {}
};

struct NilEvictionPolicy : EvictionPolicy {
  virtual void init() override { std::cerr << "NilEvictionPolicy::init()" << std::endl; }

  virtual void read(int id) override {
    std::cerr << "NilEvictionPolicy::read(" << id << ")" << std::endl;
  }

  virtual void write(int id) override {
    std::cerr << "NilEvictionPolicy::write(" << id << ")" << std::endl;
  }
};

int eviction_loop(std::unique_ptr<EvictionPolicy> policy);
