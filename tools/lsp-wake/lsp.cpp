/* Wake Language Server Protocol implementation
 *
 * Copyright 2020 SiFive, Inc.
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

#include <sys/time.h>
#include <unistd.h>
#include <iostream>
#include <string>
#include <map>
#include <fstream>
#include <utility>
#include <algorithm>

#include "compat/readable.h"
#include "util/location.h"
#include "util/execpath.h"
#include "util/diagnostic.h"
#include "json/json5.h"
#include "parser/parser.h"
#include "parser/wakefiles.h"
#include "types/internal.h"
#include "json_converter.h"
#include "astree.h"

#ifndef VERSION
#include "../src/version.h"
#endif

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define VERSION_STR TOSTRING(VERSION)

#ifdef __EMSCRIPTEN__
#define CERR_DEBUG
#include <emscripten/emscripten.h>

EM_ASYNC_JS(char *, nodejs_getstdin, (), {
  var buffer = "";

  let eof = await new Promise(resolve => {
    let timeout = setTimeout(() => {
      complete(false);
    }, 2000);
    function gotData(input) {
      buffer = input;
      complete(false);
    }
    function gotEnd() {
      complete(true);
    }
    function complete(out) {
      clearTimeout(timeout);
      process.stdin.pause();
      process.stdin.removeListener('end',   gotEnd);
      process.stdin.removeListener('error', gotEnd);
      process.stdin.removeListener('data',  gotData);
      resolve(out);
    }
    process.stdin.setEncoding('utf8');
    process.stdin.on('end',   gotEnd);
    process.stdin.on('error', gotEnd);
    process.stdin.on('data',  gotData);
    process.stdin.resume();
  });

  if (eof) {
    return 0;
  } else {
    let lengthBytes = lengthBytesUTF8(buffer)+1;
    let stringOnWasmHeap = _malloc(lengthBytes);
    stringToUTF8(buffer, stringOnWasmHeap, lengthBytes);
    return stringOnWasmHeap;
  }
});
#endif

// Header used in JSON-RPC
static const char contentLength[] = "Content-Length: ";

// Defined by JSON RPC
static const char *ParseError           = "-32700";
static const char *InvalidRequest       = "-32600";
static const char *MethodNotFound       = "-32601";
static const char *InvalidParams        = "-32602";
//static const char *InternalError        = "-32603";
//static const char *serverErrorStart     = "-32099";
//static const char *serverErrorEnd       = "-32000";
static const char *ServerNotInitialized = "-32002";
//static const char *UnknownErrorCode     = "-32001";

DiagnosticReporter *reporter;

const char *term_colour(int _) { return ""; }
const char *term_normal()         { return ""; }


class LSPServer {
public:
    LSPServer(): astree() {}
    explicit LSPServer(std::string _stdLib) : astree(std::move(_stdLib)) {}
    explicit LSPServer(bool _isSTDLibValid, std::string _stdLib) : isSTDLibValid(_isSTDLibValid), astree(std::move(_stdLib)) {}

    void processRequests() {
      std::string buffer;

      while (true) {
        size_t json_size = 0;
        // Read header lines until an empty line
        while (true) {
          // Grab a line, terminated by a not-included '\n'
          std::string line = getLine(buffer);
          // Trim trailing CR, if any
          if (!line.empty() && line.back() == '\r')
            line.resize(line.size() - 1);
          // Empty line? stop
          if (line.empty())
            break;
          // Capture the json_size
          if (line.compare(0, sizeof(contentLength) - 1, &contentLength[0]) == 0)
            json_size = std::stoul(line.substr(sizeof(contentLength) - 1));
        }

        // Content-Length was unset?
        if (json_size == 0)
          exit(1);

        // Retrieve the content
        std::string content = getBlob(buffer, json_size);

        // Parse that content as JSON
        JAST request;
        std::stringstream parseErrors;
        if (!JAST::parse(content, parseErrors, request)) {
          JAST errorMessage = JSONConverter::createErrorMessage(ParseError, parseErrors.str());
          sendMessage(errorMessage);
        } else {
          const std::string &method = request.get("method").value;
          if (!isInitialized && (method != "initialize")) {
            JAST errorMessage = JSONConverter::createErrorMessage(request, ServerNotInitialized, "Must request initialize first");
            sendMessage(errorMessage);
          } else if (isShutDown && (method != "exit")) {
            JAST errorMessage = JSONConverter::createErrorMessage(request, InvalidRequest, "Received a request other than 'exit' after a shutdown request.");
            sendMessage(errorMessage);
          } else if (!method.empty()) {
            callMethod(method, request);
          }
        }
      }
    }

    void notifyAboutInvalidSTDLib() const {
      JAST message = JSONConverter::createMessage();
      message.add("method", "window/showMessage");
      JAST &showMessageParams = message.add("params", JSON_OBJECT);
      showMessageParams.add("type", 1); // Error
      std::string messageText =
        "The path to the wake standard library (" + astree.absLibDir + ") is invalid. " +
        "Wake language features will not be provided. " +
        "Please change the path in the extension settings and reload the window by: " +
        "  1. Opening the command palette (Ctrl + Shift + P); " +
        "  2. Typing \"> Reload Window\" and executing (Enter);";
      showMessageParams.add("message", messageText.c_str());
      sendMessage(message);
    }

private:
    typedef void (LSPServer::*LspMethod)(const JAST &);

    bool isSTDLibValid = true;
    bool isInitialized = false;
    bool needsUpdate = false;
    int ignoredCount = 0;
    bool isShutDown = false;
    ASTree astree;
    std::map<std::string, LspMethod> essentialMethods = {
      {"initialize",                      &LSPServer::initialize},
      {"initialized",                     &LSPServer::initialized},
      {"textDocument/didOpen",            &LSPServer::didOpen},
      {"textDocument/didChange",          &LSPServer::didChange},
      {"textDocument/didSave",            &LSPServer::didSave},
      {"textDocument/didClose",           &LSPServer::didClose},
      {"workspace/didChangeWatchedFiles", &LSPServer::didChangeWatchedFiles},
      {"shutdown",                        &LSPServer::shutdown},
      {"exit",                            &LSPServer::serverExit}
    };
    std::map<std::string, LspMethod> additionalMethods = {
      {"textDocument/definition",         &LSPServer::goToDefinition},
      {"textDocument/references",         &LSPServer::findReportReferences},
      {"textDocument/documentHighlight",  &LSPServer::highlightOccurrences},
      {"textDocument/hover",              &LSPServer::hover},
      {"textDocument/documentSymbol",     &LSPServer::documentSymbol},
      {"workspace/symbol",                &LSPServer::workspaceSymbol},
      {"textDocument/rename",             &LSPServer::rename}
    };

    std::string getLine(std::string &buffer) {
      size_t len = 0, off = buffer.find('\n');
      while (off == std::string::npos) {
        std::string got = getStdin();
        len = buffer.size();
        off = got.find('\n');
        buffer.append(got);
      }

      off += len;
      std::string out(buffer, 0, off); // excluding '\n'
      buffer.erase(0, off+1); // including '\n'
      return out;
    }

    std::string getBlob(std::string &buffer, size_t length) {
      while (buffer.size() < length)
        buffer.append(getStdin());

      std::string out(buffer, 0, length);
      buffer.erase(0, length);
      return out;
    }

    std::string getStdin() {
      while (true) {
#ifdef __EMSCRIPTEN__
        char *buf = nodejs_getstdin();
        if (!buf) {
#ifdef CERR_DEBUG
          std::cerr << "Client did not shutdown cleanly" << std::endl;
#endif
          exit(1);
        }

        std::string out(buf, strlen(buf));
        free(buf);

        if (out.empty()) {
          poll();
          continue;
        }

        return out;
#else
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);

        struct timeval tv;
        tv.tv_sec = 2;
        tv.tv_usec = 0;

        int ret = select(STDIN_FILENO+1, &rfds, nullptr, nullptr, &tv);
        if (ret == -1) {
          perror("select(stdin)");
          exit(1);
        }

        // Timeout expired?
        if (ret == 0) {
          poll();
          continue;
        }

        char buf[4096];
        int got = read(STDIN_FILENO, &buf[0], sizeof(buf));
        if (got == -1) {
          perror("read(stdin)");
          exit(1);
        }

        // End-of-file reached?
        if (got == 0) {
#ifdef CERR_DEBUG
          std::cerr << "Client did not shutdown cleanly" << std::endl;
#endif
          exit(1);
        }

        return std::string(&buf[0], got);
#endif
      }
    }

    void refresh(const std::string &why) {
      ignoredCount = 0;
      if (needsUpdate) {
#ifdef CERR_DEBUG
        struct timeval start, stop;
        gettimeofday(&start, 0);
#endif
        diagnoseProject();
#ifdef CERR_DEBUG
        gettimeofday(&stop, 0);
        double delay =
          (stop.tv_sec  - start.tv_sec) +
          (stop.tv_usec - start.tv_usec)/1000000.0;
        std::cerr << "Refreshed project in " << delay << " seconds (due to " << why << ")" << std::endl;
#endif
      }
    }

    void poll() {
      refresh("timeout");
    }

    void callMethod(const std::string &method, const JAST &request) {
      auto functionPointer = essentialMethods.find(method);
      if (functionPointer != essentialMethods.end()) {
        (this->*(functionPointer->second))(request);
      } else {
        functionPointer = additionalMethods.find(method);
        if (functionPointer != additionalMethods.end()) {
          (this->*(functionPointer->second))(request);
        } else {
          JAST errorMessage = JSONConverter::createErrorMessage(request, MethodNotFound, "Method '" + method + "' is not implemented.");
          sendMessage(errorMessage);
        }
      }
    }

    static void sendMessage(const JAST &message) {
      std::stringstream str;
      str << message;
      str.seekg(0, std::ios::end);
      size_t length = str.tellg();
      str.seekg(0, std::ios::beg);
      std::cout << contentLength << (length+2) << "\r\n\r\n";
      std::cout << str.rdbuf() << "\r\n" << std::flush;
    }

    void initialize(const JAST &receivedMessage) {
      JAST message(JSON_OBJECT);
      if (!isSTDLibValid) {
        notifyAboutInvalidSTDLib();
        message = JSONConverter::createInitializeResultInvalidSTDLib(receivedMessage);
      } else {
        message = JSONConverter::createInitializeResultDefault(receivedMessage);
      }

      isInitialized = true;
      astree.absWorkDir = JSONConverter::decodePath(receivedMessage.get("params").get("rootUri").value);
      sendMessage(message);

      if (isSTDLibValid) {
        needsUpdate = true;
        refresh("initialize");
      }
    }

    void initialized(const JAST &_) { }

    void registerCapabilities() {
      JAST message = JSONConverter::createRequestMessage();
      message.add("method", "client/registerCapability");
      JAST &registrationParams = message.add("params", JSON_OBJECT);
      JAST &registrations = registrationParams.add("registrations", JSON_ARRAY);

      for (const auto &method: additionalMethods) {
        JAST &registration = registrations.add("", JSON_OBJECT);
        registration.add("id", method.first.c_str());
        registration.add("method", method.first.c_str());
      }
      sendMessage(message);
    }

    void diagnoseProject() {
      astree.diagnoseProject([](ASTree::FileDiagnostics &fileDiagnostics) {
        JAST fileDiagnosticsJSON = JSONConverter::fileDiagnosticsToJSON(fileDiagnostics.first, fileDiagnostics.second);
        sendMessage(fileDiagnosticsJSON);
      });
      needsUpdate = false;
    }

    void goToDefinition(const JAST &receivedMessage) {
      refresh("goto-definition");
      Location locationToDefine = JSONConverter::getLocationFromJSON(receivedMessage);
      Location definitionLocation = astree.findDefinitionLocation(locationToDefine);
      JAST definitionLocationJSON = JSONConverter::definitionLocationToJSON(receivedMessage, definitionLocation);
      sendMessage(definitionLocationJSON);
    }

    void findReportReferences(const JAST &receivedMessage) {
      refresh("report-references");
      Location definitionLocation = JSONConverter::getLocationFromJSON(receivedMessage);
      bool isDefinitionFound = false;
      std::vector<Location> references;

      astree.findReferences(definitionLocation, isDefinitionFound, references);
      if (isDefinitionFound && receivedMessage.get("params").get("context").get("includeDeclaration").value == "true") {
        references.push_back(definitionLocation);
      }

      JAST referencesJSON = JSONConverter::referencesToJSON(receivedMessage, references);
      sendMessage(referencesJSON);
    }

    void highlightOccurrences(const JAST &receivedMessage) {
      if (needsUpdate) {
        if (++ignoredCount > 2) {
          refresh("highlight");
        } else {
#ifdef CERR_DEBUG
          std::cerr << "Opting not to refresh code for highlight request" << std::endl;
#endif
        }
      }
      Location symbolLocation = JSONConverter::getLocationFromJSON(receivedMessage);
      std::vector<Location> occurrences = astree.findOccurrences(symbolLocation);
      JAST highlightsJSON = JSONConverter::highlightsToJSON(receivedMessage, occurrences);
      sendMessage(highlightsJSON);
    }

    void hover(const JAST &receivedMessage) {
      if (needsUpdate) {
        if (++ignoredCount > 2) {
          refresh("hover");
        } else {
#ifdef CERR_DEBUG
          std::cerr << "Opting not to refresh code for hover request" << std::endl;
#endif
        }
      }
      Location symbolLocation = JSONConverter::getLocationFromJSON(receivedMessage);
      std::vector<SymbolDefinition> hoverInfoPieces = astree.findHoverInfo(symbolLocation);
      JAST hoverInfoJSON = JSONConverter::hoverInfoToJSON(receivedMessage, hoverInfoPieces);
      sendMessage(hoverInfoJSON);
    }

    void documentSymbol(const JAST &receivedMessage) {
      if (needsUpdate) {
        if (++ignoredCount > 2) {
          refresh("document-symbol");
        } else {
#ifdef CERR_DEBUG
          std::cerr << "Opting not to refresh code for document-symbol request" << std::endl;
#endif
        }
      }
      JAST message = JSONConverter::createResponseMessage(receivedMessage);
      JAST &result = message.add("result", JSON_ARRAY);

      std::string fileUri = receivedMessage.get("params").get("textDocument").get("uri").value;
      std::string filePath = JSONConverter::decodePath(fileUri);

      std::vector<SymbolDefinition> symbols = astree.documentSymbol(filePath);
      for (const SymbolDefinition &symbol: symbols) {
        JSONConverter::appendSymbolToJSON(symbol, result);
      }
      sendMessage(message);
    }

    void workspaceSymbol(const JAST &receivedMessage) {
      refresh("workspace-symbol");
      JAST message = JSONConverter::createResponseMessage(receivedMessage);
      JAST &result = message.add("result", JSON_ARRAY);

      std::string query = receivedMessage.get("params").get("query").value;
      std::vector<SymbolDefinition> symbols = astree.workspaceSymbol(query);
      for (const SymbolDefinition &symbol: symbols) {
        JSONConverter::appendSymbolToJSON(symbol, result);
      }
      sendMessage(message);
    }

    void rename(const JAST &receivedMessage) {
      refresh("rename-symbol");
      std::string newName = receivedMessage.get("params").get("newName").value;
      if (newName.find(' ') != std::string::npos || (newName[0] >= '0' && newName[0] <= '9')) {
        JAST errorMessage = JSONConverter::createErrorMessage(receivedMessage, InvalidParams, "The given name is invalid.");
        sendMessage(errorMessage);
        return;
      }

      Location definitionLocation = JSONConverter::getLocationFromJSON(receivedMessage);
      bool isDefinitionFound = false;
      std::vector<Location> references;
      astree.findReferences(definitionLocation, isDefinitionFound, references);
      if (isDefinitionFound) {
        references.push_back(definitionLocation);
      }
      JAST workspaceEditsJSON = JSONConverter::workspaceEditsToJSON(receivedMessage, references, newName);
      sendMessage(workspaceEditsJSON);
    }

    void didOpen(const JAST &_) {
      // no refresh should be needed
    }

    void didChange(const JAST &receivedMessage) {
      std::string fileUri = receivedMessage.get("params").get("textDocument").get("uri").value;
      std::string fileContent = receivedMessage.get("params").get("contentChanges").children.back().second.get("text").value;
      std::string fileName = JSONConverter::decodePath(fileUri);
      astree.changedFiles[fileName] = std::unique_ptr<StringFile>(new StringFile(fileName.c_str(), std::move(fileContent)));
      needsUpdate = true;
      ignoredCount = 0;
    }

    void didSave(const JAST &receivedMessage) {
      std::string fileUri = receivedMessage.get("params").get("textDocument").get("uri").value;
      astree.changedFiles.erase(JSONConverter::decodePath(fileUri));

      // Might have replaced a file modified on disk
      needsUpdate = true;
      refresh("file-save");
    }

    void didClose(const JAST &receivedMessage) {
      std::string fileUri = receivedMessage.get("params").get("textDocument").get("uri").value;
      if (astree.changedFiles.erase(JSONConverter::decodePath(fileUri)) > 0) {
        needsUpdate = true;
        // If a user hits 'undo' on a symbol rename, you can get hundreds of sequential didClose invocations
        // Calling refresh here would cause the extension to 'hang' for a very long time.
      }
    }

    void didChangeWatchedFiles(const JAST &receivedMessage) {
      JAST jfiles = receivedMessage.get("params").get("changes");
      size_t changed = 0;
      for (auto child: jfiles.children) {
        std::string fileUri = child.second.get("uri").value;
        changed += astree.changedFiles.erase(JSONConverter::decodePath(fileUri));
      }
      if (changed) {
        needsUpdate = true;
        refresh("watch-list-updated");
      }
    }

    void shutdown(const JAST &receivedMessage) {
      JAST message = JSONConverter::createResponseMessage(receivedMessage);
      message.add("result", JSON_NULLVAL);
      isShutDown = true;
      sendMessage(message);
    }

    void serverExit(const JAST &_) {
      exit(isShutDown ? 0 : 1);
    }
};


int main(int argc, const char **argv) {
  std::string stdLib;
  if (argc >= 2) {
    stdLib = make_canonical(argv[1]);
  } else {
    stdLib = make_canonical(find_execpath() + "/../../share/wake/lib");
  }

  LSPServer lsp;
  if (is_readable((stdLib + "/core/boolean.wake").c_str())) {
    lsp = LSPServer(stdLib);
  } else {
    lsp = LSPServer(false, stdLib);
  }
  // Process requests until something goes wrong
  lsp.processRequests();
}
