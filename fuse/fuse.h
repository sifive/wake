/* Wake FUSE launcher to capture inputs/outputs
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

#ifndef FUSE_H
#define FUSE_H

#include <string>
#include <vector>
#include "namespace.h"

struct json_args {
	std::vector<std::string> command;
	std::vector<std::string> environment;
	std::vector<std::string> visible;
	std::string directory;
	std::string stdin_file;

	std::string hostname;
	std::string domainname;
	bool isolate_network;

	int userid;
	int groupid;

	std::vector<mount_op> mount_ops;
};

struct fuse_args : public json_args {
	std::string working_dir;
	std::string daemon_path;
	bool use_stdin_file;
};

bool json_as_struct(const std::string& json, json_args& result);

int run_in_fuse(const fuse_args& args, std::string& result_json);

#endif
