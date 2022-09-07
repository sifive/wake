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

#include "json_converter.h"

#include <map>

#include "compat/windows.h"
#include "json/json5.h"
#include "symbol_definition.h"

namespace JSONConverter {
static int parse_hex(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  } else if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  } else if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  } else {
    return -1;
  }
}

std::string decodePathOrScheme(const std::string &fileUri, bool wantPath) {
  std::string out;
  auto i = fileUri.begin(), e = fileUri.end();
  while (i != e) {
    int lo, hi;
    if (i[0] == '%' && (e - i) >= 3 && (hi = parse_hex(i[1])) != -1 &&
        (lo = parse_hex(i[2])) != -1) {
      out.push_back(hi << 4 | lo);
      i += 3;
    } else {
      out.push_back(*i);
      ++i;
    }
  }

  // skip over scheme
  size_t schemeEnd = out.find("://");
  if (schemeEnd == std::string::npos) {
    return wantPath ? out : ""; // want scheme but no "://" was encountered => scheme is empty
  }

  // skip over optional authority
  size_t root = out.find_first_of('/', schemeEnd + 3);
  if (root == std::string::npos) {
    root = schemeEnd + 3;
  } else if (is_windows()) {
    // strip leading '/' on windows
    ++root;
  }

  if (wantPath) {
    out.erase(out.begin(), out.begin() + root);  // want path => strip scheme
  } else {
    out.erase(root, std::string::npos);  // do the opposite
  }
  return out;
}

std::string decodePath(const std::string &fileUri) {
  return decodePathOrScheme(fileUri, true);
}

std::string decodeScheme(const std::string &fileUri) {
  return decodePathOrScheme(fileUri, false);
}


static char encodeTable[256][4];

static char encode_hex(int x) {
  if (x < 10) {
    return '0' + x;
  } else {
    return 'A' + (x - 10);
  }
}

static void normal(int x) {
  encodeTable[x][0] = x;
  encodeTable[x][1] = 0;
}

std::string encodePath(const std::string &filePath, const std::string &uriScheme) {
  if (!encodeTable[0][0]) {
    for (int i = 0; i < 256; ++i) {
      encodeTable[i][0] = '%';
      encodeTable[i][1] = encode_hex(i >> 4);
      encodeTable[i][2] = encode_hex(i & 15);
      encodeTable[i][3] = 0;
    }
    for (int i = 'a'; i <= 'z'; ++i) normal(i);
    for (int i = 'A'; i <= 'Z'; ++i) normal(i);
    for (int i = '0'; i <= '9'; ++i) normal(i);
    normal('-');
    normal('_');
    normal('.');
    normal('~');
    normal('/');                    // This is non-standard, but necessary
    if (is_windows()) normal(':');  // Do not escape volume names
  }

  std::string out(uriScheme);

  for (char c : filePath) out.append(encodeTable[static_cast<int>(static_cast<unsigned char>(c))]);

  return out;
}

JAST createMessage() {
  JAST message(JSON_OBJECT);
  message.add("jsonrpc", "2.0");
  return message;
}

namespace {
JAST createResponseMessage() {
  JAST message = createMessage();
  message.add("id", 0);
  return message;
}

JAST createRangeFromLocation(const Location &location) {
  JAST range(JSON_OBJECT);

  JAST &start = range.add("start", JSON_OBJECT);
  start.add("line", std::max(0, location.start.row - 1));
  start.add("character", std::max(0, location.start.column - 1));

  JAST &end = range.add("end", JSON_OBJECT);
  end.add("line", std::max(0, location.end.row - 1));
  end.add("character", std::max(0, location.end.column));  // It can be -1

  return range;
}

JAST createDiagnostic(const Diagnostic &diagnostic) {
  JAST diagnosticJSON(JSON_OBJECT);

  diagnosticJSON.children.emplace_back("range", createRangeFromLocation(diagnostic.getLocation()));
  diagnosticJSON.add("severity", diagnostic.getSeverity());
  diagnosticJSON.add("source", "wake");

  diagnosticJSON.add("message", diagnostic.getMessage());

  return diagnosticJSON;
}

JAST createDiagnosticMessage() {
  JAST message = createMessage();
  message.add("method", "textDocument/publishDiagnostics");
  return message;
}

JAST createLocationJSON(const Location &location, const std::string &uriScheme) {
  JAST locationJSON(JSON_OBJECT);
  std::string fileUri = encodePath(location.filename, uriScheme);
  locationJSON.add("uri", fileUri.c_str());
  locationJSON.children.emplace_back("range", createRangeFromLocation(location));
  return locationJSON;
}

JAST createDocumentHighlightJSON(const Location &location) {
  JAST documentHighlightJSON(JSON_OBJECT);
  documentHighlightJSON.children.emplace_back("range", createRangeFromLocation(location));
  return documentHighlightJSON;
}
}  // namespace

JAST createErrorMessage(const char *code, const std::string &message) {
  JAST errorMessage = createResponseMessage();
  JAST &error = errorMessage.add("error", JSON_OBJECT);
  error.add("code", JSON_INTEGER, code);
  error.add("message", message.c_str());
  return errorMessage;
}

JAST createErrorMessage(const JAST &receivedMessage, const char *code, const std::string &message) {
  JAST errorMessage = createResponseMessage(receivedMessage);
  JAST &error = errorMessage.add("error", JSON_OBJECT);
  error.add("code", JSON_INTEGER, code);
  error.add("message", message.c_str());
  return errorMessage;
}

JAST createResponseMessage(JAST receivedMessage) {
  JAST message = createMessage();
  message.children.emplace_back("id", receivedMessage.get("id"));
  return message;
}

JAST createRequestMessage() {
  JAST message = createMessage();
  message.add("id", 0);
  return message;
}

Location getLocationFromJSON(JAST receivedMessage) {
  std::string fileURI = receivedMessage.get("params").get("textDocument").get("uri").value;

  int row = stoi(receivedMessage.get("params").get("position").get("line").value);
  int column = stoi(receivedMessage.get("params").get("position").get("character").value);
  return {decodePath(fileURI).c_str(), Coordinates(row + 1, column + 1),
          Coordinates(row + 1, column)};
}

JAST createInitializeResultDefault(const JAST &receivedMessage) {
  JAST message = createResponseMessage(receivedMessage);
  JAST &result = message.add("result", JSON_OBJECT);

  JAST &capabilities = result.add("capabilities", JSON_OBJECT);
  capabilities.add("textDocumentSync", 1);
  capabilities.add("definitionProvider", true);
  capabilities.add("referencesProvider", true);
  capabilities.add("documentHighlightProvider", true);
  capabilities.add("hoverProvider", true);
  capabilities.add("documentSymbolProvider", true);
  capabilities.add("workspaceSymbolProvider", true);
  capabilities.add("renameProvider", true);

  JAST &serverInfo = result.add("serverInfo", JSON_OBJECT);
  serverInfo.add("name", "lsp wake server");

  return message;
}

JAST createInitializeResultInvalidSTDLib(const JAST &receivedMessage) {
  JAST message = createResponseMessage(receivedMessage);
  JAST &result = message.add("result", JSON_OBJECT);
  result.add("capabilities", JSON_OBJECT);
  JAST &serverInfo = result.add("serverInfo", JSON_OBJECT);
  serverInfo.add("name", "lsp wake server");
  return message;
}

JAST fileDiagnosticsToJSON(const std::string &filePath,
                           const std::vector<Diagnostic> &fileDiagnostics, const std::string &uriScheme) {
  JAST diagnosticsArray(JSON_ARRAY);
  for (const Diagnostic &diagnostic : fileDiagnostics) {
    diagnosticsArray.children.emplace_back(
        "", createDiagnostic(diagnostic));  // add .add for JSON_OBJECT to JSON_ARRAY
  }
  JAST message = createDiagnosticMessage();
  JAST &params = message.add("params", JSON_OBJECT);
  params.add("uri", encodePath(filePath, uriScheme));
  params.children.emplace_back("diagnostics", diagnosticsArray);
  return message;
}

JAST definitionLocationToJSON(JAST receivedMessage, const Location &definitionLocation, const std::string &uriScheme) {
  JAST message = createResponseMessage(std::move(receivedMessage));
  JAST &result = message.add("result", JSON_OBJECT);
  if (!definitionLocation.filename.empty()) {
    result = createLocationJSON(definitionLocation, uriScheme);
  }
  return message;
}

JAST referencesToJSON(JAST receivedMessage, const std::vector<Location> &references, const std::string &uriScheme) {
  JAST message = createResponseMessage(std::move(receivedMessage));
  JAST &result = message.add("result", JSON_ARRAY);
  for (const Location &location : references) {
    result.children.emplace_back("", createLocationJSON(location, uriScheme));
  }
  return message;
}

JAST highlightsToJSON(JAST receivedMessage, const std::vector<Location> &occurrences) {
  JAST message = createResponseMessage(std::move(receivedMessage));
  JAST &result = message.add("result", JSON_ARRAY);
  for (const Location &location : occurrences) {
    result.children.emplace_back("", createDocumentHighlightJSON(location));
  }
  return message;
}

JAST hoverInfoToJSON(JAST receivedMessage, const std::vector<SymbolDefinition> &hoverInfoPieces) {
  JAST message = createResponseMessage(std::move(receivedMessage));
  JAST &result = message.add("result", JSON_OBJECT);

  std::string value;
  for (const SymbolDefinition &def : hoverInfoPieces) {
    value += "**" + def.name + ": " + def.type + "**\n\n";
    value += def.documentation + "\n\n";
    if (!def.introduces.empty()) {
      value += "Introduces:\n\n";
      for (auto introduced : def.introduces) {
        value += "**" + introduced.first + ": " + introduced.second + "**\n\n";
      }
    }
    value += def.outerDocumentation + "\n\n";
  }
  if (!value.empty()) {
    JAST &contents = result.add("contents", JSON_OBJECT);
    contents.add("kind", "markdown");
    contents.add("value", value.c_str());
  }
  return message;
}

void appendSymbolToJSON(const SymbolDefinition &def, JAST &json, const std::string &uriScheme) {
  JAST &symbol = json.add("", JSON_OBJECT);
  symbol.add("name", def.name + ": " + def.type);
  symbol.add("kind", def.symbolKind);
  symbol.children.emplace_back("location", createLocationJSON(def.location, uriScheme));
}

JAST workspaceEditsToJSON(JAST receivedMessage, const std::vector<Location> &references,
                          const std::string &newName, const std::string &uriScheme) {
  JAST message = createResponseMessage(std::move(receivedMessage));
  JAST &result = message.add("result", JSON_OBJECT);

  std::map<std::string, JAST> filesEdits;
  for (const Location &ref : references) {
    JAST edit(JSON_OBJECT);
    edit.children.emplace_back("range", createRangeFromLocation(ref));
    edit.add("newText", newName.c_str());

    std::string fileUri = encodePath(ref.filename, uriScheme);
    if (filesEdits.find(fileUri) == filesEdits.end()) {
      filesEdits[fileUri] = JAST(JSON_ARRAY);
    }
    filesEdits[fileUri].children.emplace_back("", edit);
  }

  if (!filesEdits.empty()) {
    JAST &changes = result.add("changes", JSON_OBJECT);
    for (const auto &fileEdits : filesEdits) {
      changes.children.emplace_back(fileEdits.first, fileEdits.second);
    }
  }
  return message;
}
}  // namespace JSONConverter
