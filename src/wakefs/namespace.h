/* Linux namespace and mount ops for wakebox
 *
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
#ifndef NAMESPACE_H
#define NAMESPACE_H

#include <string>
#include <vector>

struct mount_op {
  std::string type;
  std::string source;
  std::string destination;
  bool read_only;
};

#ifdef __linux__

struct JAST;

bool setup_user_namespaces(int id_user, int id_group, bool isolate_network,
                           const std::string &hostname, const std::string &domainname);

bool do_mounts(const std::vector<mount_op> &mount_ops, const std::string &fuse_mount_path,
               std::vector<std::string> &environments);

struct pidns_args {
  const std::vector<std::string> &command;
  const std::vector<std::string> &environment;
};

[[noreturn]] int pidns_init(void *arg);

[[noreturn]] void exec_in_pidns(pidns_args& nsargs);

#endif

#endif
