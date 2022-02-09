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

#ifndef ASTREE_H
#define ASTREE_H

#include <iostream>
#include <map>
#include <set>
#include <vector>
#include <functional>
#include <parser/cst.h>

#include "dst/expr.h"
#include "util/diagnostic.h"
#include "symbol_definition.h"


class ASTree {
public:
    std::map<std::string, std::unique_ptr<StringFile>> changedFiles;

    ASTree();

    explicit ASTree(std::string _absLibDir);

    typedef std::pair<const std::string, std::vector<Diagnostic>> FileDiagnostics;

    void diagnoseProject(const std::function<void(FileDiagnostics &)>& processFileDiagnostics);

    Location findDefinitionLocation(const Location& locationToDefine);

    void findReferences(Location &definitionLocation, bool &isDefinitionFound, std::vector<Location> &references);

    std::vector<Location> findOccurrences(Location &symbolLocation);

    std::vector<SymbolDefinition> findHoverInfo(const Location& symbolLocation);

    std::vector<SymbolDefinition> documentSymbol(const std::string &filePath);

    std::vector<SymbolDefinition> workspaceSymbol(const std::string &query);

    std::string absLibDir;
    std::string absWorkDir;

private:
    struct SymbolUsage {
        Location usage;
        Location definition;
        SymbolUsage(Location _usage, Location _definition);
    };

    struct Comment {
        Comment(std::string _comment_text, Location _location, int level);

        std::string comment_text;
        Location location;
        int level; // level of nestedness in the tree
    };

    std::set<Location> types;
    std::vector<SymbolDefinition> definitions;
    std::vector<SymbolUsage> usages;
    std::vector<SymbolDefinition> packages;
    std::vector<Comment> comments;

    class LSPReporter : public DiagnosticReporter {
    private:
        std::map<std::string, std::vector<Diagnostic>> &diagnostics;

        void report(Diagnostic diagnostic) override;
    public:
        explicit LSPReporter(std::map<std::string, std::vector<Diagnostic>> &_diagnostics, const std::vector<std::string> &allFiles);
    };

    void explore(Expr *expr, bool isGlobal);
    void explore_type(const AST &ast);

    static SymbolKind getSymbolKind(const char *name, const std::string& type);

    void recordComments(CSTElement def, int level);

    void fillDefinitionDocumentationFields();

    static std::string sanitizeComment(std::string comment);

    static std::string composeOuterComment(std::vector<std::pair<std::string, int>> comment);

    static void emplaceComment(std::vector<std::pair<std::string, int>> &comment, const std::string &text, int level);

    void recordSameLocationDefinition(std::vector<SymbolDefinition>::iterator &definitions_iterator);
};
#endif
