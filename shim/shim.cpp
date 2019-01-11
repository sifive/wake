/* Wake vfork exec shim */
#include <sys/errno.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "../src/MurmurHash3.cpp"

static int do_hash_dir() {
  printf("00000000000000000000000000000000\n");
  return 0;
}

static int do_hash(const char *file) {
  struct stat stat;
  int fd;
  uint8_t hash[16];
  void *map;

  fd = open(file, O_RDONLY);
  if (fd == -1) {
    if (errno == EISDIR) return do_hash_dir();
    perror("open");
    return 1;
  }

  if (fstat(fd, &stat) != 0) {
    if (errno == EISDIR) return do_hash_dir();
    perror("fstat");
    return 1;
  }

  if (S_ISDIR(stat.st_mode)) return do_hash_dir();

  if (stat.st_size != 0) {
    map = mmap(0, stat.st_size, PROT_READ, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
      perror("mmap");
      return 1;
    }
  }

  MurmurHash3_x64_128(map, stat.st_size, 42, &hash[0]);
  printf("%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n",
    hash[ 0], hash[ 1], hash[ 2], hash[ 3],
    hash[ 4], hash[ 5], hash[ 6], hash[ 7],
    hash[ 8], hash[ 9], hash[10], hash[11],
    hash[12], hash[13], hash[14], hash[15]);

  return 0;
}

int main(int argc, char **argv) {
  int stdin_fd, stdout_fd, stderr_fd;
  const char *root, *dir;

  if (argc < 4) return 1;

  stdin_fd = open(argv[1], O_RDONLY);
  if (stdin_fd == -1) {
    fprintf(stderr, "open: %s: %s\n", argv[1], strerror(errno));
    return 127;
  }
  dup2(stdin_fd, 0);
  close(stdin_fd);

  stdout_fd = atoi(argv[2]);
  stderr_fd = atoi(argv[3]);
  dup2(stdout_fd, 1);
  dup2(stderr_fd, 2);
  close(stdout_fd);
  close(stderr_fd);

  root = argv[4];
  if ((root[0] != '.' || root[1] != 0) && chdir(root)) {
    fprintf(stderr, "chdir: %s: %s\n", root, strerror(errno));
    return 127;
  }

  dir = argv[5];
  if ((dir[0] != '.' || dir[1] != 0) && chdir(dir)) {
    fprintf(stderr, "chdir: %s: %s\n", dir, strerror(errno));
    return 127;
  }

  if (strcmp(argv[6], "<hash>")) {
    execv(argv[6], argv+6);
    fprintf(stderr, "execv: %s: %s\n", argv[6], strerror(errno));
    return 127;
  } else {
    return do_hash(argv[7]);
  }
}
