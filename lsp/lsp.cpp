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

#include <sys/select.h>
#include <cstring>

#include <iostream>
#include <string>
#include <map>
#include <sstream>
#include <fstream>
#include <chrono>
#include <ctime>
#include <functional>
#include <utility>

#include "json5.h"
#include "location.h"
#include "frontend/parser.h"
#include "frontend/symbol.h"
#include "runtime/runtime.h"
#include "frontend/expr.h"
#include "runtime/sources.h"
#include "frontend/diagnostic.h"
#include "types/bind.h"


#ifndef VERSION
#include "../src/version.h"
#endif

// Number of pipes to the wake subprocess
#define PIPES 5

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define VERSION_STR TOSTRING(VERSION)

// Header used in JSON-RPC
static const char contentLength[] = "Content-Length: ";

// Defined by JSON RPC
static const char *ParseError           = "-32700";
static const char *InvalidRequest       = "-32600";
static const char *MethodNotFound       = "-32601";
//static const char *InvalidParams        = "-32602";
//static const char *InternalError        = "-32603";
//static const char *serverErrorStart     = "-32099";
//static const char *serverErrorEnd       = "-32000";
static const char *ServerNotInitialized = "-32002";
//static const char *UnknownErrorCode     = "-32001";


DiagnosticReporter *reporter;

class LSP {
public:
    LSP() : runtime(nullptr, 0, 4.0, 0) {}

    void processRequests() {
      // Begin log
      std::ofstream clientLog;
      clientLog.open("requests_log.txt", std::ios_base::app); // append instead of overwriting
      std::time_t currentTime = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
      clientLog << std::endl
        << "Log start: " << ctime(&currentTime);

      while (true) {
        size_t json_size = 0;
        // Read header lines until an empty line
        while (true) {
          std::string line;
          std::getline(std::cin, line);
          // Trim trailing CR, if any
          if (!line.empty() && line.back() == '\r')
            line.resize(line.size() - 1);
          // EOF? exit LSP
          if (std::cin.eof())
            exit(0);
          // Failure reading? Fail with non-zero exit status
          if (!std::cin)
            exit(1);
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
        std::string content(json_size, ' ');
        std::cin.read(&content[0], json_size);

        // Log the request
        clientLog << content << std::endl;

        // Parse that content as JSON
        JAST request;
        std::stringstream parseErrors;
        if (!JAST::parse(content, parseErrors, request)) {
          sendErrorMessage(ParseError, parseErrors.str());
        } else {
          const std::string &method = request.get("method").value;
          if (!isInitialized && (method != "initialize")) {
            sendErrorMessage(request, ServerNotInitialized, "Must request initialize first");
          } else if (isShutDown && (method != "exit")) {
            sendErrorMessage(request, InvalidRequest, "Received a request other than 'exit' after a shutdown request.");
          } else {
            callMethod(method, request);
          }
        }
      }
    }

private:
    typedef void (LSP::*LspMethod)(JAST);

    std::string rootUri = "";
    bool isInitialized = false;
    bool isShutDown = false;
    Runtime runtime;
    std::map<std::string, std::string> changedFiles;
    std::map<std::string, std::vector<Diagnostic>> diagnostics;
    std::vector<std::pair<Location, Location>> uses;
    std::vector<std::pair<std::string, Location>> definitions;
    std::map<std::string, LspMethod> methodToFunction = {
      {"initialize",                      &LSP::initialize},
      {"initialized",                     &LSP::initialized},
      {"textDocument/didOpen",            &LSP::didOpen},
      {"textDocument/didChange",          &LSP::didChange},
      {"textDocument/didSave",            &LSP::didSave},
      {"textDocument/didClose",           &LSP::didClose},
      {"workspace/didChangeWatchedFiles", &LSP::didChangeWatchedFiles},
      {"shutdown",                        &LSP::shutdown},
      {"exit",                            &LSP::serverExit},
      {"textDocument/definition",         &LSP::goToDefinition}
    };

    void callMethod(const std::string &method, const JAST &request) {
      auto functionPointer = methodToFunction.find(method);
      if (functionPointer != methodToFunction.end()) {
        (this->*(functionPointer->second))(request);
      } else {
        sendErrorMessage(request, MethodNotFound, "Method '" + method + "' is not implemented.");
      }
    }

    static void sendMessage(const JAST &message) {
      std::stringstream str;
      str << message;
      str.seekg(0, std::ios::end);
      std::cout << contentLength << str.tellg() << "\r\n\r\n";
      str.seekg(0, std::ios::beg);
      std::cout << str.rdbuf();
    }

    static JAST createMessage() {
      JAST message(JSON_OBJECT);
      message.add("jsonrpc", "2.0");
      return message;
    }

    static JAST createResponseMessage() {
      JAST message = createMessage();
      message.add("id", JSON_NULLVAL);
      return message;
    }

    static JAST createResponseMessage(JAST receivedMessage) {
      JAST message = createMessage();
      message.children.emplace_back("id", receivedMessage.get("id"));
      return message;
    }

    static void sendErrorMessage(const char *code, const std::string &message) {
      JAST errorMessage = createResponseMessage();
      JAST &error = errorMessage.add("error", JSON_OBJECT);
      error.add("code", JSON_INTEGER, code);
      error.add("message", message.c_str());
      sendMessage(errorMessage);
    }

    static void sendErrorMessage(const JAST &receivedMessage, const char *code, const std::string &message) {
      JAST errorMessage = createResponseMessage(receivedMessage);
      JAST &error = errorMessage.add("error", JSON_OBJECT);
      error.add("code", JSON_INTEGER, code);
      error.add("message", message.c_str());
      sendMessage(errorMessage);
    }

    static JAST createInitializeResult(const JAST &receivedMessage) {
      JAST message = createResponseMessage(receivedMessage);
      JAST &result = message.add("result", JSON_OBJECT);

      JAST &capabilities = result.add("capabilities", JSON_OBJECT);
      capabilities.add("textDocumentSync", 1);
      capabilities.add("definitionProvider", true);

      JAST &serverInfo = result.add("serverInfo", JSON_OBJECT);
      serverInfo.add("name", "lsp wake server");

      return message;
    }

    void initialize(JAST receivedMessage) {
      JAST message = createInitializeResult(receivedMessage);
      isInitialized = true;
      rootUri = receivedMessage.get("params").get("rootUri").value;
      sendMessage(message);

      diagnoseProject();
    }

    void initialized(JAST _) { }

    static JAST createRangeFromLocation(const Location &location) {
      JAST range(JSON_OBJECT);

      JAST &start = range.add("start", JSON_OBJECT);
      start.add("line", std::max(0, location.start.row - 1));
      start.add("character", std::max(0, location.start.column - 1));

      JAST &end = range.add("end", JSON_OBJECT);
      end.add("line", std::max(0, location.end.row - 1));
      end.add("character", std::max(0, location.end.column)); // It can be -1

      return range;
    }

    static JAST createDiagnostic(const Diagnostic &diagnostic) {
      JAST diagnosticJSON(JSON_OBJECT);

      diagnosticJSON.children.emplace_back("range", createRangeFromLocation(diagnostic.getLocation()));
      diagnosticJSON.add("severity", diagnostic.getSeverity());
      diagnosticJSON.add("source", "wake");

      diagnosticJSON.add("message", diagnostic.getMessage());

      return diagnosticJSON;
    }

    static JAST createDiagnosticMessage() {
      JAST message = createMessage();
      message.add("method", "textDocument/publishDiagnostics");
      return message;
    }

    class LSPReporter : public DiagnosticReporter {
    private:
        std::map<std::string, std::vector<Diagnostic>> &diagnostics;

        void report(Diagnostic diagnostic) override {
          diagnostics[diagnostic.getFilename()].push_back(diagnostic);
        }

    public:
        explicit LSPReporter(std::map<std::string, std::vector<Diagnostic>> &_diagnostics) : diagnostics(
                _diagnostics) {}
    };

    void reportFileDiagnostics(const std::string &filePath, const std::vector<Diagnostic> &fileDiagnostics) const {
      JAST diagnosticsArray(JSON_ARRAY);
      for (const Diagnostic &diagnostic: fileDiagnostics) {
        diagnosticsArray.children.emplace_back("", createDiagnostic(diagnostic)); // add .add for JSON_OBJECT to JSON_ARRAY
      }
      JAST message = createDiagnosticMessage();
      JAST &params = message.add("params", JSON_OBJECT);
      std::string fileUri = rootUri + '/' + filePath;
      params.add("uri", fileUri.c_str());
      params.children.emplace_back("diagnostics", diagnosticsArray);
      sendMessage(message);
    }

    void runSyntaxChecker(const std::string &filePath, Top &top) {
      LSPReporter lspReporter(diagnostics);
      reporter = &lspReporter;

      std::string fileUri = rootUri + '/' + filePath;
      auto fileChangesPointer = changedFiles.find(fileUri);
      if (fileChangesPointer != changedFiles.end()) {
        Lexer lex(runtime.heap, (*fileChangesPointer).second, filePath.c_str());
        parse_top(top, lex);
      } else {
        Lexer lex(runtime.heap, filePath.c_str());
        parse_top(top, lex);
      }
    }

    void diagnoseProject() {
      uses.clear();
      definitions.clear();
      std::unique_ptr<Top> top(new Top);
      bool enumok = true;
      auto wakefiles = find_all_wakefiles(enumok, true, false);

      std::vector<std::string> filenames; // needed to keep file names alive for bind_refs()
      filenames.reserve(wakefiles.size());
      for (const std::string &filePath : wakefiles) {
        filenames.push_back(filePath);
        runSyntaxChecker(filenames.back(), *top);
      }
      LSPReporter lspReporter(diagnostics);
      reporter = &lspReporter;

      top->def_package = "nothing";
      top->body = std::unique_ptr<Expr>(new VarRef(LOCATION, "Nil@wake"));
      PrimMap pmap = prim_register_all(nullptr, nullptr);
      std::unique_ptr<Expr> root = bind_refs(std::move(top), pmap);

      for (const std::string &filePath : wakefiles) {
        auto fileDiagnostics = diagnostics[filePath];
        reportFileDiagnostics(filePath, fileDiagnostics);
      }
      diagnostics.clear();

      if (root != nullptr) {
        explore(root.get());
      }
    }

    void explore(Expr *expr) {
      if (expr->type == &VarRef::type) {
        VarRef *ref = static_cast<VarRef*>(expr);
        if (ref->location.start.bytes >= 0 && ref->target.start.bytes >= 0) {
          uses.emplace_back(ref->location /* use location */, ref->target /* definition location */);
        }
      } else if (expr->type == &App::type) {
        App *app = static_cast<App*>(expr);
        explore(app->val.get());
        explore(app->fn.get());
      } else if (expr->type == &Lambda::type) {
        Lambda *lambda = static_cast<Lambda*>(expr);
        if (lambda->token.start.bytes >= 0) {
          definitions.emplace_back(lambda->name /* name */, lambda->token /* location */);
        }
        explore(lambda->body.get());
      } else if (expr->type == &Ascribe::type) {
        Ascribe *ascribe = static_cast<Ascribe*>(expr);
        explore(ascribe->body.get());
      } else if (expr->type == &DefBinding::type) {
        DefBinding *defbinding = static_cast<DefBinding*>(expr);
        for (auto &i : defbinding->val) explore(i.get());
        for (auto &i : defbinding->fun) explore(i.get());
        for (auto &i : defbinding->order) {
          if (i.second.location.start.bytes >= 0) {
            if (i.first.compare(0, 6, "topic ") == 0) {
              // noop
            } else if (i.first.compare(0, 8, "publish ") != 0) {
              // noop
            } else {
              definitions.emplace_back(i.first /* name */, i.second.location /* location */);
            }
          }
        }
        explore(defbinding->body.get());
      }
    }

    void didOpen(JAST receivedMessage) {
      std::string fileUri = receivedMessage.get("params").get("textDocument").get("uri").value;
      diagnoseProject();
    }

    void didChange(JAST receivedMessage) {
      std::string fileUri = receivedMessage.get("params").get("textDocument").get("uri").value;
      std::string fileContent = receivedMessage.get("params").get("contentChanges").children.back().second.get("text").value;
      changedFiles[fileUri] = fileContent;
      diagnoseProject();
    }

    void reportDefinitionLocation(JAST receivedMessage, const Location &definitionLocation) {
      JAST message = createResponseMessage(std::move(receivedMessage));
      JAST &result = message.add("result", JSON_OBJECT);

      std::string fileUri = rootUri + '/' + definitionLocation.filename;
      result.add("uri", fileUri.c_str());
      result.children.emplace_back("range", createRangeFromLocation(definitionLocation));

      sendMessage(message);
    }

    static void reportNoDefinition(JAST receivedMessage) {
      JAST message = createResponseMessage(std::move(receivedMessage));
      JAST result = message.add("result", JSON_NULLVAL);
      sendMessage(message);
    }

    void goToDefinition(JAST receivedMessage) {
      std::string fileUri = receivedMessage.get("params").get("textDocument").get("uri").value;
      std::string filePath = fileUri.substr(rootUri.length() + 1, std::string::npos);

      int row = stoi(receivedMessage.get("params").get("position").get("line").value) + 1;
      int column = stoi(receivedMessage.get("params").get("position").get("character").value) + 1;
      Location locationToDefine(filePath.c_str(), Coordinates(row, column), Coordinates(row, column));

      for (const std::pair<Location, Location>& use: uses) {
        if (use.first.contains(locationToDefine)) {
          reportDefinitionLocation(receivedMessage, use.second);
          return;
        }
      }
      reportNoDefinition(receivedMessage);
    }

    void didSave(JAST receivedMessage) {
      std::string fileUri = receivedMessage.get("params").get("textDocument").get("uri").value;
      changedFiles.erase(fileUri);
      diagnoseProject();
    }

    void didClose(JAST receivedMessage) {
      std::string fileUri = receivedMessage.get("params").get("textDocument").get("uri").value;
      changedFiles.erase(fileUri);
    }

    void didChangeWatchedFiles(JAST receivedMessage) {
      JAST files = receivedMessage.get("params").get("changes");
      for (auto child: files.children) {
        std::string fileUri = child.second.get("uri").value;
        changedFiles.erase(fileUri);
        diagnoseProject();
      }
    }

    void shutdown(JAST receivedMessage) {
      JAST message = createResponseMessage(std::move(receivedMessage));
      message.add("result", JSON_NULLVAL);
      isShutDown = true;
      sendMessage(message);
    }

    void serverExit(JAST _) {
      exit(isShutDown ? 0 : 1);
    }
};


int main() {
  LSP lsp;
  // Process requests until something goes wrong 
  lsp.processRequests();
}
