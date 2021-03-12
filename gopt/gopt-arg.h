/*
 * Copyright 2021 SiFive, Inc.
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

#ifndef GOPT_ARG_H_INCLUDED
#define GOPT_ARG_H_INCLUDED

#include "gopt.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
  Search for an option by long name.
*/
struct option *arg(struct option opts[], const char *name);

#ifdef __cplusplus
}
#endif

#endif
