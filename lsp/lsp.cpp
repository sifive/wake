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

// Open Group Base Specifications Issue 7
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <fcntl.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>

#include <iostream>
#include <string>
#include <map>
#include <sstream>
#include <fstream>

#include <json5.h>
#include <execpath.h>

#ifndef VERSION
#include "../src/version.h"
#endif

#include <emscripten/emscripten.h>

// Number of pipes to the wake subprocess
#define PIPES 5

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define VERSION_STR TOSTRING(VERSION)

// Header used in JSON-RPC
static const char contentLength[] = "Content-Length: ";

// Defined by JSON RPC
static const char *ParseError = "-32700";
//static const char *InvalidRequest       = "-32600";
static const char *MethodNotFound = "-32601";
//static const char *InvalidParams        = "-32602";
//static const char *InternalError        = "-32603";
//static const char *serverErrorStart     = "-32099";
//static const char *serverErrorEnd       = "-32000";
//static const char *ServerNotInitialized = "-32002";
//static const char *UnknownErrorCode     = "-32001";

static void sendMessage(const JAST &message)
{
  std::stringstream str;
  str << message;
  str.seekg(0, std::ios::end);
  std::cout << contentLength << str.tellg() << "\r\n\r\n";
  str.seekg(0, std::ios::beg);
  std::cout << str.rdbuf();
}

extern "C"
{
  EMSCRIPTEN_KEEPALIVE int add_one(int n)
  {
    return n + 1;
  }
}

int main(int argc, const char **argv)
{
  // Begin log
  std::ofstream logfile;
  logfile.open("/home/elena/log.txt", std::ios_base::app); // append instead of overwriting
  logfile << std::endl
          << "Log start:" << std::endl;

  // Process requests until something goes wrong
  while (true)
  {
    size_t json_size = 0;
    // Read header lines until an empty line
    while (true)
    {
      std::string line;
      std::getline(std::cin, line);
      // Trim trailing CR, if any
      if (!line.empty() && line.back() == '\r')
        line.resize(line.size() - 1);
      // EOF? exit BSP
      if (std::cin.eof())
        return 0;
      // Failure reading? Fail with non-zero exit status
      if (!std::cin)
        return 1;
      // Empty line? stop
      if (line.empty())
        break;
      // Capture the json_size
      if (line.compare(0, sizeof(contentLength) - 1, &contentLength[0]) == 0)
        json_size = std::stoul(line.substr(sizeof(contentLength) - 1));
    }

    // Content-Length was unset?
    if (json_size == 0)
      return 1;

    // Retrieve the content
    std::string content(json_size, ' ');
    std::cin.read(&content[0], json_size);

    // Log the request
    logfile << content << std::endl;

    // Begin to formulate our response
    JAST response(JSON_OBJECT);
    response.add("jsonrpc", "2.0");

    // Parse that content as JSON
    JAST request;
    std::stringstream parseErrors;
    if (!JAST::parse(content, parseErrors, request))
    {
      response.add("id", JSON_NULLVAL);
      JAST &error = response.add("error", JSON_OBJECT);
      error.add("code", JSON_INTEGER, ParseError);
      error.add("message", parseErrors.str());
    }
    else
    {
      // What command?
      const std::string &method = request.get("method").value;
      const JAST &id = request.get("id");
      //const JAST &params = request.get("params");

      // Echo back the request's id
      response.children.emplace_back("id", id);

      JAST &error = response.add("error", JSON_OBJECT);
      error.add("code", JSON_INTEGER, MethodNotFound);
      error.add("message", "Method '" + method + "' is not implemented.");
    }

    sendMessage(response);
  }
}
