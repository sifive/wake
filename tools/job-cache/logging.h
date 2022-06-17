/*
 * Copyright 2022 SiFive, Inc.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctime>
#include <unistd.h>

namespace {
// This header contains useful information that you might want
// when running a deamon
 inline void log_header(FILE *file) {
  int pid = getpid();
  time_t t = time(NULL);
  struct tm tm = *localtime(&t);
  fprintf(file, "[pid=%d, %d-%02d-%02d %02d:%02d:%02d] ", pid, tm.tm_year + 1900, tm.tm_mon + 1,
          tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
}

// A generic logging function
template <class... Args>
void log_info(Args &&...args) {
  log_header(stdout);
  fprintf(stdout, args...);
  fprintf(stdout, "\n");
  fflush(stdout);
}

// A logging function for logging and then exiting with
// a failure code.
template <class... Args>
void log_fatal(Args &&...args) {
  log_header(stderr);
  fprintf(stderr, args...);
  fprintf(stderr, "\n");
  fflush(stderr);
  exit(1);
}

}
