#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <iostream>


// cleanup_fifo(squashfuse_notify_pipe_path, squashfuse_notify_pipe_fd);
void cleanup_fifo(char *squashfuse_notify_pipe_path, int squashfuse_notify_pipe_fd) {
  close(squashfuse_notify_pipe_fd);
  std::string rm_command = "rm " + std::string(squashfuse_notify_pipe_path);
  // std::string rm_command = "rm /scratch/erikp/wake/squashfuse-notify/notify_pipe_fifo";
  system(rm_command.c_str());
}

// Copies a string and allocates the memory
char *copy_str(char *src) {
  size_t len = strlen(src);
  char *dest = (char *) calloc(len + 1, sizeof(char));
  if (dest == NULL) {
    return NULL;
  }
  strncpy(dest, src, len);
  dest[len] = '\0';
  return dest;
}

// Works like mkfifo but it gets created in a mktemp() style.
// We create a tmpdir first in order to use mkfifoat() inside of that since mkfifo() cannot
// create a randomly named file on its own.
bool mktempfifo(char *template_str) {
  char *tempdir_template_ptr = copy_str(template_str);
  char *tempdir_template = dirname(tempdir_template_ptr);

  char *tempdir = mkdtemp(tempdir_template);
  if (!tempdir) {
    std::cerr << "mkdtemp ('" << std::string(tempdir_template) << "'): " << strerror(errno) << std::endl;
    free(tempdir_template_ptr);
    return false;
  }

  int tempdir_fd = open(tempdir, O_DIRECTORY);
  if (tempdir_fd == -1) {
    std::cerr << "open ('" << std::string(tempdir) << "'): " << strerror(errno) << std::endl;
    free(tempdir_template_ptr);
    return false;
  }

  char *basename_ptr = copy_str(template_str);
  char *fifo_filename = basename(basename_ptr);

  int mkfifoat_result = mkfifoat(tempdir_fd, fifo_filename, 0664);
  close(tempdir_fd);
  std::string squashfuse_notify_fifo_path = std::string(tempdir) + "/" + fifo_filename;

  strncpy(template_str, squashfuse_notify_fifo_path.c_str(), strlen(template_str));

  if (mkfifoat_result == -1) {
    std::cerr << "mkfifoat '" << squashfuse_notify_fifo_path << "': " << strerror(errno) << std::endl;
    free(tempdir_template_ptr);
    free(basename_ptr);
    return false;
  }

  free(tempdir_template_ptr);
  free(basename_ptr);
  return true;
}

bool wait_for_squashfuse_mount(char *squashfuse_notify_pipe_path) {
  // Open the fd
  int squashfuse_notify_pipe_fd = open(squashfuse_notify_pipe_path, O_RDONLY | O_NONBLOCK);
  if (squashfuse_notify_pipe_fd == -1) {
    std::cerr << "open ('" << std::string(squashfuse_notify_pipe_path) << "'): " << strerror(errno) << std::endl;
    return false;
  }

  // Now wait for the notify pipe to respond
  struct pollfd poll_squashfuse_notify_fifo;
  int poll_timeout = 10000;
  nfds_t num_fds = 1;
  poll_squashfuse_notify_fifo.fd = squashfuse_notify_pipe_fd;
  poll_squashfuse_notify_fifo.events = POLLIN;

  int poll_result = poll(&poll_squashfuse_notify_fifo, num_fds, poll_timeout);
  if (poll_result == 0) {
    std::cerr << "poll '" << squashfuse_notify_pipe_path << "': Timed out after " << poll_timeout << " ms" << std::endl;
    cleanup_fifo(squashfuse_notify_pipe_path, squashfuse_notify_pipe_fd);
    return false;
  }
  else if (!poll_result) {
    std::cerr << "poll '" << squashfuse_notify_pipe_path << "': " << strerror(errno) << std::endl;
    cleanup_fifo(squashfuse_notify_pipe_path, squashfuse_notify_pipe_fd);
    return false;
  }

  char squashfuse_notify_buffer[2] = { '\0' };
  ssize_t bytesRead = read(squashfuse_notify_pipe_fd, squashfuse_notify_buffer, 1);
  if (bytesRead == -1) {
    std::cerr << "Error reading squashfuse fifo notify_pipe" << std::endl;
    cleanup_fifo(squashfuse_notify_pipe_path, squashfuse_notify_pipe_fd);
    return false;
  } else if (bytesRead == 0) {
    std::cerr << "Error: Zero bytes were read from squashfuse fifo notify_pipe" << std::endl;
    cleanup_fifo(squashfuse_notify_pipe_path, squashfuse_notify_pipe_fd);
    return false;
  }

  if (squashfuse_notify_buffer[0] == 'f') {
    std::cerr << "Failure: squashfuse fifo notify_pipe returned 'f'" << std::endl;
    return false;
  }
  
  cleanup_fifo(squashfuse_notify_pipe_path, squashfuse_notify_pipe_fd);
  return true;
}
