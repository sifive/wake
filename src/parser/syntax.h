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

#include <stdio.h>

class FileContent;
class DiagnosticReporter;
class CSTBuilder;

struct ParseInfo {
  FileContent *fcontent;
  CSTBuilder *cst;
  DiagnosticReporter *reporter;

  ParseInfo(FileContent *fcontent_, CSTBuilder *cst_, DiagnosticReporter *reporter_)
      : fcontent(fcontent_), cst(cst_), reporter(reporter_) {}
};

const char *symbolExample(int symbol);
const char *symbolName(int symbol);

void *ParseAlloc(void *(*mallocProc)(size_t));
void ParseFree(void *p, void (*freeProc)(void *));
void Parse(void *p, int yymajor, struct StringSegment yyminor, ParseInfo pi);
void ParseTrace(FILE *TraceFILE, char *zTracePrompt);
bool ParseShifts(void *p, int yymajor);
void parseWake(ParseInfo pi);

#endif
