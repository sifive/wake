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

#ifndef COLOUR_H
#define COLOUR_H

#define TERM_DEFAULT    0

#define TERM_BLACK      (8+0)
#define TERM_RED        (8+1)
#define TERM_GREEN      (8+2)
#define TERM_YELLOW     (8+3)
#define TERM_BLUE       (8+4)
#define TERM_MAGENTA    (8+5)
#define TERM_CYAN       (8+6)
#define TERM_WHITE      (8+7)

#define TERM_DIM        (16*1)
#define TERM_BRIGHT     (16*2)

const char *term_colour(int code);
const char *term_normal();

#endif
