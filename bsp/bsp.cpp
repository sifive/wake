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
#include <sstream>
#include <fstream>
#include <fcntl.h>
#include <unistd.h>

#include "json5.h"
#include "execpath.h"

#ifndef VERSION
#include "../src/version.h"
#endif

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
//static const char *InternalError        = "-32603";
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

static void makeAbsolute(JAST &node) {
  for (auto &child : node.children) makeAbsolute(child.second);
  if (node.kind == JSON_STR && node.value.compare(0, sizeof(workspace)-1, &workspace[0]) == 0) {
    node.value = rooturi + node.value.substr(sizeof(workspace)-1);
  }
}

static void enumerateTargets(JAST &response) {
  std::vector<std::string> bspTargets;
  // !!! invoke wake on a topic here?
  bspTargets.push_back("build/java/edu.berkeley.cs/chisel3-core_2.12.12-3.4.0/bsp/buildTarget.json");
  bspTargets.push_back("build/java/edu.berkeley.cs/chisel3-macros_2.12.12-3.4.0/bsp/buildTarget.json");
  bspTargets.push_back("build/java/edu.berkeley.cs/rocket-chip_2.12.12-0.1/bsp/buildTarget.json");
  bspTargets.push_back("build/java/edu.berkeley.cs/chisel3_2.12.12-3.4.0/bsp/buildTarget.json");

  JAST &targets = response.add("result", JSON_OBJECT).add("targets", JSON_ARRAY);
  for (auto &file : bspTargets) {
    JAST buildTarget;
    std::stringstream parseErrors;
    if (JAST::parse(file.c_str(), parseErrors, buildTarget)) {
      makeAbsolute(buildTarget);
      targets.children.emplace_back(std::string(), std::move(buildTarget));
    }
  }
}

static void compile(JAST &response, const JAST &params) {
  JAST &result = response.add("result", JSON_OBJECT);
  result.add("statusCode", JSON_INTEGER, "0");
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

static void executeTarget(const std::string &method, JAST &response, const JAST &params) {
  JAST &items = response.add("result", JSON_OBJECT).add("items", JSON_ARRAY);
  // This case applies for scalacOptions, sources, dependencySources, resources, scalaMainClasses, scalaTestClasses
  for (auto &x : params.get("targets").children) {
    const std::string &uri = x.second.get("uri").value;
    if (uri.compare(0, sizeof(bsp)-1, &bsp[0]) != 0) continue;
    std::string path = uri.substr(sizeof(bsp)-1) + "/" + method + ".json";
    JAST ast;
    std::stringstream parseErrors;
    if (!JAST::parse(path.c_str(), parseErrors, ast)) continue;
    makeAbsolute(ast);
    items.children.emplace_back(std::string(), std::move(ast));
  }
}

int main(int argc, const char **argv) {
  bool initialized = false;

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
      } else if (id.kind == JSON_NULLVAL) {
        // Just ignore notifications (eg: build/initialized)
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
        compile(response, params);
      } else if (method == "buildTarget/run") {
        run(response, params);
      } else if (method == "buildTarget/test") {
        test(response, params);
      } else if (method == "buildTarget/cleanCache") {
        clean(response, params);
      } else if (method.compare(0, sizeof(buildTarget)-1, &buildTarget[0]) == 0) {
        // Dispatch request to static JSON files or scripts
        executeTarget(method.substr(sizeof(buildTarget)-1), response, params);
      } else {
        JAST &error = response.add("error", JSON_OBJECT);
        error.add("code", JSON_INTEGER, MethodNotFound);
        error.add("message", "Method '" + method + "' is not implemented.");
      }
    }

    sendMessage(response);
  }
}
