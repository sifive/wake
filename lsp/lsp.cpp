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
#include <unistd.h>

#include "json5.h"
#include "location.h"
#include "frontend/parser.h"
#include "frontend/symbol.h"
#include "runtime/runtime.h"
#include "frontend/expr.h"
#include "runtime/sources.h"
#include "frontend/diagnostic.h"
#include "types/bind.h"
#include "execpath.h"
#include "frontend/wakefiles.h"


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
    explicit LSP(std::string _stdLib) : stdLib(std::move(_stdLib)), runtime(nullptr, 0, 4.0, 0) {}

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
    std::string stdLib;
    Runtime runtime;
    std::vector<std::string> allFiles;
    std::map<std::string, std::string> changedFiles;
    struct Use {
        Location use;
        Location def;
        Use(Location _use, Location _def) : use(_use), def(_def) {}
    };
    std::vector<Use> uses;
    struct Definition {
        std::string name;
        Location location;
        std::string type;
        Definition(std::string _name, Location _location, std::string _type) : name(std::move(_name)),
                                                                               location(_location),
                                                                               type(std::move(_type)) {}
    };
    std::vector<Definition> definitions;
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
      {"textDocument/definition",         &LSP::goToDefinition},
      {"textDocument/references",         &LSP::findReferences},
      {"textDocument/documentHighlight",  &LSP::highlightOccurrences},
      {"textDocument/hover",              &LSP::hover},
      {"textDocument/documentSymbol",     &LSP::documentSymbol},
      {"workspace/symbol",                &LSP::workspaceSymbol}
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
      capabilities.add("referencesProvider", true);
      capabilities.add("documentHighlightProvider", true);
      capabilities.add("hoverProvider", true);
      capabilities.add("documentSymbolProvider", true);
      capabilities.add("workspaceSymbolProvider", true);

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
      auto fileChangesPointer = changedFiles.find(rootUri + '/' + filePath);
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

      bool enumok = true;
      allFiles = find_all_wakefiles(enumok, true, false, stdLib);

      std::map<std::string, std::vector<Diagnostic>> diagnostics;
      LSPReporter lspReporter(diagnostics);
      reporter = &lspReporter;

      std::unique_ptr<Top> top(new Top);
      top->def_package = "nothing";
      top->body = std::unique_ptr<Expr>(new VarRef(LOCATION, "Nil@wake"));

      for (auto &file: allFiles)
        runSyntaxChecker(file, *top);

      PrimMap pmap = prim_register_all(nullptr, nullptr);
      bool isTreeBuilt = true;
      std::unique_ptr<Expr> root = bind_refs(std::move(top), pmap, isTreeBuilt);

      for (auto &file: allFiles)
        reportFileDiagnostics(file, diagnostics[file]);

      if (root != nullptr)
        explore(root.get());
    }

    void explore(Expr *expr) {
      if (expr->type == &VarRef::type) {
        VarRef *ref = static_cast<VarRef*>(expr);
        if (ref->location.start.bytes >= 0 && ref->target.start.bytes >= 0 && (ref->flags & FLAG_AST) != 0) {
          uses.emplace_back(ref->location /* use location */, ref->target /* definition location */);
        }
      } else if (expr->type == &App::type) {
        App *app = static_cast<App*>(expr);
        explore(app->val.get());
        explore(app->fn.get());
      } else if (expr->type == &Lambda::type) {
        Lambda *lambda = static_cast<Lambda*>(expr);
        if (lambda->token.start.bytes >= 0) {
          std::stringstream ss;
          ss << lambda->typeVar[0];
          if (lambda->name.find(' ') == std::string::npos)
            definitions.emplace_back(lambda->name /* name */, lambda->token /* location */, ss.str() /* type */);
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
            std::stringstream ss;
            size_t idx = i.second.index;
            if (idx < defbinding->val.size()) {
              ss << defbinding->val[idx]->typeVar;
            } else {
              idx -= defbinding->val.size();
              ss << defbinding->fun[idx]->typeVar;
            }
            if (i.first.find(' ') == std::string::npos ||
                i.first.compare(0, 7, "binary ") == 0 ||
                i.first.compare(0, 6, "unary ") == 0)
              definitions.emplace_back(i.first /* name */, i.second.location /* location */, ss.str() /* type */);
          }
        }
        explore(defbinding->body.get());
      }
    }

    JAST createLocationJSON(Location location) {
      JAST locationJSON(JSON_OBJECT);
      std::string fileUri = rootUri + '/' + location.filename;
      locationJSON.add("uri", fileUri.c_str());
      locationJSON.children.emplace_back("range", createRangeFromLocation(location));
      return locationJSON;
    }

    void reportDefinitionLocation(JAST receivedMessage, const Location &definitionLocation) {
      JAST message = createResponseMessage(std::move(receivedMessage));
      message.children.emplace_back("result", createLocationJSON(definitionLocation));
      sendMessage(message);
    }

    static void reportNoDefinition(JAST receivedMessage) {
      JAST message = createResponseMessage(std::move(receivedMessage));
      JAST result = message.add("result", JSON_NULLVAL);
      sendMessage(message);
    }

    const char *findURI(const std::string &fileURI) {
      const char *filePtr = nullptr;
      for (auto &file: allFiles) {
        if (fileURI.compare(rootUri.length() + 1, std::string::npos, file) == 0)
          filePtr = file.c_str();
      }
      return filePtr;
    }

    Location getLocationFromJSON(JAST receivedMessage) {
      std::string fileURI = receivedMessage.get("params").get("textDocument").get("uri").value;
      std::string filePath = fileURI.substr(rootUri.length() + 1, std::string::npos);

      int row = stoi(receivedMessage.get("params").get("position").get("line").value);
      int column = stoi(receivedMessage.get("params").get("position").get("character").value);
      return{findURI(fileURI), Coordinates(row + 1, column + 1), Coordinates(row + 1, column)};
    }

    void goToDefinition(JAST receivedMessage) {
      Location locationToDefine = getLocationFromJSON(receivedMessage);
      for (const Use &use: uses) {
        if (use.use.contains(locationToDefine)) {
          reportDefinitionLocation(receivedMessage, use.def);
          return;
        }
      }
      for (const Definition &def: definitions) {
        if (def.location.contains(locationToDefine)) {
          reportDefinitionLocation(receivedMessage, def.location);
          return;
        }
      }
      reportNoDefinition(receivedMessage);
    }

    void reportReferences(JAST receivedMessage, const std::vector<Location> &references) {
      JAST message = createResponseMessage(std::move(receivedMessage));
      if (references.empty()) {
        JAST result = message.add("result", JSON_NULLVAL);
        sendMessage(message);
        return;
      }

      JAST &result = message.add("result", JSON_ARRAY);
      for (const Location &location: references) {
        result.children.emplace_back("", createLocationJSON(location));
      }
      sendMessage(message);
    }

    void findReferences(JAST receivedMessage) {
      Location symbolLocation = getLocationFromJSON(receivedMessage);
      Location definitionLocation = symbolLocation;
      bool isDefinitionFound = false;

      for (const Use &use: uses) {
        if (use.use.contains(symbolLocation)) {
          definitionLocation = use.def;
          isDefinitionFound = true;
          break;
        }
      }
      if (!isDefinitionFound) {
        for (const Definition &def: definitions) {
          if (def.location.contains(symbolLocation)) {
            definitionLocation = def.location;
            isDefinitionFound = true;
            break;
          }
        }
      }
      std::vector<Location> references;
      if (isDefinitionFound) {
        for (const Use &use: uses) {
          if (use.def.contains(definitionLocation)) {
            references.push_back(use.use);
          }
        }

        if (receivedMessage.get("params").get("context").get("includeDeclaration").value == "true") {
          references.push_back(definitionLocation);
        }
      }
      reportReferences(receivedMessage, references);
    }

    static JAST createDocumentHighlightJSON(Location location) {
      JAST documentHighlightJSON(JSON_OBJECT);
      documentHighlightJSON.children.emplace_back("range", createRangeFromLocation(location));
      return documentHighlightJSON;
    }

    static void reportHighlights(JAST receivedMessage, const std::vector<Location> &occurrences) {
      JAST message = createResponseMessage(std::move(receivedMessage));
      if (occurrences.empty()) {
        JAST result = message.add("result", JSON_NULLVAL);
        sendMessage(message);
        return;
      }

      JAST &result = message.add("result", JSON_ARRAY);
      for (const Location &location: occurrences) {
        result.children.emplace_back("", createDocumentHighlightJSON(location));
      }
      sendMessage(message);
    }

    void highlightOccurrences(JAST receivedMessage) {
      Location symbolLocation = getLocationFromJSON(receivedMessage);
      Location definitionLocation = symbolLocation;
      bool isDefinitionFound = false;

      for (const Use &use: uses) {
        if (use.use.contains(symbolLocation)) {
          definitionLocation = use.def;
          isDefinitionFound = true;
          break;
        }
      }
      if (!isDefinitionFound) {
        for (const Definition &def: definitions) {
          if (def.location.contains(symbolLocation)) {
            definitionLocation = def.location;
            isDefinitionFound = true;
            break;
          }
        }
      }
      std::vector<Location> occurrences;
      if (isDefinitionFound) {
        for (const Use &use: uses) {
          if (use.use.filename == symbolLocation.filename && use.def.contains(definitionLocation)) {
            occurrences.push_back(use.use);
          }
        }
        if (definitionLocation.filename == symbolLocation.filename) {
          occurrences.push_back(definitionLocation);
        }
      }
      reportHighlights(receivedMessage, occurrences);
    }

    static void reportHoverInfo(JAST receivedMessage, const std::vector<Definition> &hoverInfoPieces) {
      JAST message = createResponseMessage(std::move(receivedMessage));
      if (hoverInfoPieces.empty()) {
        JAST result = message.add("result", JSON_NULLVAL);
        sendMessage(message);
        return;
      }

      JAST &result = message.add("result", JSON_OBJECT);
      JAST &contents = result.add("contents", JSON_ARRAY);
      for (const Definition& def: hoverInfoPieces) {
        contents.add((def.name + ": " + def.type).c_str());
      }
      sendMessage(message);
    }

    void hover(JAST receivedMessage) {
      Location symbolLocation = getLocationFromJSON(receivedMessage);
      Location definitionLocation = symbolLocation;

      for (const Use &use: uses) {
        if (use.use.contains(symbolLocation)) {
          definitionLocation = use.def;
          break;
        }
      }
      std::vector<Definition> hoverInfoPieces;
      for (Definition &def: definitions) {
        if (def.location.contains(definitionLocation)) {
          hoverInfoPieces.push_back(def);
        }
      }
      reportHoverInfo(receivedMessage, hoverInfoPieces);
    }

    void appendSymbolToJSON(const Definition& def, JAST &json) {
      JAST &symbol = json.add("", JSON_OBJECT);
      symbol.add("name", def.name.c_str());
      symbol.add("kind", 13); // Variable
      symbol.children.emplace_back("location", createLocationJSON(def.location));
    }

    void documentSymbol(JAST receivedMessage) {
      std::string fileUri = receivedMessage.get("params").get("textDocument").get("uri").value;
      std::string filePath = fileUri.substr(rootUri.length() + 1, std::string::npos);
      JAST message = createResponseMessage(std::move(receivedMessage));
      JAST &result = message.add("result", JSON_ARRAY);
      for (const Definition &def: definitions) {
        if (def.location.filename == filePath) {
          appendSymbolToJSON(def, result);
        }
      }
      sendMessage(message);
    }

    void workspaceSymbol(JAST receivedMessage) {
      std::string query = receivedMessage.get("params").get("query").value;
      JAST message = createResponseMessage(std::move(receivedMessage));
      JAST &result = message.add("result", JSON_ARRAY);
      for (const Definition &def: definitions) {
        if (def.name.find(query) != std::string::npos) {
          appendSymbolToJSON(def, result);
        }
      }
      sendMessage(message);
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
      }
      diagnoseProject();
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


int main(int argc, const char **argv) {
  std::string stdLib;
  if (argc == 2) {
    stdLib = argv[1];
  } else {
    stdLib = find_execpath() + "/../../share/wake/lib";
  }
  if (access((stdLib + "/core/boolean.wake").c_str(), F_OK) != -1) {
    LSP lsp(stdLib);
    // Process requests until something goes wrong
    lsp.processRequests();
  } else {
    std::cerr << "Path to the wake standard library is invalid. Server will not be initialized." << std::endl;
    return 1;
  }
}
