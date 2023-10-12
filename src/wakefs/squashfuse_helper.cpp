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
#include <wcl/xoshiro_256.h>
#include <wcl/result.h>
#include <wcl/defer.h>

#include <iostream>

#include "squashfuse_helper.h"

void cleanup_fifo(std::string squashfuse_fifo_path, int squashfuse_notify_pipe_fd) {
  close(squashfuse_notify_pipe_fd);
  unlink(squashfuse_fifo_path.c_str());
}

// Works like mkfifo but it gets created in a mktemp() style.
wcl::result<std::string, wcl::posix_error_t> mktempfifo() {
  wcl::xoshiro_256 rng(wcl::xoshiro_256::get_rng_seed());
  std::string fifo_filepath = "/tmp/squashfuse_notify_pipe_fifo_" + rng.unique_name();

  int mkfifoat_result = mkfifo(fifo_filepath.c_str(), 0664);
  if (mkfifoat_result < 0) {
    return wcl::make_errno<std::string>();
  }

  return wcl::make_result<std::string, wcl::posix_error_t>(fifo_filepath);
}

wcl::optional<SquashFuseMountWaitError> wait_for_squashfuse_mount(const std::string& squashfuse_fifo_path) {
  int squashfuse_notify_pipe_fd = open(squashfuse_fifo_path.c_str(), O_RDONLY);
  if (squashfuse_notify_pipe_fd == -1) {
    return wcl::some(SquashFuseMountWaitError { SquashFuseMountWaitErrorType::CannotOpenFifo, errno });
  }

  auto defer = wcl::make_defer([&](){
    cleanup_fifo(squashfuse_fifo_path, squashfuse_notify_pipe_fd);
  });

  char squashfuse_notify_result = '\0';
  ssize_t bytesRead = read(squashfuse_notify_pipe_fd, &squashfuse_notify_result, sizeof(squashfuse_notify_result));
  if (bytesRead == -1) {
    // std::cerr << "Error reading squashfuse fifo notify_pipe" << std::endl;
    return wcl::some(SquashFuseMountWaitError { SquashFuseMountWaitErrorType::FailureToReadFifo, errno });
  } else if (bytesRead == 0) {
    // std::cerr << "Error: Zero bytes were read from squashfuse fifo notify_pipe" << std::endl;
    return wcl::some(SquashFuseMountWaitError { SquashFuseMountWaitErrorType::ReceivedZeroBytes, -1 });
  }

  if (squashfuse_notify_result == 'f') {
    // std::cerr << "Failure: squashfuse fifo notify_pipe returned 'f'" << std::endl;
    return wcl::some(SquashFuseMountWaitError { SquashFuseMountWaitErrorType::MountFailed, -1 });
  }
  
  return {};
}
