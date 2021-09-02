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
#include <unistd.h>
#include <string.h>

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
#include <algorithm>

#include "util/location.h"
#include "util/execpath.h"
#include "util/file.h"
#include "util/diagnostic.h"
#include "json/json5.h"
#include "parser/cst.h"
#include "parser/wakefiles.h"
#include "parser/lexer.h"
#include "parser/parser.h"
#include "dst/bind.h"
#include "dst/todst.h"
#include "dst/expr.h"

#ifndef VERSION
#include "../src/version.h"
#endif

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define VERSION_STR TOSTRING(VERSION)

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>

EM_ASYNC_JS(char *, nodejs_getstdin, (), {
  var buffer = "";

  let eof = await new Promise(resolve => {
    let timeout = setTimeout(() => {
      complete(false);
    }, 1000);
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

static CPPFile cppFile(__FILE__);

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

const char *term_colour(int code) { return ""; }
const char *term_normal()         { return ""; }

class LSP {
public:
    explicit LSP(std::string _stdLib) : stdLib(std::move(_stdLib)) {}

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
          sendErrorMessage(ParseError, parseErrors.str());
        } else {
          const std::string &method = request.get("method").value;
          if (!isInitialized && (method != "initialize")) {
            sendErrorMessage(request, ServerNotInitialized, "Must request initialize first");
          } else if (isShutDown && (method != "exit")) {
            sendErrorMessage(request, InvalidRequest, "Received a request other than 'exit' after a shutdown request.");
          } else if (!method.empty()) {
            callMethod(method, request);
          }
        }
      }
    }

private:
    typedef void (LSP::*LspMethod)(JAST);

    std::string rootUri = "";
    bool isInitialized = false;
    bool isCrashed = false;
    std::string crashedFlagFilename = ".lsp-wake.lock";
    bool isShutDown = false;
    std::string stdLib;
    std::map<std::string, std::unique_ptr<FileContent>> files;
    struct Use {
        Location use;
        Location def;
        Use(Location _use, Location _def) : use(_use), def(_def) {}
    };
    std::vector<Use> uses;
    enum SymbolKind { KIND_PACKAGE = 4, KIND_FUNCTION = 12, KIND_VARIABLE = 13, KIND_STRING = 15, KIND_NUMBER = 16,
      KIND_BOOLEAN = 17, KIND_ARRAY = 18, KIND_ENUM_MEMBER = 22, KIND_OPERATOR = 25 };
    struct Definition {
        std::string name;
        Location location;
        std::string type;
        SymbolKind symbolKind;
        bool isGlobal;
        std::string documentation;

        Definition(std::string _name, Location _location, std::string _type, SymbolKind _symbolKind, bool _isGlobal) :
          name(std::move(_name)), location(_location), type(std::move(_type)), symbolKind(_symbolKind),
          isGlobal(_isGlobal) {}

        bool operator < (const Definition &def) const {
          return location < def.location;
        }
    };
    std::vector<Definition> definitions;
    std::vector<Definition> packages;
    std::vector<std::pair<Location, std::string>> comments;
    std::map<std::string, LspMethod> essentialMethods = {
      {"initialize",                      &LSP::initialize},
      {"initialized",                     &LSP::initialized},
      {"textDocument/didOpen",            &LSP::didOpen},
      {"textDocument/didChange",          &LSP::didChange},
      {"textDocument/didSave",            &LSP::didSave},
      {"textDocument/didClose",           &LSP::didClose},
      {"workspace/didChangeWatchedFiles", &LSP::didChangeWatchedFiles},
      {"shutdown",                        &LSP::shutdown},
      {"exit",                            &LSP::serverExit}
    };
    std::map<std::string, LspMethod> additionalMethods = {
      {"textDocument/definition",         &LSP::goToDefinition},
      {"textDocument/references",         &LSP::findReportReferences},
      {"textDocument/documentHighlight",  &LSP::highlightOccurrences},
      {"textDocument/hover",              &LSP::hover},
      {"textDocument/documentSymbol",     &LSP::documentSymbol},
      {"workspace/symbol",                &LSP::workspaceSymbol},
      {"textDocument/rename",             &LSP::rename}
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
          std::cerr << "Client did not shutdown cleanly" << std::endl;
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
        tv.tv_sec = 1;
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
          std::cerr << "Client did not shutdown cleanly" << std::endl;
          exit(1);
        }

        return std::string(&buf[0], got);
#endif
      }
    }

    void poll() {
#ifdef __EMSCRIPTEN__
      int usage = EM_ASM_INT({ return HEAPU8.length; });
      std::cerr << "One second expired; using " << usage << " bytes of memory" << std::endl;
#else
      std::cerr << "One second expired" << std::endl;
#endif
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
          sendErrorMessage(request, MethodNotFound, "Method '" + method + "' is not implemented.");
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

    static JAST createMessage() {
      JAST message(JSON_OBJECT);
      message.add("jsonrpc", "2.0");
      return message;
    }

    static JAST createResponseMessage() {
      JAST message = createMessage();
      message.add("id", 0);
      return message;
    }

    static JAST createRequestMessage() {
      JAST message = createMessage();
      message.add("id", 0);
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

    static JAST createInitializeResultDefault(const JAST &receivedMessage) {
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

      isCrashed = true;
      return message;
    }

    void initialize(JAST receivedMessage) {
      JAST message(JSON_OBJECT);
      if (access(crashedFlagFilename.c_str(), F_OK) != -1) {
        message = createInitializeResultCrashed(receivedMessage);
      } else {
        message = createInitializeResultDefault(receivedMessage);
        std::ofstream isServerStarted;
        isServerStarted.open(crashedFlagFilename);
      }

      isInitialized = true;
      rootUri = receivedMessage.get("params").get("rootUri").value;
      sendMessage(message);

      if (!isCrashed)
        diagnoseProject();
    }

    void initialized(JAST _) { }

    void registerCapabilities() {
      JAST message = createRequestMessage();
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

    static std::string sanitizeComment(std::string comment) {
      std::size_t comment_from = 0;
      for (std::size_t i = 0; i + 1 < comment.size(); ++i) {
        if (comment[i] == '\n' && comment[i + 1] == '\n') comment_from = i + 2;
      }
      if (comment_from < comment.size()) {
        comment = comment.substr(comment_from);
      }

      std::vector <size_t> commentHashtagIndices;
      if (!comment.empty() && comment[0] == '#')
        commentHashtagIndices.push_back(0);
      for (std::size_t i = 0; i + 1 < comment.size(); ++i) {
        if (comment[i] == '\n' && comment[i + 1] == '#')
          commentHashtagIndices.push_back(i + 1);
      }
      for (std::size_t i = 0; i < commentHashtagIndices.size(); ++i) {
        comment.erase(commentHashtagIndices[i] - i, 1);
      }
      if (!comment.empty() && comment[0] == '\n')
        comment.erase(0, 1);
      return comment;
    }

    void fillDefinitionDocumentationFields() {
      auto definitions_iterator = definitions.begin();
      auto comments_iterator = comments.begin();

      std::string comment;
      Location lastCommentLocation("");

      while (definitions_iterator != definitions.end() && comments_iterator != comments.end()) {
        if (definitions_iterator->location < comments_iterator->first) {
          if (std::strcmp(lastCommentLocation.filename, definitions_iterator->location.filename) != 0) {
            comment = "";
          }
          comment = sanitizeComment(comment);
          definitions_iterator->documentation = comment;
          comment = "";
          ++definitions_iterator;
        } else {
          if (std::strcmp(lastCommentLocation.filename, comments_iterator->first.filename) != 0) {
            comment = "";
          }
          comment += comments_iterator->second;
          lastCommentLocation = comments_iterator->first;
          ++comments_iterator;
        }
      }

      if (definitions_iterator != definitions.end()) {
        definitions_iterator->documentation = comment;
      }
    }

    void diagnoseProject() {
      uses.clear();
      definitions.clear();
      packages.clear();
      comments.clear();

      bool enumok = true;
      auto allFiles = find_all_wakefiles(enumok, true, false, stdLib);

      std::map<std::string, std::vector<Diagnostic>> diagnostics;
      LSPReporter lspReporter(diagnostics);
      reporter = &lspReporter;

      std::unique_ptr<Top> top(new Top);
      top->def_package = "nothing";
      top->body = std::unique_ptr<Expr>(new VarRef(FRAGMENT_CPP_LINE, "Nil@wake"));

      std::map<std::string, std::unique_ptr<FileContent>> newFiles;
      for (auto &file: allFiles) {
        auto it = files.find(file);
        FileContent *fcontent;
        if (it == files.end()) {
          fcontent = new ExternalFile(lspReporter, file.c_str());
          newFiles[file] = std::unique_ptr<FileContent>(fcontent);
        } else {
          fcontent = it->second.get();
          newFiles[file] = std::move(it->second);
        }
        CST cst(*fcontent, lspReporter);
        CSTElement root = cst.root();
        dst_top(cst.root(), *top);

        for (CSTElement topdef = root.firstChildElement(); !topdef.empty(); topdef.nextSiblingElement()) {
          if (topdef.id() == TOKEN_COMMENT || topdef.id() == TOKEN_NL) {
            comments.emplace_back(topdef.location(), topdef.segment().str());
          }
        }
      }
      files = std::move(newFiles);
      flatten_exports(*top);

      for (auto &p : top->packages) {
        for (auto &f: p.second->files) {
          packages.emplace_back(Definition(p.first, f.content->fragment.location(), "Package", KIND_PACKAGE, true));
        }
      }

      PrimMap pmap;
      bool isTreeBuilt = true;
      std::unique_ptr<Expr> root = bind_refs(std::move(top), pmap, isTreeBuilt);

      for (auto &file: allFiles)
        reportFileDiagnostics(file, diagnostics[file]);

      if (root != nullptr)
        explore(root.get(), true);

      std::sort(definitions.begin(), definitions.end());
      fillDefinitionDocumentationFields();
    }

    static SymbolKind getSymbolKind(const char *name, const std::string& type) {
      switch (lex_kind(name)) {
      case OPERATOR:
        return KIND_OPERATOR;
      case UPPER:
        return KIND_ENUM_MEMBER;
      case LOWER:
      default:
        if (type.compare("binary =>@builtin") == 0) {
          return KIND_FUNCTION;
        }
        if (type.compare("String@builtin") == 0 ||
           type.compare("RegExp@builtin") == 0) {
          return KIND_STRING;
        }
        if (type.compare("Integer@builtin") == 0 ||
            type.compare("Double@builtin") == 0) {
          return KIND_NUMBER;
        }
        if (type.compare("Boolean@wake") == 0) {
          return KIND_BOOLEAN;
        }
        if (type.compare(0, 12, "Vector@wake ") == 0) {
          return KIND_ARRAY;
        }
        return KIND_VARIABLE;
      }
    }

    void explore(Expr *expr, bool isGlobal) {
      if (expr->type == &VarRef::type) {
        VarRef *ref = static_cast<VarRef*>(expr);
        if (!ref->fragment.empty() && !ref->target.empty() && (ref->flags & FLAG_AST) != 0) {
          uses.emplace_back(
            /* use location */        ref->fragment.location(),
            /* definition location */ ref->target.location());
        }
      } else if (expr->type == &App::type) {
        App *app = static_cast<App*>(expr);
        explore(app->val.get(), false);
        explore(app->fn.get(), false);
      } else if (expr->type == &Lambda::type) {
        Lambda *lambda = static_cast<Lambda*>(expr);
        if (!lambda->token.empty()) {
          std::stringstream ss;
          ss << lambda->typeVar[0];
          if (lambda->name.find(' ') == std::string::npos) {
            definitions.emplace_back(
              /* name */     lambda->name,
              /* location */ lambda->token.location(),
              /* type */     ss.str(),
              /* kind */     getSymbolKind(lambda->name.c_str(), lambda->typeVar[0].getName()),
              /* global */   isGlobal);
          }
        }
        explore(lambda->body.get(), false);
      } else if (expr->type == &Ascribe::type) {
        Ascribe *ascribe = static_cast<Ascribe*>(expr);
        explore(ascribe->body.get(), false);
      } else if (expr->type == &DefBinding::type) {
        DefBinding *defbinding = static_cast<DefBinding*>(expr);
        for (auto &i : defbinding->val) explore(i.get(), false);
        for (auto &i : defbinding->fun) explore(i.get(), false);
        for (auto &i : defbinding->order) {
          if (!i.second.fragment.empty()) {
            std::stringstream ss;
            SymbolKind symbolKind;
            size_t idx = i.second.index;
            if (idx < defbinding->val.size()) {
              ss << defbinding->val[idx]->typeVar;
              symbolKind = getSymbolKind(i.first.c_str(), defbinding->val[idx]->typeVar.getName());
            } else {
              idx -= defbinding->val.size();
              ss << defbinding->fun[idx]->typeVar;
              symbolKind = getSymbolKind(i.first.c_str(), defbinding->fun[idx]->typeVar.getName());
            }
            if (i.first.find(' ') == std::string::npos ||
            i.first.compare(0, 7, "binary ") == 0 ||
            i.first.compare(0, 6, "unary ") == 0) {
              definitions.emplace_back(
                /* name */     i.first,
                /* location */ i.second.fragment.location(),
                /* type */     ss.str(),
                /* kind */     symbolKind,
                /* global */   isGlobal);
            }
          }
        }
        explore(defbinding->body.get(), isGlobal);
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
      for (auto &file: files) {
        if (fileURI.compare(rootUri.length() + 1, std::string::npos, file.second->filename()) == 0)
          filePtr = file.second->filename();
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

    void findReferences(Location &definitionLocation, bool &isDefinitionFound, std::vector<Location> &references) {
      for (const Use &use: uses) {
        if (use.use.contains(definitionLocation)) {
          definitionLocation = use.def;
          isDefinitionFound = true;
          break;
        }
      }
      if (!isDefinitionFound) {
        for (const Definition &def: definitions) {
          if (def.location.contains(definitionLocation)) {
            definitionLocation = def.location;
            isDefinitionFound = true;
            break;
          }
        }
      }
      if (isDefinitionFound) {
        for (const Use &use: uses) {
          if (use.def.contains(definitionLocation)) {
            references.push_back(use.use);
          }
        }
      }
    }

    void reportReferences(JAST receivedMessage, const std::vector<Location> &references) {
      JAST message = createResponseMessage(std::move(receivedMessage));
      JAST &result = message.add("result", JSON_ARRAY);
      for (const Location &location: references) {
        result.children.emplace_back("", createLocationJSON(location));
      }
      sendMessage(message);
    }

    void findReportReferences(JAST receivedMessage) {
      Location definitionLocation = getLocationFromJSON(receivedMessage);
      bool isDefinitionFound = false;
      std::vector<Location> references;
      findReferences(definitionLocation, isDefinitionFound, references);
      if (isDefinitionFound && receivedMessage.get("params").get("context").get("includeDeclaration").value == "true") {
        references.push_back(definitionLocation);
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
      JAST &result = message.add("result", JSON_OBJECT);

      std::string value;
      for (const Definition& def: hoverInfoPieces) {
        value += "**" + def.name + ": " + def.type + "**\n\n";
        value += def.documentation + "\n\n";
      }
      if (!value.empty()) {
        JAST &contents = result.add("contents",JSON_OBJECT);
        contents.add("kind", "markdown");
        contents.add("value", value.c_str());
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
      symbol.add("name", def.name + ": " + def.type);
      symbol.add("kind", def.symbolKind);
      symbol.children.emplace_back("location", createLocationJSON(def.location));
    }

    void documentSymbol(JAST receivedMessage) {
      std::string fileUri = receivedMessage.get("params").get("textDocument").get("uri").value;
      std::string filePath = fileUri.substr(rootUri.length() + 1, std::string::npos);
      JAST message = createResponseMessage(std::move(receivedMessage));
      JAST &result = message.add("result", JSON_ARRAY);
      for (const Definition &def: definitions) {
        if (def.isGlobal && def.location.filename == filePath) {
          appendSymbolToJSON(def, result);
        }
      }
      for (const Definition &p: packages) {
        if (p.location.filename == filePath) {
          appendSymbolToJSON(p, result);
        }
      }
      sendMessage(message);
    }

    void workspaceSymbol(JAST receivedMessage) {
      std::string query = receivedMessage.get("params").get("query").value;
      JAST message = createResponseMessage(std::move(receivedMessage));
      JAST &result = message.add("result", JSON_ARRAY);
      for (const Definition &def: definitions) {
        if (def.isGlobal && def.name.find(query) != std::string::npos) {
          appendSymbolToJSON(def, result);
        }
      }
      for (const Definition &p: packages) {
        if (p.name.find(query) != std::string::npos) {
          appendSymbolToJSON(p, result);
        }
      }
      sendMessage(message);
    }

    void reportWorkspaceEdits(JAST receivedMessage, const std::vector<Location> &references, const std::string &newName) {
      JAST message = createResponseMessage(std::move(receivedMessage));
      JAST &result = message.add("result", JSON_OBJECT);

      std::map<std::string, JAST> filesEdits;
      for (Location ref: references) {
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
      sendMessage(message);
    }

    void rename(JAST receivedMessage) {
      std::string newName = receivedMessage.get("params").get("newName").value;
      if (newName.find(' ') != std::string::npos || (newName[0] >= '0' && newName[0] <= '9')) {
        sendErrorMessage(receivedMessage, InvalidParams, "The given name is invalid.");
        return;
      }

      Location definitionLocation = getLocationFromJSON(receivedMessage);
      bool isDefinitionFound = false;
      std::vector<Location> references;
      findReferences(definitionLocation, isDefinitionFound, references);
      if (isDefinitionFound) {
        references.push_back(definitionLocation);
      }
      reportWorkspaceEdits(receivedMessage, references, newName);
    }

    void didOpen(JAST _) {
      if (!isCrashed)
        diagnoseProject();
    }

    std::string stripRootUri(const std::string &fileUri) {
      if (fileUri.size() < rootUri.size()+1) return "";
      if (fileUri.compare(0, rootUri.size(), rootUri) != 0) return "";
      if (fileUri[rootUri.size()] != '/') return "";
      return fileUri.substr(rootUri.size()+1);
    }

    void didChange(JAST receivedMessage) {
      std::string fileUri = receivedMessage.get("params").get("textDocument").get("uri").value;
      std::string fileContent = receivedMessage.get("params").get("contentChanges").children.back().second.get("text").value;
      std::string fileName = stripRootUri(fileUri);
      if (fileName.empty()) return;
      files[fileName] = std::unique_ptr<FileContent>(new StringFile(fileName.c_str(), std::move(fileContent)));
      if (!isCrashed)
        diagnoseProject();
    }

    void didSave(JAST receivedMessage) {
      if (isCrashed) {
        registerCapabilities();
        isCrashed = false;
      }

      std::string fileUri = receivedMessage.get("params").get("textDocument").get("uri").value;
      files.erase(stripRootUri(fileUri));

      if (!isCrashed)
        diagnoseProject();
    }

    void didClose(JAST receivedMessage) {
      std::string fileUri = receivedMessage.get("params").get("textDocument").get("uri").value;
      files.erase(stripRootUri(fileUri));
    }

    void didChangeWatchedFiles(JAST receivedMessage) {
      JAST jfiles = receivedMessage.get("params").get("changes");
      for (auto child: jfiles.children) {
        std::string fileUri = child.second.get("uri").value;
        files.erase(stripRootUri(fileUri));
      }
      if (!isCrashed)
        diagnoseProject();
    }

    void shutdown(JAST receivedMessage) {
      JAST message = createResponseMessage(std::move(receivedMessage));
      message.add("result", JSON_NULLVAL);
      isShutDown = true;
      sendMessage(message);
      std::remove(crashedFlagFilename.c_str());
    }

    void serverExit(JAST _) {
      exit(isShutDown ? 0 : 1);
    }
};


int main(int argc, const char **argv) {
  std::string stdLib;
  if (argc >= 2) {
    stdLib = argv[1];
  } else {
    stdLib = find_execpath() + "/../../share/wake/lib";
  }

#ifdef __EMSCRIPTEN__
  EM_ASM({
    FS.mkdir('/workspace');
    FS.mkdir('/stdlib');
    FS.mount(NODEFS, { root: '.' }, '/workspace');
    FS.mount(NODEFS, { root: UTF8ToString($0) }, '/stdlib');
  }, stdLib.c_str());
  chdir("/workspace");
  stdLib = "/stdlib";
#endif

  if (access((stdLib + "/core/boolean.wake").c_str(), F_OK) != -1) {
    LSP lsp(stdLib);
    // Process requests until something goes wrong
    lsp.processRequests();
  } else {
    std::cerr << "Path to the wake standard library (" << stdLib << ") is invalid. Server will not be initialized." << std::endl;
    return 1;
  }
}
