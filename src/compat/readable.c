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

// Open Group Base Specifications Issue 7
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include "readable.h"

#ifdef __EMSCRIPTEN__

#include <emscripten/emscripten.h>

// clang-format off
EM_ASYNC_JS(int, isReadable, (const char *filename, const char *uriScheme), {
  try {
    await wakeLspModule.sendRequest('accessFile', UTF8ToString(uriScheme) + UTF8ToString(filename));
    return 1;
  } catch (err) {
    return 0;
  }
});
// clang-format on

int is_readable(const char *filename, const char *uriScheme) {
  int is_node = 0;
  // clang-format off
  int res = EM_ASM_INT({
    if (!ENVIRONMENT_IS_NODE) {
      return 0;
    }

    setValue($1, 1, 'i32');
    try {
      const fs = require('fs');
      fs.accessSync(UTF8ToString($0), fs.constants.R_OK);
      return 1;
    } catch (err) {
      return 0;
    }
  }, filename, &is_node);
  // clang-format on

  if (is_node)
    return res;
  else
    return isReadable(filename, uriScheme);
}

#else

#include <unistd.h>

int is_readable(const char *filename, const char *_) { return access(filename, R_OK) == 0; }

#endif
