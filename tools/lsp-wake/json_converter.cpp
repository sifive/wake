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

#include <map>

#include "symbol_definition.h"
#include "json_converter.h"
#include "json/json5.h"

namespace JSONConverter {
    std::string stripRootUri(const std::string &fileUri, const std::string &rootUri) {
      if (fileUri.size() < rootUri.size()+1) return "";
      if (fileUri.compare(0, rootUri.size(), rootUri) != 0) return "";
      if (fileUri[rootUri.size()] != '/') return "";
      return fileUri.substr(rootUri.size()+1);
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
          end.add("character", std::max(0, location.end.column)); // It can be -1

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

        JAST createLocationJSON(const Location &location, const std::string &rootUri) {
          JAST locationJSON(JSON_OBJECT);
          std::string fileUri = rootUri + '/' + location.filename;
          locationJSON.add("uri", fileUri.c_str());
          locationJSON.children.emplace_back("range", createRangeFromLocation(location));
          return locationJSON;
        }

        JAST createDocumentHighlightJSON(const Location &location) {
          JAST documentHighlightJSON(JSON_OBJECT);
          documentHighlightJSON.children.emplace_back("range", createRangeFromLocation(location));
          return documentHighlightJSON;
        }
    }

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

    Location getLocationFromJSON(JAST receivedMessage, const std::string &rootUri) {
      std::string fileURI = receivedMessage.get("params").get("textDocument").get("uri").value;

      int row = stoi(receivedMessage.get("params").get("position").get("line").value);
      int column = stoi(receivedMessage.get("params").get("position").get("character").value);
      return {stripRootUri(fileURI, rootUri).c_str(), Coordinates(row + 1, column + 1), Coordinates(row + 1, column)};
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

    JAST createInitializeResultCrashed(const JAST &receivedMessage) {
      JAST message = createResponseMessage(receivedMessage);
      JAST &result = message.add("result", JSON_OBJECT);

      JAST &capabilities = result.add("capabilities", JSON_OBJECT);
      capabilities.add("textDocumentSync", 1);

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

    JAST fileDiagnosticsToJSON(const std::string &filePath, const std::vector<Diagnostic> &fileDiagnostics, const std::string &rootUri) {
      JAST diagnosticsArray(JSON_ARRAY);
      for (const Diagnostic &diagnostic: fileDiagnostics) {
        diagnosticsArray.children.emplace_back("", createDiagnostic(diagnostic)); // add .add for JSON_OBJECT to JSON_ARRAY
      }
      JAST message = createDiagnosticMessage();
      JAST &params = message.add("params", JSON_OBJECT);
      std::string fileUri = rootUri + '/' + filePath;
      params.add("uri", fileUri.c_str());
      params.children.emplace_back("diagnostics", diagnosticsArray);
      return message;
    }

    JAST definitionLocationToJSON(JAST receivedMessage, const Location &definitionLocation, const std::string &rootUri) {
      JAST message = createResponseMessage(std::move(receivedMessage));
      JAST &result = message.add("result", JSON_OBJECT);
      if (!definitionLocation.filename.empty()) {
        result = createLocationJSON(definitionLocation, rootUri);
      }
      return message;
    }

    JAST referencesToJSON(JAST receivedMessage, const std::vector<Location> &references, const std::string &rootUri) {
      JAST message = createResponseMessage(std::move(receivedMessage));
      JAST &result = message.add("result", JSON_ARRAY);
      for (const Location &location: references) {
        result.children.emplace_back("", createLocationJSON(location, rootUri));
      }
      return message;
    }

    JAST highlightsToJSON(JAST receivedMessage, const std::vector<Location> &occurrences) {
      JAST message = createResponseMessage(std::move(receivedMessage));
      JAST &result = message.add("result", JSON_ARRAY);
      for (const Location &location: occurrences) {
        result.children.emplace_back("", createDocumentHighlightJSON(location));
      }
      return message;
    }

    JAST hoverInfoToJSON(JAST receivedMessage, const std::vector<SymbolDefinition> &hoverInfoPieces) {
      JAST message = createResponseMessage(std::move(receivedMessage));
      JAST &result = message.add("result", JSON_OBJECT);

      std::string value;
      for (const SymbolDefinition& def: hoverInfoPieces) {
        value += "**" + def.name + ": " + def.type + "**\n\n";
        value += def.documentation + "\n\n";
      }
      if (!value.empty()) {
        JAST &contents = result.add("contents",JSON_OBJECT);
        contents.add("kind", "markdown");
        contents.add("value", value.c_str());
      }
      return message;
    }

    void appendSymbolToJSON(const SymbolDefinition& def, JAST &json, const std::string &rootUri) {
      JAST &symbol = json.add("", JSON_OBJECT);
      symbol.add("name", def.name + ": " + def.type);
      symbol.add("kind", def.symbolKind);
      symbol.children.emplace_back("location", createLocationJSON(def.location, rootUri));
    }

    JAST workspaceEditsToJSON(JAST receivedMessage, const std::vector<Location> &references, const std::string &newName, const std::string &rootUri) {
      JAST message = createResponseMessage(std::move(receivedMessage));
      JAST &result = message.add("result", JSON_OBJECT);

      std::map<std::string, JAST> filesEdits;
      for (const Location &ref: references) {
        JAST edit(JSON_OBJECT);
        edit.children.emplace_back("range", createRangeFromLocation(ref));
        edit.add("newText", newName.c_str());

        std::string fileUri = rootUri + '/' + ref.filename;
        if (filesEdits.find(fileUri) == filesEdits.end()) {
          filesEdits[fileUri] = JAST(JSON_ARRAY);
        }
        filesEdits[fileUri].children.emplace_back("", edit);
      }

      if (!filesEdits.empty()) {
        JAST &changes = result.add("changes", JSON_OBJECT);
        for (const auto &fileEdits: filesEdits) {
          changes.children.emplace_back(fileEdits.first, fileEdits.second);
        }
      }
      return message;
    }
}
