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

#ifndef SYNTAX_H
#define SYNTAX_H

#include <string>
#include <ostream>
#include <stdint.h>
#include <stdio.h>

#include "location.h"

class FileContent;
class Reporter;

struct TokenInfo {
    const uint8_t *start;
    const uint8_t *end;

    size_t size() const { return end - start; }
    Location location(FileContent &fcontent) const;
};

std::ostream & operator << (std::ostream &os, TokenInfo token);

struct ParseInfo {
    FileContent *fcontent;
    Reporter *reporter;

    ParseInfo(FileContent *fcontent_, Reporter *reporter_)
     : fcontent(fcontent_), reporter(reporter_) { }
};

const char *symbolExample(int symbol);

void *ParseAlloc(void *(*mallocProc)(size_t));
void ParseFree(void *p, void (*freeProc)(void*));
void Parse(void *p, int yymajor, struct TokenInfo yyminor, ParseInfo pi);
void ParseTrace(FILE *TraceFILE, char *zTracePrompt);
bool ParseShifts(void *p, int yymajor);

#endif
