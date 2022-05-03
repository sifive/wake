#define _XOPEN_SOURCE 700
#define FUSE_USE_VERSION 39

#include <fuse.h>
#include <iostream>

int main() {
  std::cout << fuse_version << std::endl;
}
