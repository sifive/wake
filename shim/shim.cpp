/* Wake vfork exec shim */
#include <sys/errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

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

  execv(argv[6], argv+6);
  fprintf(stderr, "execv: %s: %s\n", argv[6], strerror(errno));
  return 127;
}
