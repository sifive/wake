/* Wake Build Server Protocol implementation
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

#include <iostream>
#include <string>
#include <map>
#include <sstream>
#include <fstream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>

#include "json5.h"
#include "execpath.h"

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
// Prefix for commands dispatched to the filesystem / wake
static const char buildTarget[] = "buildTarget/";
// URI-Prefix we replace with file:/
static const char workspace[] = "workspace://";
// URI-Prefix we strip from targets
static const char bsp[] = "bsp://";

// Defined by JSON RPC
static const char *ParseError           = "-32700";
//static const char *InvalidRequest       = "-32600";
static const char *MethodNotFound       = "-32601";
static const char *InvalidParams        = "-32602";
static const char *InternalError        = "-32603";
//static const char *serverErrorStart     = "-32099";
//static const char *serverErrorEnd       = "-32000";
static const char *ServerNotInitialized = "-32002";
//static const char *UnknownErrorCode     = "-32001";

// Defined by the BSP.
//static const char *RequestCancelled = "-32800";
//static const char *ContentModified  = "-32801";

// We need this when remapping paths
static std::string rooturi;

static void sendMessage(const JAST &message) {
  std::stringstream str;
  str << message;
  str.seekg(0, std::ios::end);
  std::cout << contentLength << str.tellg() << "\r\n\r\n";
  str.seekg(0, std::ios::beg);
  std::cout << str.rdbuf();
}

static bool initialize(JAST &response, const JAST &params) {
  // Wake BSP supports all languages, so just echo back their requested languages
  const JAST &langs = params.get("capabilities").get("languageIds");
  const std::string &uri = params.get("rootUri").value;

  bool ok = true;
  if (ok) ok = 0 == uri.compare(0, 7, "file://");
  if (ok) ok = 0 == chdir(uri.c_str() + 7);
  if (ok) ok = 0 == access("wake.db", W_OK);

  if (!ok) {
    JAST &error = response.add("error", JSON_OBJECT);
    error.add("code", JSON_INTEGER, InvalidParams);
    error.add("message", "Could not open wake.db read-write in " + uri);
  } else {
    rooturi = uri;
    JAST &result = response.add("result", JSON_OBJECT);
    result.add("displayName", "wake");
    result.add("version", VERSION_STR);
    result.add("bspVersion", "2.0.0-M5");
    JAST &caps = result.add("capabilities", JSON_OBJECT);
    caps.add("compileProvider", JSON_OBJECT).children.emplace_back("languageIds", langs);
    caps.add("testProvider",    JSON_OBJECT).children.emplace_back("languageIds", langs);
    caps.add("runProvider",     JSON_OBJECT).children.emplace_back("languageIds", langs);
    caps.add("dependencySourcesProvider", JSON_TRUE); // can supply sources for external libraries
  }

  return ok;
}

struct ExecuteWakeProcess {
  JAST result;
  JAST error;
  std::vector<std::string> cmdline;
  int status;

  ExecuteWakeProcess();
  virtual void gotLine(JAST &row) = 0;

  void errorMessage(std::string &&message);
  void errorPrefix(std::string &&message);

  void execute();
  void executeLine(int i, std::string &&line);
};

static bool quiet = true;

ExecuteWakeProcess::ExecuteWakeProcess() : result(JSON_OBJECT), error(JSON_NULLVAL) {
  std::string myDir = find_execpath();
  cmdline.push_back(myDir + "/../../bin/wake");
  cmdline.push_back("--quiet");
  cmdline.push_back("--stdout=bsp");
  cmdline.push_back("--stderr=error");
  cmdline.push_back("--fd:3=warning");
  if (quiet) {
    cmdline.push_back("--fd:4=info");
  } else {
    cmdline.push_back("--fd:4=info,echo");
    cmdline.push_back("--fd:5=debug");
  }
}

void ExecuteWakeProcess::errorMessage(std::string &&message) {
  if (error.kind == JSON_NULLVAL) {
    error.kind = JSON_OBJECT;
    error.add("code", JSON_INTEGER, InternalError);
    error.add("message", std::move(message));
  }
}

void ExecuteWakeProcess::errorPrefix(std::string &&message) {
  errorMessage(message + ": " + strerror(errno));
}

// Yes, reading line-by-line without blocking from a subprocess really is this hard in C.
void ExecuteWakeProcess::execute() {
  int pipefds[PIPES][2];
  for (int i = 0; i < PIPES; ++i) {
    if (pipe(&pipefds[i][0]) != 0) {
      errorPrefix("pipe");
      return;
    }
  }

  pid_t child = fork();
  if (child == 0) {
    // Close the reading side of the pipe
    for (int i = 0; i < PIPES; ++i)
      if (close(pipefds[i][0]) != 0)
        exit(42);

    // We need to dup2 pipefds[i][0,PIPES) to [1,PIPES]
    // Unfortunately, some of the these might be in the way
    // dup() any of them that are in the way, out of the way
    for (int i = 0; i < PIPES; ++i)
      while (pipefds[i][1] >= 1 && pipefds[i][1] <= PIPES)
        if ((pipefds[i][1] = dup(pipefds[i][1])) == -1)
          exit(43);

    // Put the pipes in their proper final position
    // This also closes any transient descriptors from dup() above
    for (int i = 0; i < PIPES; ++i) {
      if (dup2(pipefds[i][1], i+1) == -1) exit(44);
      if (close(pipefds[i][1]) != 0) exit(45);
    }

    // Launch subprocess with specified arguments
    std::vector<char*> args;
    for (auto &arg : cmdline)
      args.push_back(const_cast<char*>(arg.c_str()));
    args.push_back(0);
    execv(cmdline[0].c_str(), args.data());

    exit(46);
  }

  if (child == -1) errorPrefix("fork");
  for (int i = 0; i < PIPES; ++i)
    if (close(pipefds[i][1]) != 0)
       errorPrefix("close1");

  // Gather output from wake here
  std::string buffer[PIPES];
  char block[4096];

  while (true) {
    fd_set rfds;
    FD_ZERO(&rfds);
    int nfds = 0;

    for (int i = 0; i < PIPES; ++i) {
      if (pipefds[i][0] == -1) continue;
      FD_SET(pipefds[i][0], &rfds);
      if (pipefds[i][0] >= nfds) nfds = pipefds[i][0]+1;
    }

    if (nfds == 0) break;
    int ret = select(nfds, &rfds, 0, 0, 0);
    if (ret <= 0) { errorPrefix("select"); break; }

    ssize_t got;
    for (int i = 0; i < PIPES; ++i) {
      if (pipefds[i][0] == -1) continue;
      if (!FD_ISSET(pipefds[i][0], &rfds)) continue;
      got = read(pipefds[i][0], &block[0], sizeof(block));

      if (got > 0) {
        buffer[i].append(&block[0], got);
        size_t start = 0, end;
        while ((end = buffer[i].find_first_of('\n', start)) != std::string::npos) {
          executeLine(i, buffer[i].substr(start, end-start));
          start = end+1;
        }
        buffer[i].erase(buffer[i].begin(), buffer[i].begin() + start);
      } else { // got <= 0
        if (got < 0) errorPrefix("read");
        if (close(pipefds[i][0]) != 0) errorPrefix("close");
        pipefds[i][0] = -1;
        if (got < 0) errorPrefix("read");
      }
    }
  }

  for (int i = 0; i < PIPES; ++i) {
    if (!buffer[i].empty()) {
      executeLine(i, std::move(buffer[i]));
    }
  }

  if (waitpid(child, &status, 0) == -1) errorPrefix("waitpid");
}

void ExecuteWakeProcess::executeLine(int i, std::string &&line) {
  if (i == 0) {
    JAST json;
    std::stringstream parseErrors;
    if (JAST::parse(line, parseErrors, json)) {
      gotLine(json);
    } else {
      errorPrefix("failed to parse wake output: " + parseErrors.str());
    }
  } else {
    const char *code = "";
    switch (i) {
    case 1: code = "1"; break; // stderr => error   (1)
    case 2: code = "2"; break; // fd:3   => warning (2)
    case 3: code = "3"; break; // fd:4   => info    (3)
    case 4: code = "4"; break; // fd:5   => log     (4)
    }
    JAST log(JSON_OBJECT);
    log.add("jsonrpc", "2.0");
    log.add("method", "build/logMessage");
    JAST &params = log.add("params", JSON_OBJECT);
    params.add("type", JSON_INTEGER, code);
    params.add("message", std::move(line));
    sendMessage(log);
  }
}

static void makeAbsolute(JAST &node) {
  for (auto &child : node.children) makeAbsolute(child.second);
  if (node.kind == JSON_STR && node.value.compare(0, sizeof(workspace)-1, &workspace[0]) == 0) {
    node.value = rooturi + node.value.substr(sizeof(workspace)-1);
  }
}

struct EnumerateTargetsState : public ExecuteWakeProcess {
  JAST &targets;

  EnumerateTargetsState() : targets(result.add("targets", JSON_ARRAY)) {
    cmdline.push_back("--tag-dag");
    cmdline.push_back("bsp\\.buildTarget");
  }

  void gotLine(JAST &row) override;
};

void EnumerateTargetsState::gotLine(JAST &row) {
  // Map from jobid => targetid
  std::map<std::string, std::string> idmap;
  std::vector<JAST> docs;
  // First pass: extract the document and dependency info
  for (auto &job : row.children) {
    std::string &jobid = job.second.get("job").value;
    std::string &targetStr = job.second.get("tags").get("bsp.buildTarget").value;

    JAST target;
    std::stringstream parseErrors;
    if (JAST::parse(targetStr, parseErrors, target)) {
      // Copy job-identifier dependencies into target
      JAST &dependencies = target.add("dependencies", JSON_ARRAY);
      for (auto &dep : job.second.get("deps").children)
        dependencies.add(JSON_OBJECT).value = dep.second.value;
      // Save target and record jobid=>targetid
      idmap[jobid] = target.get("id").get("uri").value;
      docs.emplace_back(std::move(target));
    } else {
      errorMessage("failed to parse tag 'bsp.buildTarget' for job " + jobid + ": " + parseErrors.str());
    }
  }
  // Second pass: resolve references in documents and output them
  for (auto &target : docs) {
    // Resolve job identifiers into bsp:// identifiers
    for (auto &dep : target.get("dependencies").children)
      dep.second.add("uri", std::string(idmap[dep.second.value]));
    // Resolve workspace:// identifiers
    makeAbsolute(target);
    targets.children.emplace_back(std::string(), std::move(target));
  }
}

static void enumerateTargets(JAST &response) {
  EnumerateTargetsState state;
  state.execute();
  if (state.error.kind == JSON_NULLVAL) {
    response.children.emplace_back("result", std::move(state.result));
  } else {
    response.children.emplace_back("error", std::move(state.error));
  }
}

static void makeTime(JAST &node) {
  struct timeval tv;
  for (auto &child : node.children) makeTime(child.second);
  if (node.kind == JSON_STR && node.value == "time://now" && gettimeofday(&tv, 0) == 0) {
    node.value = std::to_string(tv.tv_sec*1000L + tv.tv_usec/1000);
  }
}

struct CompileState : public ExecuteWakeProcess {
  CompileState(int argc, const char **argv) {
    for (int i = 1; i < argc; ++i)
      cmdline.push_back(argv[i]);
  }

  void gotLine(JAST &row) override;
};

void CompileState::gotLine(JAST &row) {
  makeTime(row);
  sendMessage(row);
}

static void compile(JAST &response, const JAST &params, int argc, const char **argv) {
  CompileState state(argc, argv);
  state.execute();
  if (state.error.kind == JSON_NULLVAL) {
    JAST &result = response.add("result", JSON_OBJECT);
    bool ok = WIFEXITED(state.status) && WEXITSTATUS(state.status) == 0;
    result.add("statusCode", JSON_INTEGER, ok?"1":"2");
  } else {
    response.children.emplace_back("error", std::move(state.error));
  }
}

static void test(JAST &response, const JAST &params) {
  JAST &result = response.add("result", JSON_OBJECT);
  result.add("statusCode", JSON_INTEGER, "1");
}

static void run(JAST &response, const JAST &params) {
  JAST &result = response.add("result", JSON_OBJECT);
  result.add("statusCode", JSON_INTEGER, "1");
}

static void clean(JAST &response, const JAST &params) {
  JAST &result = response.add("result", JSON_OBJECT);
  result.add("cleaned", JSON_TRUE);
}

struct ExtractBSPDocument : public ExecuteWakeProcess {
  JAST &items;

  ExtractBSPDocument(const std::string &method, const JAST &params);
  void gotLine(JAST &row) override;
};

ExtractBSPDocument::ExtractBSPDocument(const std::string &method, const JAST &params)
 : items(result.add("items", JSON_ARRAY))
{
  cmdline.push_back("--tag");
  cmdline.push_back("bsp." + method);
  cmdline.push_back("-o");
  for (auto &x : params.get("targets").children) {
    const std::string &uri = x.second.get("uri").value;
    if (uri.compare(0, sizeof(bsp)-1, &bsp[0]) != 0) continue;
    cmdline.emplace_back(uri.substr(sizeof(bsp)-1));
  }
}

void ExtractBSPDocument::gotLine(JAST &row) {
  makeAbsolute(row);
  items.children.emplace_back(std::string(), std::move(row));
}

static void staticTarget(const std::string &method, JAST &response, const JAST &params) {
  ExtractBSPDocument state(method, params);
  state.execute();
  if (state.error.kind == JSON_NULLVAL) {
    response.children.emplace_back("result", std::move(state.result));
  } else {
    response.children.emplace_back("error", std::move(state.error));
  }
}

int main(int argc, const char **argv) {
  bool initialized = false;

  if (getenv("BSP_VERBOSE")) quiet = false;

  // Process requests until something goes wrong
  while (true) {
    size_t json_size = 0;
    // Read header lines until an empty line
    while (true) {
      std::string line;
      std::getline(std::cin, line);
      // Trim trailing CR, if any
      if (!line.empty() && line.back() == '\r') line.resize(line.size()-1);
      // EOF? exit BSP
      if (std::cin.eof()) return 0;
      // Failure reading? Fail with non-zero exit status
      if (!std::cin) return 1;
      // Empty line? stop
      if (line.empty()) break;
      // Capture the json_size
      if (line.compare(0, sizeof(contentLength)-1, &contentLength[0]) == 0)
        json_size = std::stoul(line.substr(sizeof(contentLength)-1));
    }

    // Content-Length was unset?
    if (json_size == 0) return 1;

    // Retrieve the content
    std::string content(json_size, ' ');
    std::cin.read(&content[0], json_size);

    // Begin to formulate our response
    JAST response(JSON_OBJECT);
    response.add("jsonrpc", "2.0");

    // Parse that content as JSON
    JAST request;
    std::stringstream parseErrors;
    if (!JAST::parse(content, parseErrors, request)) {
      response.add("id", JSON_NULLVAL);
      JAST &error = response.add("error", JSON_OBJECT);
      error.add("code", JSON_INTEGER, ParseError);
      error.add("message", parseErrors.str());
    } else {
      // What command?
      const std::string &method = request.get("method").value;
      const JAST &id = request.get("id");
      const JAST &params = request.get("params");

      // Echo back the request's id
      response.children.emplace_back("id", id);

      if (method == "build/exit") {
        return initialized?1:0;
      } else if (method == "build/initialized") {
        // Trigger initial compile of the workspace
        CompileState state(argc, argv);
        state.execute();
      } else if (id.kind == JSON_NULLVAL) {
        // Just ignore notifications
        continue;
      } else if (method == "build/initialize") {
        initialized = initialize(response, params);
      } else if (!initialized) {
        JAST &error = response.add("error", JSON_OBJECT);
        error.add("code", JSON_INTEGER, ServerNotInitialized);
        error.add("message", "Must request build/initialize first");
      } else if (method == "build/shutdown") {
        response.add("result", JSON_NULLVAL);
        initialized = false;
      } else if (method == "workspace/buildTargets") {
        // Query the wake db for BSP targets and collate their descriptions
        enumerateTargets(response);
      } else if (method == "buildTarget/compile") {
        compile(response, params, argc, argv);
      } else if (method == "buildTarget/run") {
        run(response, params);
      } else if (method == "buildTarget/test") {
        test(response, params);
      } else if (method == "buildTarget/cleanCache") {
        clean(response, params);
      } else if (method.compare(0, sizeof(buildTarget)-1, &buildTarget[0]) == 0) {
        // Dispatch request to static JSON files
        staticTarget(method.substr(sizeof(buildTarget)-1), response, params);
      } else {
        JAST &error = response.add("error", JSON_OBJECT);
        error.add("code", JSON_INTEGER, MethodNotFound);
        error.add("message", "Method '" + method + "' is not implemented.");
      }
    }

    sendMessage(response);
  }
}
