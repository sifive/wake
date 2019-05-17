#define _GNU_SOURCE
#include <stdarg.h>
#include <unistd.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#ifndef O_TMPFILE
#define O_TMPFILE 0
#endif

/*
#define DYLD_INTERPOSE(_replacment,_replacee) \
__attribute__((used)) static struct{ const void* replacment; const void* replacee; } _interpose_##_replacee \
__attribute__ ((section ("__DATA,__interpose"))) = { (const void*)(unsigned long)&_replacment, (const void*)(unsigned long)&_replacee };
*/

/*
 catopen
 execv[p][e]
 execl[pe]
 */

static void unlink_guard(const char *filename) {
  char buf[4096];
  char prefix[] = ".guard-";

  int last = 0, len;
  for (len = 0; filename[len]; ++len)
    if (filename[len] == '/')
      last = len + 1;

  if (len + sizeof(prefix) > sizeof(buf)) return;

  memcpy(buf, filename, last);
  memcpy(buf+last, prefix, sizeof(prefix)-1);
  memcpy(buf+last+sizeof(prefix)-1, filename+last, 1+len-last);
  unlink(buf);
}

#define OPEN(fn)						\
int fn(const char *filename, int flags, ...) {			\
  static int (*orig)(const char *, int, ...);			\
  if (!orig) orig = dlsym(RTLD_NEXT, #fn);			\
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
}

OPEN(open);
OPEN(open64);
OPEN(__open);
OPEN(__open64);
OPEN(__open_2);
OPEN(__open64_2);

#define OPENAT(fn)						\
int fn(int dirfd, const char *filename, int flags, ...) {	\
  static int (*orig)(int dirfd, const char *, int, ...);	\
  if (!orig) orig = dlsym(RTLD_NEXT, #fn);			\
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
}

OPENAT(openat);
OPENAT(openat64);
OPENAT(__openat_2);
OPENAT(__openat64_2);

#define CREAT(fn)						\
int fn(const char *filename, mode_t mode) {			\
  static int (*orig)(const char *, mode_t);			\
  if (!orig) orig = dlsym(RTLD_NEXT, #fn);			\
  unlink_guard(filename);					\
  return orig(filename, mode);					\
}

CREAT(creat);
CREAT(creat64);

#define FOPEN(fn)						\
FILE *fn(const char *filename, const char *mode) {		\
  static FILE *(*orig)(const char *, const char *);		\
  if (!orig) orig = dlsym(RTLD_NEXT, #fn);			\
  unlink_guard(filename);					\
  return orig(filename, mode);					\
}

FOPEN(fopen);
FOPEN(fopen64);

#define FREOPEN(fn)						\
FILE *fn(const char *filename, const char *mode, FILE *s) {	\
  static FILE *(*orig)(const char *, const char *, FILE *);	\
  if (!orig) orig = dlsym(RTLD_NEXT, #fn);			\
  unlink_guard(filename);					\
  return orig(filename, mode, s);				\
}

FREOPEN(freopen);
FREOPEN(freopen64);
