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

#ifndef FILE_H
#define FILE_H

#include <vector>
#include <string>
#include <stdint.h>

#include "location.h"
class DiagnosticReporter;

class FileContent {
public:
    FileContent(const char *filename_);

    std::string filename;
    const uint8_t *start;
    const uint8_t *end;

    Coordinates coordinates(const uint8_t *position) const;
    void newline(const uint8_t *first_column);

private:
    std::vector<size_t> newlines;
};

class StringFile : public FileContent {
public:
    StringFile(const char *filename_, std::string &&content_);

private:
    std::string content;
};

class ExternalFile : public FileContent {
public:
    ExternalFile(DiagnosticReporter &reporter, const char *filename_);
    ExternalFile(ExternalFile &&o);
    ~ExternalFile();
    ExternalFile &operator = (ExternalFile &&o);

    // copy construction is forbidden
    ExternalFile(const ExternalFile &) = delete;
    ExternalFile &operator = (const ExternalFile &) = delete;
};

#endif
