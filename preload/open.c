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
OPEN(__open_2)
OPEN(__open64_2)
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
OPENAT(__openat_2)
OPENAT(__openat64_2)
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
