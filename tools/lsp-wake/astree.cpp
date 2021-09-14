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

#include <iostream>
#include <map>
#include <utility>
#include <vector>
#include <functional>
#include <algorithm>

#include "astree.h"
#include "dst/expr.h"
#include "dst/bind.h"
#include "util/file.h"
#include "parser/cst.h"
#include "dst/todst.h"
#include "util/diagnostic.h"
#include "symbol_definition.h"
#include "parser/wakefiles.h"
#include "parser/lexer.h"
#include "parser/parser.h"
#include "util/fragment.h"
#include "types/internal.h"

static CPPFile cppFile(__FILE__);

ASTree::ASTree() {}

ASTree::ASTree(std::string _absLibDir) : absLibDir(std::move(_absLibDir)) {}

void ASTree::diagnoseProject(const std::function<void(FileDiagnostics &)> &processFileDiagnostics) {
  usages.clear();
  definitions.clear();
  packages.clear();
  comments.clear();

  bool enumok = true;
  auto allFiles = find_all_wakefiles(enumok, true, false, absLibDir, absWorkDir);

  std::map<std::string, std::vector<Diagnostic>> diagnostics;
  LSPReporter lspReporter(diagnostics, allFiles);
  reporter = &lspReporter;

  std::unique_ptr<Top> top(new Top);
  top->def_package = "nothing";
  top->body = std::unique_ptr<Expr>(new VarRef(FRAGMENT_CPP_LINE, "Nil@wake"));

  std::vector<ExternalFile> externalFiles;
  externalFiles.reserve(allFiles.size());

  for (auto &filename: allFiles) {
    auto it = changedFiles.find(filename);
    FileContent *fcontent;
    if (it == changedFiles.end()) {
      // Re-read files that are not modified in the editor, because who knows what someone did in a terminal
      externalFiles.emplace_back(lspReporter, filename.c_str());
      fcontent = &externalFiles.back();
    } else {
      fcontent = it->second.get();
    }

    CST cst(*fcontent, lspReporter);
    dst_top(cst.root(), *top);
    for (CSTElement topdef = cst.root().firstChildElement(); !topdef.empty(); topdef.nextSiblingElement()) {
      if (topdef.id() == TOKEN_COMMENT || topdef.id() == TOKEN_NL) {
        comments.emplace_back(topdef.segment().str(), topdef.location());
      }
    }
  }
  flatten_exports(*top);


  for (auto &p : top->packages) {
    for (auto &f: p.second->files) {
      packages.emplace_back(SymbolDefinition(p.first, f.content->fragment.location(), "Package", KIND_PACKAGE, true));
    }
  }

  PrimMap pmap = prim_register_internal();
  bool isTreeBuilt = true;
  std::unique_ptr<Expr> root = bind_refs(std::move(top), pmap, isTreeBuilt);

  for (auto &diagnosticEntry : diagnostics) {
    processFileDiagnostics(diagnosticEntry);
  }

  if (root != nullptr)
    explore(root.get(), true);

  std::sort(definitions.begin(), definitions.end());
  fillDefinitionDocumentationFields();
}

Location ASTree::findDefinitionLocation(const Location &locationToDefine) {
  for (const SymbolUsage &usage: usages) {
    if (usage.usage.contains(locationToDefine)) {
      return usage.definition;
    }
  }
  for (const SymbolDefinition &def: definitions) {
    if (def.location.contains(locationToDefine)) {
      return def.location;
    }
  }
  return {""};
}

void ASTree::findReferences(Location &definitionLocation, bool &isDefinitionFound, std::vector<Location> &references) {
  for (const SymbolUsage &use: usages) {
    if (use.usage.contains(definitionLocation)) {
      definitionLocation = use.definition;
      isDefinitionFound = true;
      break;
    }
  }
  if (!isDefinitionFound) {
    for (const SymbolDefinition &def: definitions) {
      if (def.location.contains(definitionLocation)) {
        definitionLocation = def.location;
        isDefinitionFound = true;
        break;
      }
    }
  }
  if (isDefinitionFound) {
    for (const SymbolUsage &use: usages) {
      if (use.definition.contains(definitionLocation)) {
        references.push_back(use.usage);
      }
    }
  }
}

std::vector<Location> ASTree::findOccurrences(Location &symbolLocation) {
  Location definitionLocation = symbolLocation;
  bool isDefinitionFound = false;

  for (const SymbolUsage &use: usages) {
    if (use.usage.contains(symbolLocation)) {
      definitionLocation = use.definition;
      isDefinitionFound = true;
      break;
    }
  }
  if (!isDefinitionFound) {
    for (const SymbolDefinition &def: definitions) {
      if (def.location.contains(symbolLocation)) {
        definitionLocation = def.location;
        isDefinitionFound = true;
        break;
      }
    }
  }
  std::vector<Location> occurrences;
  if (isDefinitionFound) {
    for (const SymbolUsage &usage: usages) {
      if (usage.usage.filename == symbolLocation.filename && usage.definition.contains(definitionLocation)) {
        occurrences.push_back(usage.usage);
      }
    }
    if (definitionLocation.filename == symbolLocation.filename) {
      occurrences.push_back(definitionLocation);
    }
  }

  return occurrences;
}

std::vector<SymbolDefinition> ASTree::findHoverInfo(const Location &symbolLocation) {
  Location definitionLocation = symbolLocation;

  for (const SymbolUsage &use: usages) {
    if (use.usage.contains(symbolLocation)) {
      definitionLocation = use.definition;
      break;
    }
  }
  std::vector<SymbolDefinition> hoverInfoPieces;
  for (SymbolDefinition &def: definitions) {
    if (def.location.contains(definitionLocation)) {
      hoverInfoPieces.push_back(def);
    }
  }
  return hoverInfoPieces;
}

std::vector<SymbolDefinition> ASTree::documentSymbol(const std::string &filePath) {
  std::vector<SymbolDefinition> symbols;
  for (const SymbolDefinition &def: definitions) {
    if (def.isGlobal && def.location.filename == filePath) {
      symbols.push_back(def);
    }
  }
  for (const SymbolDefinition &p: packages) {
    if (p.location.filename == filePath) {
      symbols.push_back(p);
    }
  }
  return symbols;
}

std::vector<SymbolDefinition> ASTree::workspaceSymbol(const std::string &query) {
  std::vector<SymbolDefinition> symbols;
  for (const SymbolDefinition &def: definitions) {
    if (def.isGlobal && def.name.find(query) != std::string::npos) {
      symbols.push_back(def);
    }
  }
  for (const SymbolDefinition &p: packages) {
    if (p.name.find(query) != std::string::npos) {
      symbols.push_back(p);
    }
  }
  return symbols;
}

void ASTree::explore(Expr *expr, bool isGlobal) {
  if (expr->type == &VarRef::type) {
    VarRef *ref = dynamic_cast<VarRef*>(expr);
    if (!ref->fragment.empty() && !ref->target.empty() && (ref->flags & FLAG_AST) != 0) {
      usages.emplace_back(
        /* use location */        ref->fragment.location(),
        /* definition location */ ref->target.location());
    }
  } else if (expr->type == &App::type) {
    App *app = dynamic_cast<App*>(expr);
    explore(app->val.get(), false);
    explore(app->fn.get(), false);
  } else if (expr->type == &Lambda::type) {
    Lambda *lambda = dynamic_cast<Lambda*>(expr);
    if (!lambda->token.empty()) {
      std::stringstream ss;
      ss << lambda->typeVar[0];
      if (lambda->name.find(' ') >= lambda->name.find('@')) {
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
    Ascribe *ascribe = dynamic_cast<Ascribe*>(expr);
    explore(ascribe->body.get(), false);
  } else if (expr->type == &DefBinding::type) {
    DefBinding *defbinding = dynamic_cast<DefBinding*>(expr);
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
        if (i.first.find(' ') >= i.first.find('@') ||
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

SymbolKind ASTree::getSymbolKind(const char *name, const std::string &type) {
  switch (lex_kind(name)) {
    case OPERATOR:
      return KIND_OPERATOR;
      case UPPER:
        return KIND_ENUM_MEMBER;
        case LOWER:
          default:
            if (type == "binary =>@builtin") {
              return KIND_FUNCTION;
            }
            if (type == "String@builtin" ||
            type == "RegExp@builtin") {
              return KIND_STRING;
            }
            if (type == "Integer@builtin" ||
            type == "Double@builtin") {
              return KIND_NUMBER;
            }
            if (type == "Boolean@wake") {
              return KIND_BOOLEAN;
            }
            if (type.compare(0, 12, "Vector@wake ") == 0) {
              return KIND_ARRAY;
            }
            return KIND_VARIABLE;
  }
}

void ASTree::fillDefinitionDocumentationFields() {
  auto definitions_iterator = definitions.begin();
  auto comments_iterator = comments.begin();

  std::string comment;
  Location lastCommentLocation("");

  while (definitions_iterator != definitions.end() && comments_iterator != comments.end()) {
    if (definitions_iterator->location < comments_iterator->location) {
      if (lastCommentLocation.filename != definitions_iterator->location.filename) {
        comment = "";
      }
      definitions_iterator->documentation = sanitizeComment(comment);
      comment = "";
      ++definitions_iterator;
    } else {
      if (lastCommentLocation.filename != comments_iterator->location.filename) {
        comment = "";
      }
      comment += comments_iterator->comment_text;
      lastCommentLocation = comments_iterator->location;
      ++comments_iterator;
    }
  }

  if (definitions_iterator != definitions.end()) {
    if (!comment.empty() && lastCommentLocation.filename != comments.back().location.filename) {
      comment = "";
    }
    definitions_iterator->documentation = sanitizeComment(comment);
  }
}

std::string ASTree::sanitizeComment(std::string comment) {
  std::size_t comment_from = 0;
  for (std::size_t i = 0; i + 1 < comment.size(); ++i) {
    if (comment[i] == '\n' && comment[i + 1] == '\n') comment_from = i + 2;
  }
  comment = comment.substr(comment_from);

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

ASTree::SymbolUsage::SymbolUsage(Location _usage, Location _definition) : usage(std::move(_usage)), definition(std::move(_definition)) {}

ASTree::Comment::Comment(std::string _comment_text, Location _location)
: comment_text(std::move(_comment_text)), location(std::move(_location)) {}

void ASTree::LSPReporter::report(Diagnostic diagnostic) {
  diagnostics[diagnostic.getFilename()].push_back(diagnostic);
}

ASTree::LSPReporter::LSPReporter(std::map<std::string, std::vector<Diagnostic>> &_diagnostics, const std::vector<std::string> &allFiles)  : diagnostics(
  _diagnostics) {
  // create an empty diagnostics vector for each file
  for (const std::string &fileName: allFiles) {
    diagnostics[fileName];
  }
}
