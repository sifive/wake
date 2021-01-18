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

#ifndef MEMBYTES_H
#define MEMBYTES_H

#if   defined(__APPLE__)
#define MEMBYTES(ru)	(ru.ru_maxrss)
#elif defined(__FreeBSD__)
#define MEMBYTES(ru)    (ru.ru_maxrss*1024)
#elif defined(__linux__)
#define MEMBYTES(ru)    (ru.ru_maxrss*1024)
#elif defined(__NetBSD__)
#define MEMBYTES(ru)    (ru.ru_maxrss*1024)
#elif defined(__OpenBSD__)
#define MEMBYTES(ru)    (ru.ru_maxrss*1024)
#elif defined(__sun)
#define MEMBYTES(ru)    (ru.ru_maxrss*getpagesize())
#else
#error Missing definition to access maxrss on this platform
#endif

#endif
