/* Wake shared-library shim to capture inputs/outputs
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

#define _GNU_SOURCE
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>

#ifndef O_TMPFILE
#define O_TMPFILE 0
#endif

#ifdef __APPLE__
#define PREFIX(fn) my_##fn
#define FORWARD(fn) orig = fn
struct interpose { const void* my_fn; const void* orig_fn; };
#define INTERPOSE(fn) \
  const struct interpose __attribute__ ((section ("__DATA, __interpose"))) interpose_##fn = \
  { (const void*)&my_##fn, (const void*)&fn };
#else
#define PREFIX(fn) fn
#define FORWARD(fn) if (!orig) orig = dlsym(RTLD_NEXT, #fn)
#define INTERPOSE(fn)
#endif

static void unlink_guard(const char *filename) {
  char prefix[] = ".guard-";

  int last = 0, len;
  for (len = 0; filename[len]; ++len)
    if (filename[len] == '/')
      last = len + 1;

  char buf[len + sizeof(prefix)];
  memcpy(&buf[0], filename, last);
  memcpy(&buf[last], prefix, sizeof(prefix)-1);
  memcpy(&buf[last+sizeof(prefix)-1], filename+last, 1+len-last);

  unlink(&buf[0]);
}

#define OPEN(fn)						\
int PREFIX(fn)(const char *filename, int flags, ...) {		\
  static int (*orig)(const char *, int, ...);			\
  FORWARD(fn);							\
  unlink_guard(filename);					\
  if ((flags & (O_CREAT|O_TMPFILE))) {				\
    va_list ap;							\
    mode_t mode;						\
    va_start(ap, flags);					\
    mode = va_arg(ap, int);					\
    va_end(ap);							\
    return orig(filename, flags, mode);				\
  } else {							\
    return orig(filename, flags);				\
  }								\
}								\
INTERPOSE(fn)

OPEN(open)
#ifndef __APPLE__
OPEN(open64)
OPEN(__open)
OPEN(__open64)
#endif

#define OPEN2(fn)						\
int PREFIX(fn)(const char *filename, int flags) {		\
  static int (*orig)(const char *, int);			\
  FORWARD(fn);							\
  unlink_guard(filename);					\
  return orig(filename, flags);					\
}								\
INTERPOSE(fn)

#ifndef __APPLE__
OPEN2(__open_2)
OPEN2(__open64_2)
#endif

#define OPENAT(fn)						\
int PREFIX(fn)(int dirfd, const char *filename, int flags, ...) {\
  static int (*orig)(int dirfd, const char *, int, ...);	\
  FORWARD(fn);							\
  unlink_guard(filename);					\
  if ((flags & (O_CREAT|O_TMPFILE))) {				\
    va_list ap;							\
    mode_t mode;						\
    va_start(ap, flags);					\
    mode = va_arg(ap, int);					\
    va_end(ap);							\
    return orig(dirfd, filename, flags, mode);			\
  } else {							\
    return orig(dirfd, filename, flags);			\
  }								\
}								\
INTERPOSE(fn)

OPENAT(openat)
#ifndef __APPLE__
OPENAT(openat64)
#endif

#define OPENAT2(fn)						\
int PREFIX(fn)(int dirfd, const char *filename, int flags) {	\
  static int (*orig)(int dirfd, const char *, int);		\
  FORWARD(fn);							\
  unlink_guard(filename);					\
  return orig(dirfd, filename, flags);				\
}								\
INTERPOSE(fn)

#ifndef __APPLE__
OPENAT2(__openat_2)
OPENAT2(__openat64_2)
#endif

#define CREAT(fn)						\
int PREFIX(fn)(const char *filename, mode_t mode) {		\
  static int (*orig)(const char *, mode_t);			\
  FORWARD(fn);							\
  unlink_guard(filename);					\
  return orig(filename, mode);					\
}								\
INTERPOSE(fn)

CREAT(creat)
#ifndef __APPLE__
CREAT(creat64)
#endif

#define FOPEN(fn)						\
FILE *PREFIX(fn)(const char *filename, const char *mode) {	\
  static FILE *(*orig)(const char *, const char *);		\
  FORWARD(fn);							\
  unlink_guard(filename);					\
  return orig(filename, mode);					\
}								\
INTERPOSE(fn)

FOPEN(fopen)
#ifndef __APPLE__
FOPEN(fopen64)
#endif

#define FREOPEN(fn)						\
FILE *PREFIX(fn)(const char *filename, const char *mode, FILE *s) {\
  static FILE *(*orig)(const char *, const char *, FILE *);	\
  FORWARD(fn);							\
  unlink_guard(filename);					\
  return orig(filename, mode, s);				\
}								\
INTERPOSE(fn)

FREOPEN(freopen)
#ifndef __APPLE__
FREOPEN(freopen64)
#endif

int PREFIX(execv)(const char *path, char *const argv[]) {
  static int (*orig)(const char *, char *const []);
  FORWARD(execv);
  unlink_guard(path);
  return orig(path, argv);
}
INTERPOSE(execv)

static int check_exec(const char *prefix, int plen, const char *file, int flen) {
  char path[plen+flen+2];
  memcpy(&path[0], prefix, plen);
  memcpy(&path[plen+1], file, flen);
  path[plen] = '/';
  path[plen+flen+1] = 0;
  return access(&path[0], X_OK) == 0;
}

static int search(const char **prefix, const char *file, int flen) {
  if (!*prefix) *prefix = ".:/bin:/usr/bin";
  if (strchr(file, '/')) return 0;

  const char *tok = *prefix;
  const char *end = tok + strlen(tok);
  for (const char *scan = tok; scan != end; ++scan) {
    if (*scan == ':') {
      if (scan != tok && check_exec(tok, scan-tok, file, flen)) {
        *prefix = tok;
        return scan-tok;
      }
      tok = scan+1;
    }
  }

  if (end != tok && check_exec(tok, end-tok, file, flen)) {
    *prefix = tok;
    return end-tok;
  } else {
    return 0;
  }
}

int PREFIX(execvp)(const char *file, char *const argv[]) {
  const char *prefix = getenv("PATH");
  int flen = strlen(file);
  int plen = search(&prefix, file, flen);
  if (plen > 0) {
    char path[plen+flen+2];
    memcpy(&path[0], prefix, plen);
    memcpy(&path[plen+1], file, flen);
    path[plen] = '/';
    path[plen+flen+1] = 0;
    return PREFIX(execv)(path, argv);
  } else {
    return PREFIX(execv)(file, argv);
  }
}
INTERPOSE(execvp)

int PREFIX(execve)(const char *filename, char *const argv[], char *const envp[]) {
  static int (*orig)(const char *, char *const [], char *const []);
  FORWARD(execve);
  unlink_guard(filename);
  return orig(filename, argv, envp);
}
INTERPOSE(execve)

#ifdef __GNU__
int PREFIX(execvpe)(const char *file, char *const argv[], char *const envp[]) {
  const char *prefix = getenv("PATH");
  int flen = strlen(file);
  int plen = search(&prefix, file, flen);
  if (plen > 0) {
    char path[plen+flen+2];
    memcpy(&path[0], prefix, plen);
    memcpy(&path[plen+1], file, flen);
    path[plen] = '/';
    path[plen+flen+1] = 0;
    return PREFIX(execve)(path, argv, envp);
  } else {
    return PREFIX(execve)(file, argv, envp);
  }
}
INTERPOSE(execve)
#endif

int PREFIX(execl)(const char *path, const char *arg, ... /* (char  *) NULL */) {
  int nargs;
  va_list ap, ap2;

  va_start(ap, arg);
  va_copy(ap2, ap);
  for (nargs = 1; va_arg(ap, char*); ++nargs) { }
  va_end(ap);

  char *argv[nargs+1];

  argv[0] = (char*)arg;
  for (nargs = 1; (argv[nargs] = va_arg(ap2, char*)); ++nargs) { }
  va_end(ap2);

  return PREFIX(execv)(path, argv);
}
INTERPOSE(execl)

int PREFIX(execlp)(const char *file, const char *arg, ... /* (char  *) NULL */) {
  int nargs;
  va_list ap, ap2;

  va_start(ap, arg);
  va_copy(ap2, ap);
  for (nargs = 1; va_arg(ap, char*); ++nargs) { }
  va_end(ap);

  char *argv[nargs+1];

  argv[0] = (char*)arg;
  for (nargs = 1; (argv[nargs] = va_arg(ap2, char*)); ++nargs) { }
  va_end(ap2);

  return PREFIX(execvp)(file, argv);
}
INTERPOSE(execlp)

int PREFIX(execle)(const char *path, const char *arg, ... /*, (char *) NULL, char * const envp[] */) {
  int nargs;
  va_list ap, ap2;

  va_start(ap, arg);
  va_copy(ap2, ap);
  for (nargs = 1; va_arg(ap, char*); ++nargs) { }
  va_end(ap);

  char *argv[nargs+1];

  argv[0] = (char*)arg;
  for (nargs = 1; (argv[nargs] = va_arg(ap2, char*)); ++nargs) { }
  char * const * envp = va_arg(ap2, char * const *);
  va_end(ap2);

  return PREFIX(execve)(path, argv, envp);
}
INTERPOSE(execle)

struct dirent *PREFIX(readdir)(DIR *dirp) {
  static struct dirent *(*orig)(DIR *);
  FORWARD(readdir);
  struct dirent *out;
  do out = orig(dirp);
  while (out &&
    out->d_name[0] == '.' && out->d_name[1] == 'g' &&
    out->d_name[2] == 'u' && out->d_name[3] == 'a' &&
    out->d_name[4] == 'r' && out->d_name[5] == 'd' &&
    out->d_name[6] == '-');
  return out;
}
INTERPOSE(readdir)

int PREFIX(readdir_r)(DIR *dirp, struct dirent *entry, struct dirent **result) {
  static int (*orig)(DIR *, struct dirent *, struct dirent **);
  FORWARD(readdir_r);
  int out;
  do out = orig(dirp, entry, result);
  while (*result &&
    entry->d_name[0] == '.' && entry->d_name[1] == 'g' &&
    entry->d_name[2] == 'u' && entry->d_name[3] == 'a' &&
    entry->d_name[4] == 'r' && entry->d_name[5] == 'd' &&
    entry->d_name[6] == '-');
  return out;
}
INTERPOSE(readdir_r)
