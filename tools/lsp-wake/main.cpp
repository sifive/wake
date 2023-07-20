/* Wake Language Server Protocol implementation
 *
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

// Open Group Base Specifications Issue 7
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <poll.h>
#include <sys/time.h>
#include <unistd.h>
#include <wcl/defer.h>
#include <wcl/filepath.h>
#include <wcl/tracing.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <utility>

#include "astree.h"
#include "compat/readable.h"
#include "json/json5.h"
#include "json_converter.h"
#include "parser/parser.h"
#include "parser/wakefiles.h"
#include "types/internal.h"
#include "util/diagnostic.h"
#include "util/execpath.h"
#include "util/location.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

// Header used in JSON-RPC
static const char contentLength[] = "Content-Length: ";

// Defined by JSON RPC
static const char *ParseError = "-32700";
static const char *InvalidRequest = "-32600";
static const char *MethodNotFound = "-32601";
static const char *InvalidParams = "-32602";
// static const char *InternalError        = "-32603";
// static const char *serverErrorStart     = "-32099";
// static const char *serverErrorEnd       = "-32000";
static const char *ServerNotInitialized = "-32002";
// static const char *UnknownErrorCode     = "-32001";

DiagnosticReporter *reporter;

static inline bool message_is_notification(JAST request) {
  const JAST &entry = request.get("id");
  return entry.kind == JSON_NULLVAL;
}

class LSPServer {
 public:
  LSPServer() = default;

  struct MethodResult {
    JAST response;
    JAST diagnostics;
    JAST notification;
    MethodResult() {
      response = JAST(JSON_OBJECT);
      diagnostics = JAST(JSON_ARRAY);
      notification = JAST(JSON_OBJECT);
    }
    explicit MethodResult(JAST _response) : response(std::move(_response)) {
      diagnostics = JAST(JSON_ARRAY);
      notification = JAST(JSON_OBJECT);
    }
  };

  MethodResult processRequest(const std::string &requestString) {
    wcl::log::info("%s", requestString.c_str())({{"rpc", "rx"}});

    // Parse that requestString as JSON
    JAST request;
    std::stringstream parseErrors;
    if (!JAST::parse(requestString, parseErrors, request)) {
      JAST errorMessage = JSONConverter::createErrorMessage(ParseError, parseErrors.str());
      return MethodResult(errorMessage);
    }

    const std::string &method = request.get("method").value;
    if (!isInitialized && (method != "initialize")) {
      JAST errorMessage = JSONConverter::createErrorMessage(request, ServerNotInitialized,
                                                            "Must request initialize first");
      return MethodResult(errorMessage);
    }

    if (isShutDown && (method != "exit")) {
      JAST errorMessage = JSONConverter::createErrorMessage(
          request, InvalidRequest,
          "Received a request other than 'exit' after a shutdown request.");
      return MethodResult(errorMessage);
    }

    if (!method.empty()) {
      return callMethod(method, request);
    }
    return {};  // empty result
  }

  void processRequests() {
    std::string buffer;

    while (true) {
      size_t json_size = 0;
      // Read header lines until an empty line
      while (true) {
        // Grab a line, terminated by a not-included '\n'
        std::string line = getLine(buffer);
        // Trim trailing CR, if any
        if (!line.empty() && line.back() == '\r') line.resize(line.size() - 1);
        // Empty line? stop
        if (line.empty()) break;
        // Capture the json_size
        if (line.compare(0, sizeof(contentLength) - 1, &contentLength[0]) == 0)
          json_size = std::stoul(line.substr(sizeof(contentLength) - 1));
      }

      // Content-Length was unset?
      if (json_size == 0) exit(1);

      // Retrieve the content
      std::string content = getBlob(buffer, json_size);
      MethodResult methodResult = processRequest(content);

      const std::string errorCode = methodResult.response.get("error").get("code").value;
      if (errorCode.empty()) {  // no error occurred
        const std::string notifMethod = methodResult.notification.get("method").value;
        if (!notifMethod.empty()) {  // notification was produced => send it
          sendMessage(methodResult.notification);
        }

        for (const auto &fileDiagnostics : methodResult.diagnostics.children) {
          sendMessage(fileDiagnostics.second);  // send diagnostics
        }
      }
      sendMessage(methodResult.response);  // send response
    }
  }

 private:
  typedef MethodResult (LSPServer::*LspMethod)(const JAST &);

  bool isInitialized = false;
  bool needsUpdate = false;
  int ignoredCount = 0;
  bool isShutDown = false;
  ASTree astree;
  std::map<std::string, LspMethod> essentialMethods = {
      {"initialize", &LSPServer::initialize},
      {"initialized", &LSPServer::initialized},
      {"textDocument/didOpen", &LSPServer::didOpen},
      {"textDocument/didChange", &LSPServer::didChange},
      {"textDocument/didSave", &LSPServer::didSave},
      {"textDocument/didClose", &LSPServer::didClose},
      {"workspace/didChangeWatchedFiles", &LSPServer::didChangeWatchedFiles},
      {"shutdown", &LSPServer::shutdown},
      {"exit", &LSPServer::serverExit}};
  std::map<std::string, LspMethod> additionalMethods = {
      {"textDocument/definition", &LSPServer::goToDefinition},
      {"textDocument/references", &LSPServer::findReportReferences},
      {"textDocument/documentHighlight", &LSPServer::highlightOccurrences},
      {"textDocument/hover", &LSPServer::hover},
      {"textDocument/documentSymbol", &LSPServer::documentSymbol},
      {"workspace/symbol", &LSPServer::workspaceSymbol},
      {"textDocument/rename", &LSPServer::rename}};

  void notifyAboutInvalidSTDLib(MethodResult &methodResult, const std::string &libDir) const {
    JAST message = JSONConverter::createMessage();
    message.add("method", "window/showMessage");
    JAST &showMessageParams = message.add("params", JSON_OBJECT);
    showMessageParams.add("type", 1);  // Error
    // TODO: make this message dependent on the client
    std::string messageText =
        "The path to the wake standard library (" + libDir + ") is invalid. " +
        "Wake language features will not be provided. " +
        "Please change the path in the extension settings and reload the window by: " +
        "  1. Opening the command palette (Ctrl + Shift + P); " +
        "  2. Typing \"> Reload Window\" and executing (Enter);";
    showMessageParams.add("message", messageText.c_str());
    methodResult.notification = message;
  }

  std::string getLine(std::string &buffer) {
    size_t len = 0, off = buffer.find('\n');
    while (off == std::string::npos) {
      std::string got = getStdin();
      len = buffer.size();
      off = got.find('\n');
      buffer.append(got);
    }

    off += len;
    std::string out(buffer, 0, off);  // excluding '\n'
    buffer.erase(0, off + 1);         // including '\n'
    return out;
  }

  std::string getBlob(std::string &buffer, size_t length) {
    while (buffer.size() < length) buffer.append(getStdin());

    std::string out(buffer, 0, length);
    buffer.erase(0, length);
    return out;
  }

  std::string getStdin() {
    while (true) {
      struct pollfd pfds;
      pfds.fd = STDIN_FILENO;
      pfds.events = POLLIN;

      int ret = poll(&pfds, 1, 2000);
      if (ret == -1) {
        wcl::log::error("poll(stdin): %s", strerror(errno))();
        exit(1);
      }

      // Timeout expired?
      if (ret == 0) {
        timeout();
        continue;
      }

      char buf[4096];
      int got = read(STDIN_FILENO, &buf[0], sizeof(buf));
      if (got == -1) {
        wcl::log::error("read(stdin): %s", strerror(errno))();
        exit(1);
      }

      // End-of-file reached?
      if (got == 0) {
        wcl::log::error("Client did not shutdown cleanly")();
        exit(1);
      }

      return std::string(&buf[0], got);
    }
  }

  void refresh(const std::string &why, MethodResult &methodResult) {
    ignoredCount = 0;
    if (needsUpdate) {
      struct timeval start, stop;
      gettimeofday(&start, 0);
      diagnoseProject(methodResult);
      gettimeofday(&stop, 0);
      double delay = (stop.tv_sec - start.tv_sec) + (stop.tv_usec - start.tv_usec) / 1000000.0;
      wcl::log::info("Refreshed project in %f seconds (due to %s)", delay, why.c_str())();
    }
  }

  void timeout() {
    MethodResult methodResult;
    // Send message w/ diagnostics if there are any
    refresh("timeout", methodResult);
  }

  MethodResult callMethod(const std::string &method, const JAST &request) {
    auto functionPointer = essentialMethods.find(method);
    if (functionPointer != essentialMethods.end()) {
      return (this->*(functionPointer->second))(request);
    }

    functionPointer = additionalMethods.find(method);
    if (functionPointer != additionalMethods.end()) {
      return (this->*(functionPointer->second))(request);
    }

    // If a server or client receives notifications starting with ‘$/’
    // it is free to ignore the notification.
    if (message_is_notification(request) && method[0] == '$' && method[1] == '/') {
      return {};
    }

    JAST errorMessage = JSONConverter::createErrorMessage(
        request, MethodNotFound, "Method '" + method + "' is not implemented.");
    return MethodResult(errorMessage);
  }

  static void sendMessage(const JAST &message) {
    std::stringstream str;
    str << message;

    std::string msg = str.str();

    wcl::log::info("%s", msg.c_str())({{"rpc", "tx"}});

    if (msg == "{}") {
      wcl::log::warning("Throwing away empty response message")();
      return;
    }

    std::cout << contentLength << (msg.size() + 2) << "\r\n\r\n";
    std::cout << msg << "\r\n" << std::flush;
  }

  MethodResult initialize(const JAST &receivedMessage) {
    MethodResult methodResult;

    std::string stdLibPath = wcl::make_canonical(find_execpath() + "/../../share/wake/lib");
    auto initializationOptions = receivedMessage.get("params").get("initializationOptions");
    if (initializationOptions.kind == JSON_OBJECT) {
      auto stdLibPathEntry = initializationOptions.get("stdLibPath");
      if (stdLibPathEntry.kind == JSON_STR) {
        stdLibPath = stdLibPathEntry.value;
      }
    }

    bool stdLibValid = is_readable((stdLibPath + "/core/boolean.wake").c_str());
    if (!stdLibValid) {
      notifyAboutInvalidSTDLib(methodResult, stdLibPath);  // set notification
      methodResult.response = JSONConverter::createInitializeResultInvalidSTDLib(receivedMessage);
      return methodResult;
    }

    methodResult.response = JSONConverter::createInitializeResultDefault(receivedMessage);

    isInitialized = true;
    std::string workspaceUri =
        receivedMessage.get("params").get("workspaceFolders").children[0].second.get("uri").value;

    astree.absLibDir = stdLibPath;
    astree.absWorkDir = JSONConverter::decodePath(workspaceUri);

    wcl::log::info("Initialized LSP with stdlib = %s, workspace = %s", astree.absLibDir.c_str(),
                   astree.absWorkDir.c_str())();

    return methodResult;
  }

  MethodResult initialized(const JAST &_) {
    MethodResult methodResult;
    needsUpdate = true;
    refresh("initialized", methodResult);  // set diagnostics
    return methodResult;
  }

  void diagnoseProject(MethodResult &methodResult) {
    astree.diagnoseProject([&methodResult](ASTree::FileDiagnostics &fileDiagnostics) {
      JAST fileDiagnosticsJSON =
          JSONConverter::fileDiagnosticsToJSON(fileDiagnostics.first, fileDiagnostics.second);
      methodResult.diagnostics.children.emplace_back("", fileDiagnosticsJSON);
    });
    needsUpdate = false;
  }

  MethodResult goToDefinition(const JAST &receivedMessage) {
    MethodResult methodResult;
    refresh("goto-definition", methodResult);
    Location locationToDefine = JSONConverter::getLocationFromJSON(receivedMessage);
    Location definitionLocation = astree.findDefinitionLocation(locationToDefine);
    JAST definitionLocationJSON =
        JSONConverter::definitionLocationToJSON(receivedMessage, definitionLocation);
    methodResult.response = definitionLocationJSON;
    return methodResult;
  }

  MethodResult findReportReferences(const JAST &receivedMessage) {
    MethodResult methodResult;
    refresh("report-references", methodResult);
    Location definitionLocation = JSONConverter::getLocationFromJSON(receivedMessage);
    bool isDefinitionFound = false;
    std::vector<Location> references;

    astree.findReferences(definitionLocation, isDefinitionFound, references);
    if (isDefinitionFound &&
        receivedMessage.get("params").get("context").get("includeDeclaration").value == "true") {
      references.push_back(definitionLocation);
    }

    JAST referencesJSON = JSONConverter::referencesToJSON(receivedMessage, references);
    methodResult.response = referencesJSON;
    return methodResult;
  }

  MethodResult highlightOccurrences(const JAST &receivedMessage) {
    MethodResult methodResult;
    if (needsUpdate) {
      if (++ignoredCount > 2) {
        refresh("highlight", methodResult);
      } else {
        wcl::log::info("Opting not to refresh code for highlight request")();
      }
    }
    Location symbolLocation = JSONConverter::getLocationFromJSON(receivedMessage);
    std::vector<Location> occurrences = astree.findOccurrences(symbolLocation);
    JAST highlightsJSON = JSONConverter::highlightsToJSON(receivedMessage, occurrences);
    methodResult.response = highlightsJSON;
    return methodResult;
  }

  MethodResult hover(const JAST &receivedMessage) {
    MethodResult methodResult;
    if (needsUpdate) {
      if (++ignoredCount > 2) {
        refresh("hover", methodResult);
      } else {
        wcl::log::info("Opting not to refresh code for hover request")();
      }
    }
    Location symbolLocation = JSONConverter::getLocationFromJSON(receivedMessage);
    std::vector<SymbolDefinition> hoverInfoPieces = astree.findHoverInfo(symbolLocation);
    JAST hoverInfoJSON = JSONConverter::hoverInfoToJSON(receivedMessage, hoverInfoPieces);
    methodResult.response = hoverInfoJSON;
    return methodResult;
  }

  MethodResult documentSymbol(const JAST &receivedMessage) {
    MethodResult methodResult;
    if (needsUpdate) {
      if (++ignoredCount > 2) {
        refresh("document-symbol", methodResult);
      } else {
        wcl::log::info("Opting not to refresh code for document-symbol request")();
      }
    }
    JAST message = JSONConverter::createResponseMessage(receivedMessage);
    JAST &result = message.add("result", JSON_ARRAY);

    std::string fileUri = receivedMessage.get("params").get("textDocument").get("uri").value;
    std::string filePath = JSONConverter::decodePath(fileUri);

    std::vector<SymbolDefinition> symbols = astree.documentSymbol(filePath);
    for (const SymbolDefinition &symbol : symbols) {
      JSONConverter::appendSymbolToJSON(symbol, result);
    }
    methodResult.response = message;
    return methodResult;
  }

  MethodResult workspaceSymbol(const JAST &receivedMessage) {
    MethodResult methodResult;
    refresh("workspace-symbol", methodResult);
    JAST message = JSONConverter::createResponseMessage(receivedMessage);
    JAST &result = message.add("result", JSON_ARRAY);

    std::string query = receivedMessage.get("params").get("query").value;
    std::vector<SymbolDefinition> symbols = astree.workspaceSymbol(query);
    for (const SymbolDefinition &symbol : symbols) {
      JSONConverter::appendSymbolToJSON(symbol, result);
    }
    methodResult.response = message;
    return methodResult;
  }

  MethodResult rename(const JAST &receivedMessage) {
    MethodResult methodResult;
    refresh("rename-symbol", methodResult);
    std::string newName = receivedMessage.get("params").get("newName").value;
    if (newName.find(' ') != std::string::npos || (newName[0] >= '0' && newName[0] <= '9')) {
      JAST errorMessage = JSONConverter::createErrorMessage(receivedMessage, InvalidParams,
                                                            "The given name is invalid.");
      methodResult.response = errorMessage;
      return methodResult;
    }

    Location definitionLocation = JSONConverter::getLocationFromJSON(receivedMessage);
    bool isDefinitionFound = false;
    std::vector<Location> references;
    astree.findReferences(definitionLocation, isDefinitionFound, references);
    if (isDefinitionFound) {
      references.push_back(definitionLocation);
    }
    JAST workspaceEditsJSON =
        JSONConverter::workspaceEditsToJSON(receivedMessage, references, newName);
    methodResult.response = workspaceEditsJSON;
    return methodResult;
  }

  MethodResult didOpen(const JAST &_) {
    // no refresh should be needed
    return {};
  }

  MethodResult didChange(const JAST &receivedMessage) {
    std::string fileUri = receivedMessage.get("params").get("textDocument").get("uri").value;
    std::string fileContent = receivedMessage.get("params")
                                  .get("contentChanges")
                                  .children.back()
                                  .second.get("text")
                                  .value;
    std::string fileName = JSONConverter::decodePath(fileUri);
    astree.changedFiles[fileName] =
        std::make_unique<StringFile>(fileName.c_str(), std::move(fileContent));
    needsUpdate = true;
    ignoredCount = 0;
    return {};
  }

  MethodResult didSave(const JAST &receivedMessage) {
    std::string fileUri = receivedMessage.get("params").get("textDocument").get("uri").value;
    astree.changedFiles.erase(JSONConverter::decodePath(fileUri));

    // Might have replaced a file modified on disk
    needsUpdate = true;
    MethodResult methodResult;
    refresh("file-save", methodResult);
    return methodResult;
  }

  MethodResult didClose(const JAST &receivedMessage) {
    std::string fileUri = receivedMessage.get("params").get("textDocument").get("uri").value;
    if (astree.changedFiles.erase(JSONConverter::decodePath(fileUri)) > 0) {
      needsUpdate = true;
      // If a user hits 'undo' on a symbol rename, you can get hundreds of sequential didClose
      // invocations Calling refresh here would cause the extension to 'hang' for a very long time.
    }
    return {};
  }

  MethodResult didChangeWatchedFiles(const JAST &receivedMessage) {
    MethodResult methodResult;

    JAST jfiles = receivedMessage.get("params").get("changes");
    for (auto child : jfiles.children) {
      std::string filePath = JSONConverter::decodePath(child.second.get("uri").value);
      // Newly created, modified on disk, or deleted? => File should be re-read from disk.
      astree.changedFiles.erase(filePath);

      if (stoi(child.second.get("type").value) == 3) {
        // The file was deleted => clear any stale diagnostics
        std::vector<Diagnostic> emptyDiagnostics;
        JAST fileDiagnosticsJSON = JSONConverter::fileDiagnosticsToJSON(filePath, emptyDiagnostics);
        methodResult.diagnostics.children.emplace_back("", fileDiagnosticsJSON);
      }
    }

    if (!jfiles.children.empty()) {
      needsUpdate = true;
      refresh("files-created-or-deleted", methodResult);
    }
    return methodResult;
  }

  MethodResult shutdown(const JAST &receivedMessage) {
    MethodResult methodResult;
    JAST message = JSONConverter::createResponseMessage(receivedMessage);
    message.add("result", JSON_NULLVAL);
    isShutDown = true;
    methodResult.response = message;
    return methodResult;
  }

  MethodResult serverExit(const JAST &_) { exit(isShutDown ? 0 : 1); }
};

std::unique_ptr<LSPServer> lspServer;

void instantiateServerImpl() {
  wcl::log::info("Instantiating lsp server")();
  lspServer = std::make_unique<LSPServer>();
}

#ifdef __EMSCRIPTEN__

extern "C" {

void instantiateServer() { instantiateServerImpl(); }

char *processRequest(const char *request) {
  LSPServer::MethodResult methodResult = lspServer->processRequest(request);

  JAST jsonResult(JSON_OBJECT);
  jsonResult.children.emplace_back("response", methodResult.response);
  jsonResult.children.emplace_back("diagnostics", methodResult.diagnostics);
  jsonResult.children.emplace_back("notification", methodResult.notification);

  // Turn the json into a string
  std::stringstream stringstream;
  stringstream << jsonResult;
  std::string str = stringstream.str();
  const char *c = str.c_str();
  size_t length = strlen(c);

  // Malloc the string for typescript to use.
  // Typescript will free it in lsp-server/src/common.ts/getResponse()
  char *result = static_cast<char *>(malloc(length + 1));
  memcpy(result, c, length);
  result[length] = '\0';
  return result;
}
}

#else

int main(int argc, const char **argv) {
  const char *wake_lsp_log_path = getenv("WAKE_LSP_LOG_PATH");

  std::ofstream log_file;
  auto log_file_defer = wcl::make_defer([&log_file]() { log_file.close(); });
  if (wake_lsp_log_path != nullptr) {
    log_file = std::ofstream(wake_lsp_log_path, std::ios::app);
    wcl::log::subscribe(std::make_unique<wcl::log::FormatSubscriber>(log_file.rdbuf()));
  }

  instantiateServerImpl();

  // Process requests until something goes wrong
  lspServer->processRequests();
}

#endif
