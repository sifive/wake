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

#define _DEFAULT_SOURCE  // MAP_ANON is not in POSIX yet

#include "file.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <fstream>

#include "diagnostic.h"

static uint8_t null[1] = {0};

FileContent::FileContent(const char *filename_) : fname(filename_) {}

FileContent::FileContent(FileContent &&o)
    : ss(o.ss), fname(std::move(o.fname)), newlines(std::move(o.newlines)) {
  o.ss.end = o.ss.start = &null[0];
}

FileContent &FileContent::operator=(FileContent &&o) {
  ss = o.ss;
  fname = std::move(o.fname);
  newlines = std::move(o.newlines);
  o.ss.end = o.ss.start = &null[0];
  return *this;
}

void FileContent::clearNewLines() {
  newlines.clear();
  newlines.push_back(0);
}

void FileContent::addNewline(const uint8_t *first_column) {
  newlines.push_back(first_column - ss.start);
}

static int utf8_tokens(const uint8_t *s, const uint8_t *e) {
  int out = e - s;
  // Don't count any 10xx xxxx characters
  for (; s != e; ++s) out -= (*s >> 6 == 2);
  return out;
}

Coordinates FileContent::coordinates(const uint8_t *position) const {
  size_t offset = position - ss.start;
  if (newlines.empty()) {
    return Coordinates(offset, 1);
  } else {
    auto it = std::upper_bound(newlines.begin(), newlines.end(), offset);
    --it;  // always works, because newlines includes 0
    size_t row = 1 + (it - newlines.begin());
    // If position points to the first byte of a codepoint, position+1 increases col to include it
    // If position points to any other byte of a codepoint, position+1 includes an ignored '10xx
    // xxxx'
    size_t col = utf8_tokens(ss.start + *it, position + 1);
    return Coordinates(row, col);
  }
}

StringFile::StringFile(const char *filename_, std::string &&content_)
    : FileContent(filename_), content(std::move(content_)) {
  ss.start = reinterpret_cast<const uint8_t *>(content.c_str());
  ss.end = ss.start + content.size();
}

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>

// clang-format off
EM_ASYNC_JS(uint8_t *, getBase, (int *length, const char *filename, const char *uriScheme), {
  try {
    const content = await wakeLspModule.sendRequest('readFile', UTF8ToString(uriScheme) + UTF8ToString(filename));
    if (content.hasOwnProperty('message')) { // readFile request resulted in an error
      throw content;
    }
    const encoder = new TextEncoder();
    const bytes = encoder.encode(content);
    // ExternalFile takes ownership of the memory and frees it in the destructor
    const wasmPointer = Module._malloc(bytes.length + 1);
    for (let i = 0; i < bytes.length; i++) {
      Module.HEAPU8[wasmPointer + i] = bytes[i];
    }
    setValue(length, bytes.length, 'i32');
    return wasmPointer;
  } catch (err) {
    const lengthBytes = lengthBytesUTF8(err.message) + 1;
    // Same memory ownership here
    const stringOnWasmHeap = _malloc(lengthBytes);
    stringToUTF8(err.message, stringOnWasmHeap, lengthBytes);
    setValue(length, -1, 'i32');
    return stringOnWasmHeap;
  }
});

// clang-format on

ExternalFile::ExternalFile(DiagnosticReporter &reporter, const char *filename_, const char *uriScheme)
    : FileContent(filename_) {
  Location l(filename());

  int32_t length;
  uint8_t *base = getBase(&length, filename(), uriScheme);

  if (length == -1) {
    reporter.reportError(l, std::string("readFile failed; ") + reinterpret_cast<char *>(base));
    free(base);
    ss.end = ss.start = &null[0];
    return;
  }

  base[length] = 0;
  ss.start = base;
  ss.end = base + length;
}

ExternalFile::~ExternalFile() {
  if (ss.start != ss.end) free((void *)ss.start);
}

#else

ExternalFile::ExternalFile(DiagnosticReporter &reporter, const char *filename_, const char *_)
    : FileContent(filename_) {
  Location l(filename());

  int fd = open(filename(), O_RDONLY);
  if (fd == -1) {
    reporter.reportError(l, std::string("open failed; ") + strerror(errno));
    ss.end = ss.start = &null[0];
    return;
  }

  struct stat s;
  if (fstat(fd, &s) == -1) {
    reporter.reportError(l, std::string("fstat failed; ") + strerror(errno));
    close(fd);
    ss.end = ss.start = &null[0];
    return;
  }

  // There is no need for a mapping if the file is empty.
  if (s.st_size == 0) {
    close(fd);
    ss.end = ss.start = &null[0];
    return;
  }

  // Map +1 over the length (for null ptr)
  void *map = mmap(nullptr, s.st_size + 1, PROT_READ, MAP_PRIVATE, fd, 0);
  if (map == MAP_FAILED) {
    reporter.reportError(l, std::string("mmap failed; ") + strerror(errno));
    close(fd);
    ss.end = ss.start = &null[0];
    return;
  }

  // Now that the content is mapped, we don't need the file descriptor
  close(fd);
  ss.start = reinterpret_cast<const uint8_t *>(map);
  ss.end = ss.start + s.st_size;

  // We need to remap the tail of the file to ensure null termination
  long pagesize = sysconf(_SC_PAGESIZE);
  size_t tail_len = (s.st_size % pagesize) + 1;
  size_t tail_start = (s.st_size + 1) - tail_len;

  std::string buf(reinterpret_cast<const char *>(map) + tail_start, tail_len - 1);
  void *out = mmap(reinterpret_cast<uint8_t *>(map) + tail_start, pagesize, PROT_READ | PROT_WRITE,
                   MAP_FIXED | MAP_PRIVATE | MAP_ANON, -1, 0);
  if (out == MAP_FAILED || ((char *)out - (char *)map) != static_cast<ssize_t>(tail_start)) {
    reporter.reportError(l, std::string("mmap anon failed; ") + strerror(errno));
    munmap(map, s.st_size + 1);
    ss.end = ss.start = &null[0];
    return;
  }

  // Fill in the last page
  memcpy(reinterpret_cast<uint8_t *>(map) + tail_start, buf.c_str(), tail_len);
}

ExternalFile::~ExternalFile() {
  if (ss.start != ss.end)
    munmap(const_cast<void *>(reinterpret_cast<const void *>(ss.start)), ss.size() + 1);
}

#endif

CPPFile::CPPFile(const char *filename) : FileContent(filename) {}
