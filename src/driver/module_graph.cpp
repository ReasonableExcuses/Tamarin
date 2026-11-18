// ============================================================================
// module_graph.cpp - Static module loading, dependency graph, and cycle detection
// ============================================================================

#include "tamarin/driver/module_graph.h"
#include "tamarin/lexer/lexer.h"
#include "tamarin/parser/parser.h"

#include <algorithm>
#include <sstream>

namespace tamarin::driver {

ModuleGraph::ModuleGraph(SourceManager& sm, DiagnosticEngine& diag, std::vector<std::string> searchRoots)
    : sm_(sm), diag_(diag), searchRoots_(std::move(searchRoots)) {}

std::shared_ptr<Module> ModuleGraph::findByCanonicalPath(const std::string& path) const {
    auto it = modulesByPath_.find(path);
    return it != modulesByPath_.end() ? it->second : nullptr;
}

std::shared_ptr<Module> ModuleGraph::findByLogicalName(const std::string& name) const {
    auto it = modulesByName_.find(name);
    return it != modulesByName_.end() ? it->second : nullptr;
}

std::optional<std::string> ModuleGraph::resolveModulePath(const std::string& dottedName, SourceLocation loc) {
    // 1. Validate dotted components
    std::vector<std::string> parts;
    std::string current;
    std::istringstream stream(dottedName);
    while (std::getline(stream, current, '.')) {
        if (current.empty() || current == "." || current == ".." ||
            current.find('/') != std::string::npos || current.find('\\') != std::string::npos) {
            diag_.report(loc, Severity::Error, "invalid module path: '" + dottedName + "'");
            return std::nullopt;
        }
        parts.push_back(current);
    }

    if (parts.empty()) {
        diag_.report(loc, Severity::Error, "empty module path in import");
        return std::nullopt;
    }

    // 2. Build relative path components
    std::filesystem::path relBasePath;
    for (size_t i = 0; i < parts.size(); ++i) {
        relBasePath /= parts[i];
    }

    // 3. Search through roots in precedence order
    std::vector<std::string> attemptedPaths;
    for (const auto& root : searchRoots_) {
        std::filesystem::path rootPath(root);
        std::vector<std::filesystem::path> candidates = {
            rootPath / (relBasePath.string() + ".tam"),
            rootPath / (relBasePath.string() + ".tmn"),
            rootPath / relBasePath / "lib.tam",
            rootPath / relBasePath / "lib.tmn",
            rootPath / relBasePath / "src" / "lib.tam",
            rootPath / relBasePath / "src" / "lib.tmn"
        };
        if (rootPath.filename() == parts.back()) {
            candidates.push_back(rootPath / "src" / "lib.tam");
            candidates.push_back(rootPath / "lib.tam");
        } else if (rootPath.filename() == "src" && rootPath.parent_path().filename() == parts.back()) {
            candidates.push_back(rootPath / "lib.tam");
            candidates.push_back(rootPath / "lib.tmn");
        }

        for (const auto& candidate : candidates) {
            std::error_code ec;
            if (std::filesystem::exists(candidate, ec) && std::filesystem::is_regular_file(candidate, ec)) {
                auto canonical = std::filesystem::canonical(candidate, ec);
                if (!ec) {
                    return canonical.string();
                }
            }
            attemptedPaths.push_back(candidate.string());
        }
    }

    // 4. Report error with search context
    std::string errMsg = "cannot find module '" + dottedName + "'\nsearched in:";
    for (const auto& att : attemptedPaths) {
        errMsg += "\n  " + att;
    }
    diag_.report(loc, Severity::Error, errMsg);
    return std::nullopt;
}

std::shared_ptr<Module> ModuleGraph::loadModule(const std::string& canonicalPath,
                                                const std::string& logicalName,
                                                SourceLocation importLoc) {
    // Return cached module if already loaded
    auto it = modulesByPath_.find(canonicalPath);
    if (it != modulesByPath_.end()) {
        return it->second;
    }

    // Load file into SourceManager
    auto fileIdOrErr = sm_.loadFile(canonicalPath);
    if (!fileIdOrErr) {
        diag_.report(importLoc, Severity::Error,
                     "failed to open module file '" + canonicalPath + "': " + fileIdOrErr.error());
        return nullptr;
    }
    auto fileId = *fileIdOrErr;

    // Lex and Parse
    lexer::Lexer lex(sm_, fileId, diag_);
    parser::Parser p(lex, diag_);
    p.setTargetTriple(targetTriple_);
    auto prog = p.parseProgram();

    if (diag_.hasErrors()) {
        return nullptr;
    }

    auto mod = std::make_shared<Module>();
    mod->id.canonicalPath = canonicalPath;
    mod->id.logicalName = logicalName;
    mod->fileId = fileId;
    mod->ast = std::make_unique<ast::Program>(std::move(prog));

    modulesByPath_[canonicalPath] = mod;
    modulesByName_[logicalName] = mod;

    // Process imports
    for (const auto& imp : mod->ast->imports) {
        if (mod->aliasToLogicalName.count(imp.alias)) {
            if (mod->aliasToLogicalName[imp.alias] != imp.modulePath) {
                diag_.report(imp.loc, Severity::Error,
                             "conflicting import alias '" + imp.alias + "' for module '" +
                             imp.modulePath + "' (already used for '" +
                             mod->aliasToLogicalName[imp.alias] + "')");
                return nullptr;
            }
            continue; // Duplicate import with same alias, allowed
        }

        auto targetPath = resolveModulePath(imp.modulePath, imp.loc);
        if (!targetPath) {
            return nullptr;
        }

        // Recursively load imported module
        auto targetMod = loadModule(*targetPath, imp.modulePath, imp.loc);
        if (!targetMod) {
            return nullptr;
        }

        mod->imports.push_back(ModuleImport{
            .target = targetMod->id,
            .alias = imp.alias,
            .location = imp.loc
        });
        mod->aliasToLogicalName[imp.alias] = imp.modulePath;
        mod->aliasToModule[imp.alias] = targetMod.get();
    }

    return mod;
}

bool ModuleGraph::buildGraph(const std::string& entryFilePath) {
    std::error_code ec;
    if (!std::filesystem::exists(entryFilePath, ec)) {
        diag_.report(Severity::Error, "input file not found: " + entryFilePath);
        return false;
    }

    auto canonicalEntry = std::filesystem::canonical(entryFilePath, ec).string();
    if (ec) {
        diag_.report(Severity::Error, "failed to resolve canonical path for: " + entryFilePath);
        return false;
    }

    entryModule_ = loadModule(canonicalEntry, "main", SourceLocation{});
    if (!entryModule_ || diag_.hasErrors()) {
        return false;
    }

    return checkCyclesAndOrder();
}

bool ModuleGraph::checkCyclesAndOrder() {
    // 0 = White (unvisited), 1 = Gray (visiting), 2 = Black (visited)
    std::unordered_map<std::string, int> color;
    std::vector<std::string> visitingStack;
    std::vector<std::shared_ptr<Module>> postOrder;

    for (const auto& [path, mod] : modulesByPath_) {
        color[path] = 0;
    }

    std::function<bool(const std::string&)> dfs = [&](const std::string& path) -> bool {
        color[path] = 1; // Gray
        visitingStack.push_back(path);

        auto it = modulesByPath_.find(path);
        if (it != modulesByPath_.end()) {
            auto& mod = it->second;
            for (const auto& imp : mod->imports) {
                const auto& targetPath = imp.target.canonicalPath;
                if (color[targetPath] == 1) {
                    // Circular dependency detected!
                    auto cycleStartIt = std::find(visitingStack.begin(), visitingStack.end(), targetPath);
                    std::vector<std::string> cycleNodes(cycleStartIt, visitingStack.end());
                    cycleNodes.push_back(targetPath);

                    std::string diagMsg = "circular module dependency detected\n\n";
                    for (size_t i = 0; i + 1 < cycleNodes.size(); ++i) {
                        auto fromFilename = std::filesystem::path(cycleNodes[i]).filename().string();
                        auto toFilename = std::filesystem::path(cycleNodes[i + 1]).filename().string();
                        diagMsg += fromFilename + "\n  imports " + toFilename + "\n";
                    }
                    diagMsg += "\nA circular import prevents dependency ordering.";

                    diag_.report(imp.location, Severity::Error, diagMsg);
                    return false;
                }
                if (color[targetPath] == 0) {
                    if (!dfs(targetPath)) return false;
                }
            }
        }

        visitingStack.pop_back();
        color[path] = 2; // Black
        postOrder.push_back(modulesByPath_[path]);
        return true;
    };

    if (!dfs(entryModule_->id.canonicalPath)) {
        return false;
    }

    // Topological order: dependencies first, entry module last
    topologicalOrder_ = std::move(postOrder);
    return true;
}

} // namespace tamarin::driver

