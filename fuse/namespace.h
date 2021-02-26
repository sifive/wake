/* Linux namespace and mount ops for fuse-wake
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

#ifdef __linux__

#include <string>
struct JAST;

bool setup_user_namespaces(const JAST& jast);
bool do_mounts_from_json(const JAST& jast, const std::string& fuse_mount_path);
bool get_workspace_dir(const JAST& jast, const std::string& host_workspace_dir, std::string& out);

#endif

#endif
