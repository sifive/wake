/*
 * Copyright 2021 SiFive, Inc.
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

#ifndef JSON_CONVERTER_H
#define JSON_CONVERTER_H

#include "json/json5.h"
#include "symbol_definition.h"
#include "util/diagnostic.h"
#include "util/location.h"

namespace JSONConverter {
std::string decodePath(const std::string &fileUri);
std::string decodeScheme(const std::string &fileUri);
std::string encodePath(const std::string &filePath);

JAST createMessage();

JAST createErrorMessage(const char *code, const std::string &message);

JAST createErrorMessage(const JAST &receivedMessage, const char *code, const std::string &message);

JAST createResponseMessage(JAST receivedMessage);

JAST createRequestMessage();

Location getLocationFromJSON(JAST receivedMessage);

JAST createInitializeResultDefault(const JAST &receivedMessage);

JAST createInitializeResultInvalidSTDLib(const JAST &receivedMessage);

JAST fileDiagnosticsToJSON(const std::string &filePath,
                           const std::vector<Diagnostic> &fileDiagnostics);

JAST definitionLocationToJSON(JAST receivedMessage, const Location &definitionLocation);

JAST referencesToJSON(JAST receivedMessage, const std::vector<Location> &references);

JAST highlightsToJSON(JAST receivedMessage, const std::vector<Location> &occurrences);

JAST hoverInfoToJSON(JAST receivedMessage, const std::vector<SymbolDefinition> &hoverInfoPieces);

void appendSymbolToJSON(const SymbolDefinition &def, JAST &json);

JAST workspaceEditsToJSON(JAST receivedMessage, const std::vector<Location> &references,
                          const std::string &newName);
}  // namespace JSONConverter

#endif
