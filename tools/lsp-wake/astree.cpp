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

#include "astree.h"

#include <algorithm>
#include <cassert>
#include <functional>
#include <iostream>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

#include "dst/bind.h"
#include "dst/expr.h"
#include "dst/todst.h"
#include "parser/cst.h"
#include "parser/lexer.h"
#include "parser/parser.h"
#include "parser/wakefiles.h"
#include "symbol_definition.h"
#include "types/internal.h"
#include "util/diagnostic.h"
#include "util/file.h"
#include "util/fragment.h"

static CPPFile cppFile(__FILE__);

void ASTree::recordComments(CSTElement def, int level) {
  if (def.id() == TOKEN_COMMENT) {
    Location loc = def.location();
    comments[LineLocation(loc)] = Comment(def.segment().str(), def.location(), level);
  }

  for (CSTElement innerdef = def.firstChildElement(); !innerdef.empty();
       innerdef.nextSiblingElement()) {
    recordComments(innerdef, level + 1);
  }
}

void ASTree::diagnoseProject(const std::function<void(FileDiagnostics &)> &processFileDiagnostics) {
  usages.clear();
  definitions.clear();
  types.clear();
  packages.clear();
  comments.clear();

  bool enumok = true;
  auto allFiles = find_all_wakefiles(enumok, true, false, absLibDir, absWorkDir, stdout, false);

  std::map<std::string, std::vector<Diagnostic>> diagnostics;
  LSPReporter lspReporter(diagnostics, allFiles);
  reporter = &lspReporter;

  std::unique_ptr<Top> top(new Top);
  top->def_package = "nothing";
  top->body = std::unique_ptr<Expr>(new VarRef(FRAGMENT_CPP_LINE, "Nil@wake"));

  std::vector<ExternalFile> externalFiles;
  externalFiles.reserve(allFiles.size());

  for (auto &filename : allFiles) {
    auto it = changedFiles.find(filename);
    FileContent *fcontent;
    if (it == changedFiles.end()) {
      // Re-read files that are not modified in the editor, because who knows what someone did in a
      // terminal
      externalFiles.emplace_back(lspReporter, filename.c_str());
      fcontent = &externalFiles.back();
    } else {
      fcontent = it->second.get();
    }

    CST cst(*fcontent, lspReporter);
    dst_top(cst.root(), *top);
    recordComments(cst.root(), 0);
  }
  flatten_exports(*top);

  for (auto &p : top->packages) {
    for (auto &f : p.second->files) {
      packages.emplace_back(
          SymbolDefinition(p.first, f.content->fragment.location(), "Package", KIND_PACKAGE, true));
    }
  }

  PrimMap pmap = prim_register_internal();
  bool isTreeBuilt = true;
  std::unique_ptr<Expr> root = bind_refs(std::move(top), pmap, isTreeBuilt);

  for (auto &diagnosticEntry : diagnostics) {
    processFileDiagnostics(diagnosticEntry);
  }

  if (root != nullptr) explore(root.get(), true);

  fillDefinitionDocumentationFields();
}

Location ASTree::findDefinitionLocation(const Location &locationToDefine) {
  for (const SymbolUsage &usage : usages) {
    if (usage.usage.contains(locationToDefine)) {
      return usage.definition;
    }
  }
  for (const SymbolDefinition &def : definitions) {
    if (def.location.contains(locationToDefine)) {
      return def.location;
    }
  }
  return {""};
}

void ASTree::findReferences(Location &definitionLocation, bool &isDefinitionFound,
                            std::vector<Location> &references) {
  for (const SymbolUsage &use : usages) {
    if (use.usage.contains(definitionLocation)) {
      definitionLocation = use.definition;
      isDefinitionFound = true;
      break;
    }
  }
  if (!isDefinitionFound) {
    for (const SymbolDefinition &def : definitions) {
      if (def.location.contains(definitionLocation)) {
        definitionLocation = def.location;
        isDefinitionFound = true;
        break;
      }
    }
  }
  if (isDefinitionFound) {
    for (const SymbolUsage &use : usages) {
      if (use.definition.contains(definitionLocation)) {
        references.push_back(use.usage);
      }
    }
  }
}

std::vector<Location> ASTree::findOccurrences(Location &symbolLocation) {
  Location definitionLocation = symbolLocation;
  bool isDefinitionFound = false;

  for (const SymbolUsage &use : usages) {
    if (use.usage.contains(symbolLocation)) {
      definitionLocation = use.definition;
      isDefinitionFound = true;
      break;
    }
  }
  if (!isDefinitionFound) {
    for (const SymbolDefinition &def : definitions) {
      if (def.location.contains(symbolLocation)) {
        definitionLocation = def.location;
        isDefinitionFound = true;
        break;
      }
    }
  }
  std::vector<Location> occurrences;
  if (isDefinitionFound) {
    for (const SymbolUsage &usage : usages) {
      if (usage.usage.filename == symbolLocation.filename &&
          usage.definition.contains(definitionLocation)) {
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

  for (const SymbolUsage &use : usages) {
    if (use.usage.contains(symbolLocation)) {
      definitionLocation = use.definition;
      break;
    }
  }
  std::vector<SymbolDefinition> hoverInfoPieces;
  for (SymbolDefinition &def : definitions) {
    if (def.location.contains(definitionLocation)) {
      hoverInfoPieces.push_back(def);
    }
  }
  return hoverInfoPieces;
}

std::vector<SymbolDefinition> ASTree::documentSymbol(const std::string &filePath) {
  std::vector<SymbolDefinition> symbols;
  for (const SymbolDefinition &def : definitions) {
    if (def.isGlobal && def.location.filename == filePath) {
      symbols.push_back(def);
    }
  }
  for (const SymbolDefinition &p : packages) {
    if (p.location.filename == filePath) {
      symbols.push_back(p);
    }
  }
  return symbols;
}

std::vector<SymbolDefinition> ASTree::workspaceSymbol(const std::string &query) {
  std::vector<SymbolDefinition> symbols;
  for (const SymbolDefinition &def : definitions) {
    if (def.isGlobal && def.name.find(query) != std::string::npos) {
      symbols.push_back(def);
    }
  }
  for (const SymbolDefinition &p : packages) {
    if (p.name.find(query) != std::string::npos) {
      symbols.push_back(p);
    }
  }
  return symbols;
}

void ASTree::explore(Expr *expr, bool isGlobal) {
  if (!expr) return;
  if (expr->type == &VarRef::type) {
    VarRef *ref = dynamic_cast<VarRef *>(expr);
    if (!ref->fragment.empty() && !ref->target.empty() && (ref->flags & FLAG_AST) != 0) {
      usages.emplace_back(
          /* use location */ ref->fragment.location(),
          /* definition location */ ref->target.location());
    }
  } else if (expr->type == &App::type) {
    App *app = dynamic_cast<App *>(expr);
    explore(app->val.get(), false);
    explore(app->fn.get(), false);
  } else if (expr->type == &Lambda::type) {
    Lambda *lambda = dynamic_cast<Lambda *>(expr);
    if (!lambda->token.empty()) {
      std::stringstream ss;
      ss << lambda->typeVar[0];
      if (lambda->name.find(' ') >= lambda->name.find('@')) {
        definitions.emplace_back(
            /* name */ lambda->name,
            /* location */ lambda->token.location(),
            /* type */ ss.str(),
            /* kind */ getSymbolKind(lambda->name.c_str(), lambda->typeVar[0].getName()),
            /* global */ isGlobal);
      }
    }
    explore(lambda->body.get(), false);
  } else if (expr->type == &Ascribe::type) {
    Ascribe *ascribe = dynamic_cast<Ascribe *>(expr);
    explore_type(ascribe->signature);
    explore(ascribe->body.get(), false);
  } else if (expr->type == &DefBinding::type) {
    DefBinding *defbinding = dynamic_cast<DefBinding *>(expr);
    for (auto &i : defbinding->val) explore(i.get(), false);
    for (auto &i : defbinding->fun) explore(i.get(), false);
    for (auto &i : defbinding->order) {
      if (i.second.fragment.empty()) continue;
      std::stringstream ss;
      SymbolKind symbolKind;
      size_t idx = i.second.index;
      if (idx < defbinding->val.size()) {
        Expr *child = defbinding->val[idx].get();
        if (!child) continue;
        ss << child->typeVar;
        symbolKind = getSymbolKind(i.first.c_str(), child->typeVar.getName());
      } else {
        idx -= defbinding->val.size();
        Expr *child = defbinding->fun[idx].get();
        if (!child) continue;
        ss << child->typeVar;
        symbolKind = getSymbolKind(i.first.c_str(), child->typeVar.getName());
      }
      if (i.first.find(' ') >= i.first.find('@') || i.first.compare(0, 7, "binary ") == 0 ||
          i.first.compare(0, 6, "unary ") == 0) {
        definitions.emplace_back(
            /* name */ i.first,
            /* location */ i.second.fragment.location(),
            /* type */ ss.str(),
            /* kind */ symbolKind,
            /* global */ isGlobal);
      }
    }
    explore(defbinding->body.get(), isGlobal);
  } else if (expr->type == &Destruct::type) {
    Destruct *destruct = dynamic_cast<Destruct *>(expr);
    std::shared_ptr<Sum> sum = destruct->sum;

    for (size_t i = 0; i < sum->members.size(); i++) {
      FileFragment token = sum->members[i].ast.token;
      if (!token.empty()) {
        Location definitionLocation = token.location();
        for (FileFragment use : destruct->uses[i]) {
          usages.emplace_back(use.location(), definitionLocation);
        }
      }
    }

    explore(destruct->arg.get(), false);
    for (auto &c : destruct->cases) explore(c.get(), false);
  } else if (expr->type == &Construct::type) {
    Construct *construct = dynamic_cast<Construct *>(expr);
    if (construct->sum->scoped) {
      construct->sum->scoped = false;

      if (types.insert(construct->sum->token.location()).second) {
        definitions.emplace_back(
            /* name */ construct->sum->name,
            /* location */ construct->sum->token.location(),
            /* type */ "type",
            /* kind */ KIND_CLASS,
            /* global */ true);
      }

      for (auto &mem : construct->sum->members) {
        explore_type(mem.ast);
      }
    }
  }
}

void ASTree::explore_type(const AST &ast) {
  for (auto &x : ast.args) explore_type(x);

  if (lex_kind(ast.name) == LOWER) return;
  if (ast.definition.empty()) return;

  usages.emplace_back(ast.token.location(), ast.definition.location());

  if (types.insert(ast.definition.location()).second) {
    definitions.emplace_back(
        /* name */ ast.name,
        /* location */ ast.definition.location(),
        /* type */ "type",
        /* kind */ KIND_CLASS,
        /* global */ true);
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
      if (type == "String@builtin" || type == "RegExp@builtin") {
        return KIND_STRING;
      }
      if (type == "Integer@builtin" || type == "Double@builtin") {
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

void ASTree::mergeSameLineDefinitions(const SymbolDefinition &merge_from, SymbolDefinition &into) {
  // Certain types such as Map, have two Definition entrees. Where one has
  // type = "type" and the other is fully resolved.
  // Ex:
  //   Map@wake: type
  //   Map@wake: (Comparison: a => a => Order) => (Data: Tree (Pair a b)) => Map a b
  // We always prefer the resolved type
  if (into.introduces.empty() && into.type != "type") {
    into.introduces.emplace_back(into.name, into.type);
  }

  if (merge_from.type != "type") {
    into.introduces.emplace_back(merge_from.name, merge_from.type);
  }

  // If we start out with the bad type, we need to override it.
  if (into.type == "type") {
    into.type = merge_from.type;
  }

  // Functions starting with edit or get are generated by the compiler and are of shape
  // getTupleType@package. If we hit one of these, make the tuple type the root name
  // and make the member type the root type
  // TODO: this isn't a great way to solve this problem. We should try to fix this
  // in the front end.
  if (into.name.find("get") == 0) {
    into.name = into.name.substr(3);
    into.type = into.type.substr(into.type.find("=> ") + 3);
  }

  // Do it twice because either side of the merge could be the getX
  if (merge_from.name.find("get") == 0) {
    into.name = merge_from.name.substr(3);
    into.type = merge_from.type.substr(merge_from.type.find("=> ") + 3);
  }
}

void ASTree::fillDefinitionDocumentationFields() {
  // A single line (usually a tuple) may introduce multiple defs.
  // However we can only report one documentation for a single location.
  // Track the duplicates, and merge them together as necessary.
  // The value is an index into resolved_defs.
  std::unordered_map<Location, SymbolDefinition &> visited_definitions;
  std::vector<size_t> remove_indexes;

  // Walk all definitions and find their doc comments
  for (size_t i = 0; i < definitions.size(); i++) {
    auto &def = definitions[i];

    // "isGlobal" is a bad name. Read this as "is publically available for consumption"
    // TODO: Rename this member to more precisely cover it's semantics.
    if (!def.isGlobal) {
      continue;
    }

    def.documentation = sanitizeComments(collectDocumentationComments(def));

    auto it = visited_definitions.find(def.location);

    // This is a duplicate def, merge it
    if (it != visited_definitions.end()) {
      mergeSameLineDefinitions(def, it->second);
      remove_indexes.push_back(i);
      continue;
    }

    // Mark this definition as visited and resolved
    visited_definitions.insert({def.location, def});
  }

  for (auto it = remove_indexes.rbegin(); it != remove_indexes.rend(); ++it) {
    definitions[*it] = std::move(definitions[definitions.size() - 1]);
    definitions.pop_back();
  }
}

std::vector<ASTree::Comment> ASTree::collectDocumentationComments(const SymbolDefinition &def) {
  // Find the line above the def
  Location loc = def.location;
  std::vector<Comment> doc_comments;

  while (true) {
    // The current line is the first line, nothing else to be done
    if (loc.start.row == 1) {
      break;
    }

    // Try to get the line above us
    LineLocation next_location(loc.filename, loc.start.row - 1);
    auto it = comments.find(next_location);

    // There isn't a comment on the next line up, thus the documentation is over
    if (it == comments.end()) {
      break;
    }

    // We have a documentation comment, store it for later
    doc_comments.push_back(it->second);

    // This line becomes the current line for the next iteration
    loc = it->second.location;

    // Comments may only be line line long
    assert(loc.start.row == loc.end.row);
  }

  return doc_comments;
}

std::string ASTree::sanitizeComment(std::string comment) {
  std::size_t comment_from = 0;
  for (std::size_t i = 0; i + 1 < comment.size(); ++i) {
    if (comment[i] == '\n' && comment[i + 1] == '\n') comment_from = i + 2;
  }
  comment = comment.substr(comment_from);

  std::vector<size_t> commentHashtagIndices;
  if (!comment.empty() && comment[0] == '#') commentHashtagIndices.push_back(0);
  for (std::size_t i = 0; i + 1 < comment.size(); ++i) {
    if (comment[i] == '\n' && comment[i + 1] == '#') commentHashtagIndices.push_back(i + 1);
  }
  for (std::size_t i = 0; i < commentHashtagIndices.size(); ++i) {
    comment.erase(commentHashtagIndices[i] - i, 1);
  }
  if (!comment.empty() && comment[0] == '\n') comment.erase(0, 1);
  return comment;
}

std::string ASTree::sanitizeComments(const std::vector<Comment> &comments) {
  std::string composed;
  for (auto it = comments.rbegin(); it != comments.rend(); ++it) {
    composed += sanitizeComment(it->comment_text) + "\n";
  }
  return composed;
}

void ASTree::emplaceComment(std::vector<std::pair<std::string, int>> &comment,
                            const std::string &text, int level) {
  if (comment.empty() || comment.back().second < level) {
    comment.emplace_back(text, level);
  }
  comment.back().first += text;
}

void ASTree::LSPReporter::report(Diagnostic diagnostic) {
  diagnostics[diagnostic.getFilename()].push_back(diagnostic);
}

ASTree::LSPReporter::LSPReporter(std::map<std::string, std::vector<Diagnostic>> &_diagnostics,
                                 const std::vector<std::string> &allFiles)
    : diagnostics(_diagnostics) {
  // create an empty diagnostics vector for each file
  for (const std::string &fileName : allFiles) {
    diagnostics[fileName];
  }
}
