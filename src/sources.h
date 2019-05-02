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

#ifndef SOURCES_H
#define SOURCES_H

#include "primfn.h"
#include <memory>
#include <vector>
#include <string>

struct Value;
struct Receiver;
struct String;

bool chdir_workspace(std::string &prefix);
bool make_workspace(const std::string &dir);
std::string make_canonical(const std::string &x);

std::string find_execpath();
std::string get_cwd();
std::string get_workspace();

std::vector<std::shared_ptr<String> > find_all_sources(bool &ok, bool workspace);
std::vector<std::shared_ptr<String> > sources(const std::vector<std::shared_ptr<String> > &all, const std::string &base, const std::string &regexp);

#endif
