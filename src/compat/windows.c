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

#if defined(_WIN32)

int is_windows() { return 1; }

#elif defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>

int is_windows() {
  return EM_ASM_INT({
    if (process.platform == 'win32') {
      return 1;
    } else {
      return 0;
    }
  });
}

#else

int is_windows() { return 0; }

#endif
