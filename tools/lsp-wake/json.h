/*
 * Copyright 2022 SiFive, Inc.
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

#pragma once

// Open Group Base Specifications Issue 7
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <wcl/optional.h>

#include <iostream>
#include <sstream>
#include <string>

#include "json/json5.h"

struct JsonRPCMessage {
  std::string jsonrpc = "2.0";

  JsonRPCMessage() {}
  JsonRPCMessage(std::string jsonrpc) : jsonrpc(jsonrpc) {}

  static bool parse(const JAST& json, JsonRPCMessage& out) {
    const JAST& json_rpc_val = json.get("jsonrpc");
    if (json_rpc_val.kind != JSON_STR) {
      // TODO: error message
      std::cerr << "missing jsonrpc" << std::endl;
      return false;
    }

    out = {json_rpc_val.value};
    return true;
  }
};

enum class LSPMessageMethod {
  Initialize,
  Initialized,
  TextDocument_DidOpen,
  TextDocument_DidChange,
  TextDocument_DidSave,
  TextDocument_DidClose,
  TextDocument_Definition,
  TextDocument_DocumentHighlight,
  TextDocument_DocumentSymbol,
  TextDocument_Hover,
  TextDocument_References,
  TextDocument_Rename,
  Workspace_Symbol,
  Workspace_DidChangeWatchedFiles,
  Shutdown,
  Exit,
  None,
  Unsupported,
};

static inline LSPMessageMethod stringToLSPMessageMethod(const std::string& str) {
  if (str == "initialize") {
    return LSPMessageMethod::Initialize;
  }
  if (str == "initialized") {
    return LSPMessageMethod::Initialized;
  }
  if (str == "textDocument/didOpen") {
    return LSPMessageMethod::TextDocument_DidOpen;
  }
  if (str == "textDocument/didChange") {
    return LSPMessageMethod::TextDocument_DidChange;
  }
  if (str == "textDocument/didSave") {
    return LSPMessageMethod::TextDocument_DidSave;
  }
  if (str == "textDocument/didClose") {
    return LSPMessageMethod::TextDocument_DidClose;
  }
  if (str == "textDocument/definition") {
    return LSPMessageMethod::TextDocument_Definition;
  }
  if (str == "textDocument/documentHighlight") {
    return LSPMessageMethod::TextDocument_DocumentHighlight;
  }
  if (str == "textDocument/documentSymbol") {
    return LSPMessageMethod::TextDocument_DocumentSymbol;
  }
  if (str == "textDocument/hover") {
    return LSPMessageMethod::TextDocument_Hover;
  }
  if (str == "textDocument/references") {
    return LSPMessageMethod::TextDocument_References;
  }
  if (str == "textDocument/rename") {
    return LSPMessageMethod::TextDocument_Rename;
  }
  if (str == "workspace/symbol") {
    return LSPMessageMethod::Workspace_Symbol;
  }
  if (str == "workspace/didChangeWatchedFiles") {
    return LSPMessageMethod::Workspace_DidChangeWatchedFiles;
  }
  if (str == "shutdown") {
    return LSPMessageMethod::Shutdown;
  }
  if (str == "exit") {
    return LSPMessageMethod::Exit;
  }

  if (str == "") {
    return LSPMessageMethod::None;
  }

  return LSPMessageMethod::Unsupported;
}

struct LSPResponseError {
  const char* code;
  std::string message;
};

struct LSPResponseMessage : public JsonRPCMessage {
  wcl::optional<std::string> id;
  wcl::optional<JAST> result;
  wcl::optional<LSPResponseError> error;

  static LSPResponseMessage createErrorMessage(const char* code, const std::string& message) {
    LSPResponseMessage msg;
    msg.error = {wcl::in_place_t{}, LSPResponseError{code, message}};
    return msg;
  }
};

struct LSPRequestMessage : public JsonRPCMessage {
  wcl::optional<std::string> id;
  LSPMessageMethod method;
  wcl::optional<JAST> params;

  LSPRequestMessage() {}
  LSPRequestMessage(std::string jsonrpc, wcl::optional<std::string> id, LSPMessageMethod method,
                    wcl::optional<JAST> params)
      : JsonRPCMessage(jsonrpc), id(id), method(method), params(std::move(params)) {}

  static bool parse(const std::string& str, LSPRequestMessage& out) {
    JAST json;
    std::stringstream parse_errors;

    if (!JAST::parse(str, parse_errors, json)) {
      std::cerr << "Failed to parse json command: " << parse_errors.str() << std::endl;
      return false;
    }

    JsonRPCMessage msg;
    if (!JsonRPCMessage::parse(json, msg)) {
      return false;
    }

    const JAST& id_val = json.get("id");
    wcl::optional<std::string> id_out = {};
    if (id_val.kind == JSON_STR || id_val.kind == JSON_INTEGER) {
      id_out = {wcl::in_place_t{}, id_val.value};
    }

    const JAST& method_val = json.get("method");
    if (method_val.kind != JSON_STR) {
      std::cerr << "failed to parse method" << std::endl;
      return false;
    }

    wcl::optional<JAST> params = {wcl::in_place_t{}, std::move(json.get("params"))};
    if (params->kind != JSON_NULLVAL) {
      params = {};
    }

    out = LSPRequestMessage(msg.jsonrpc, id_out, stringToLSPMessageMethod(method_val.value),
                            std::move(params));

    return true;
  }

  // Notification don't specify an id
  const bool is_notification() const { return !id; }

  const LSPResponseMessage createErrorResponse(const char* code, const std::string& message) const {
    auto response = LSPResponseMessage::createErrorMessage(code, message);
    response.id = id;
    return response;
  }
};
