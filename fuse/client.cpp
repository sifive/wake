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

#include "execpath.h"
#include "fuse.h"
#include "json5.h"

int main(int argc, char *argv[])
{
	if (argc != 3) {
		std::cerr << "Syntax: fuse-wake <input-json> <output-json>" << std::endl;
		return 1;
	}
	std::string input_path = argv[1];
	std::string result_path = argv[2];

	// Read the input file
	std::ifstream ifs(input_path);
	const std::string json(
		(std::istreambuf_iterator<char>(ifs)),
		(std::istreambuf_iterator<char>()));
	if (ifs.fail()) {
		std::cerr << "read " << argv[1] << ": " << strerror(errno) << std::endl;
		return 2;
	}
	ifs.close();

	const std::string daemon = find_execpath() + "/fuse-waked";
	const std::string working_dir = get_cwd();
	std::string result;
	// Run the command contained in the json with the fuse daemon filtering
	// the filesystem view of the workspace dir.
	// Stdin/out/err will be closed.
	int res = run_fuse(daemon, working_dir, json, result);

	// Write output as json to argv[2]
	std::ofstream ofs(result_path, std::ios_base::trunc);
	if (ofs.fail()) {
		// stderr was closed by run_fuse().
		return 3;
	}
	ofs << result;
	ofs.close();

	return res;
}
