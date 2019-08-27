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

#ifndef JOB_H
#define JOB_H

#include <memory>

struct Database;
struct Runtime;

struct JobTable {
  struct detail;
  std::unique_ptr<detail> imp;

  JobTable(Database *db, double percent, bool verbose, bool quiet, bool check, bool batch);
  ~JobTable();

  // Wait for a job to complete; false -> no more active jobs
  bool wait(Runtime &runtime);
  static bool exit_now();
};

#endif
