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

/* Wake vfork exec shim */
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "blake2/blake2.h"
#include "compat/nofollow.h"

// Can increase to 64 if needed
#define HASH_BYTES 32

static int do_hash_dir() {
  for (int i = 0; i < HASH_BYTES; ++i)
    printf("00");
  printf("\n");
  return 0;
}

static int do_hash_link(const char *link) {
  blake2b_state S;
  uint8_t hash[HASH_BYTES];
  size_t len = 8192;
  char *buffer = malloc(len);

  ssize_t out;
  while (len == (out = readlink(link, buffer, len))) {
    len += len;
    buffer = realloc(buffer, len);
  }

  if (out == -1) {
     fprintf(stderr, "shim hash readlink(%s): %s\n", link, strerror(errno));
     free(buffer);
     return 1;
  }

  blake2b_init(&S, sizeof(hash));
  blake2b_update(&S, (uint8_t*)buffer, out);
  blake2b_final(&S, &hash[0], sizeof(hash));
  free(buffer);

  for (int i = 0; i < (int)sizeof(hash); ++i)
    printf("%02x", hash[i]);
  printf("\n");

  return 0;
}

static int do_hash_file(const char *file, int fd) {
  blake2b_state S;
  uint8_t hash[HASH_BYTES], buffer[8192];
  ssize_t got;

  blake2b_init(&S, sizeof(hash));
  while ((got = read(fd, &buffer[0], sizeof(buffer))) > 0)
    blake2b_update(&S, &buffer[0], got);
  blake2b_final(&S, &hash[0], sizeof(hash));

  if (got < 0) {
    fprintf(stderr, "shim hash read(%s): %s\n", file, strerror(errno));
    return 1;
  }

  for (int i = 0; i < (int)sizeof(hash); ++i)
    printf("%02x", hash[i]);
  printf("\n");

  return 0;
}

static int do_hash(const char *file) {
  struct stat stat;
  int fd;

  fd = open(file, O_RDONLY|O_NOFOLLOW);
  if (fd == -1) {
    if (errno == EISDIR) return do_hash_dir();
    if (errno == ELOOP || errno == EMLINK) return do_hash_link(file);
    fprintf(stderr, "shim hash open(%s): %s\n", file, strerror(errno));
    return 1;
  }

  if (fstat(fd, &stat) != 0) {
    if (errno == EISDIR) return do_hash_dir();
    fprintf(stderr, "shim hash fstat(%s): %s\n", file, strerror(errno));
    return 1;
  }

  if (S_ISDIR(stat.st_mode)) return do_hash_dir();
  if (S_ISLNK(stat.st_mode)) return do_hash_link(file);

  return do_hash_file(file, fd);
}

int main(int argc, char **argv) {
  int stdin_fd, stdout_fd, stderr_fd;
  const char *dir;

  if (argc < 6) return 1;

  dir = argv[4];
  if ((dir[0] != '.' || dir[1] != 0) && chdir(dir)) {
    fprintf(stderr, "chdir: %s: %s\n", dir, strerror(errno));
    return 127;
  }

  stdin_fd = open(argv[1], O_RDONLY);
  if (stdin_fd == -1) {
    fprintf(stderr, "open: %s: %s\n", argv[1], strerror(errno));
    return 127;
  }

  stdout_fd = atoi(argv[2]);
  stderr_fd = atoi(argv[3]);

  while (stdin_fd  <= 2 && stdin_fd  != 0) stdin_fd  = dup(stdin_fd);
  while (stdout_fd <= 2 && stderr_fd != 1) stdout_fd = dup(stdout_fd);
  while (stderr_fd <= 2 && stdout_fd != 2) stderr_fd = dup(stderr_fd);

  if (stdin_fd != 0) {
    dup2(stdin_fd, 0);
    close(stdin_fd);
  }

  if (stdout_fd != 1) {
    dup2(stdout_fd, 1);
    close(stdout_fd);
  }

  if (stderr_fd != 2) {
    dup2(stderr_fd, 2);
    close(stderr_fd);
  }

  if (strcmp(argv[5], "<hash>")) {
    execvp(argv[5], argv+5);
    fprintf(stderr, "execvp: %s: %s\n", argv[5], strerror(errno));
    return 127;
  } else {
    return do_hash(argv[6]);
  }
}
