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

#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <fcntl.h>
#include <unistd.h>

#include "execpath.h"
#include "fuse.h"
#include "json5.h"

int main(int argc, char *argv[])
{
	if (argc != 3) {
		std::cerr << "Syntax: fuse-wake <input-json> <output-json>" << std::endl;
		return 1;
	}
	const std::string input_path = argv[1];
	const std::string result_path = argv[2];

	// Read the input file
	std::ifstream ifs(input_path);
	const std::string json(
		(std::istreambuf_iterator<char>(ifs)),
		(std::istreambuf_iterator<char>()));
	if (ifs.fail()) {
		std::cerr << "read " << argv[1] << ": " << strerror(errno) << std::endl;
		return 1;
	}
	ifs.close();

	// Open the output file
	int out_fd = open(result_path.c_str(), O_WRONLY | O_CREAT | O_CLOEXEC, 0664);
	if (out_fd < 0) {
		std::cerr << "open " << result_path << ": " << strerror(errno) << std::endl;
		return 1;
	}

	fuse_args args(get_cwd(), true);
	if (!json_as_struct(json, args))
		return 1;

#ifdef __linux__
	args.command_running_dir = args.working_dir + "/" + args.directory;
#else
	// On non-linux platform like MacOS, run_in_fuse is unable to re-map the fuse
	// mountpoint over the top of the original workspace.
	// This can impact attempts to write wake build rules that are reproducable between
	// repeated runs, for if the command requests the full path to a file in the workspace
	// it may expose the temporary fuse mount as a component of the full path.
	args.command_running_dir = args.daemon.mount_subdir + "/" + args.directory;
#endif

	int retcode; // unused
	std::string result;
	// Run the command contained in the json with the fuse daemon filtering
	// the filesystem view of the workspace dir.
	// Stdin/out/err will be closed.
	if (!run_in_fuse(args, retcode, result))
		return 1;

	// Write output as json to argv[2]
	ssize_t wrote = write(out_fd, result.c_str(), result.length());
	if (wrote == -1)
		return errno;

	if (0 != close(out_fd))
		return errno;

	return 0;
}
