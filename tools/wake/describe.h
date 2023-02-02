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

#ifndef DESCRIBE
#define DESCRIBE

#include <vector>

#include "json/json5.h"
#include "runtime/database.h"

struct DescribePolicy {
  enum type { TAG_URI, SCRIPT, HUMAN, METADATA, DEBUG, VERBOSE } type;
  union {
    const char *tag_uri;
  };

  static DescribePolicy tag_url(const char *tag_uri) {
    DescribePolicy policy;
    policy.type = TAG_URI;
    policy.tag_uri = tag_uri;
    return policy;
  }

  static DescribePolicy script() {
    DescribePolicy policy;
    policy.type = SCRIPT;
    return policy;
  }

  static DescribePolicy human() {
    DescribePolicy policy;
    policy.type = HUMAN;
    return policy;
  }

  static DescribePolicy metadata() {
    DescribePolicy policy;
    policy.type = METADATA;
    return policy;
  }

  static DescribePolicy debug() {
    DescribePolicy policy;
    policy.type = DEBUG;
    return policy;
  }

  static DescribePolicy verbose() {
    DescribePolicy policy;
    policy.type = VERBOSE;
    return policy;
  }
};

void describe(const std::vector<JobReflection> &jobs, DescribePolicy policy);
JAST create_tagdag(Database &db, const std::string &tag);

#endif
