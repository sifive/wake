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

#ifndef WAKEFILES_H
#define WAKEFILES_H

#include <string>
#include <vector>

namespace re2 {
class RE2;
};

bool push_files(std::vector<std::string> &out, const std::string &path, const re2::RE2 &re,
                size_t skip, FILE *user_warning_dest);
std::string glob2regexp(const std::string &glob);
std::vector<std::string> find_all_wakefiles(bool &ok, bool workspace, bool verbose,
                                            const std::string &libdir, const std::string &workdir,
                                            FILE *user_warning_dest);

#endif
