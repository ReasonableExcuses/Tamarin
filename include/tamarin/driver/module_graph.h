// ============================================================================
// module_graph.h - Static module loading, dependency graph, and cycle detection
// ============================================================================
#pragma once

#include "tamarin/ast/ast.h"
#include "tamarin/source/source_manager.h"
#include "tamarin/diagnostics/diagnostic_engine.h"

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace tamarin::driver {

struct ModuleId {
    std::string canonicalPath;
    std::string logicalName;
};

struct ModuleImport {
    ModuleId target;
    std::string alias;
    SourceLocation location;
};

struct Module {
    ModuleId id;
    SourceFileId fileId;
    std::unique_ptr<ast::Program> ast;
    std::vector<ModuleImport> imports;
    std::unordered_map<std::string, std::string> aliasToLogicalName;
    std::unordered_map<std::string, const Module*> aliasToModule;
};

class ModuleGraph {
public:
    ModuleGraph(SourceManager& sm, DiagnosticEngine& diag, std::vector<std::string> searchRoots);

    /// Load the entry module and all transitively imported modules.
    /// Returns true on success, false if module resolution, parsing, or cycle detection fails.
    bool buildGraph(const std::string& entryFilePath);

    /// Get all loaded modules in topological order (dependencies first, entry module last).
    [[nodiscard]] const std::vector<std::shared_ptr<Module>>& getTopologicalOrder() const noexcept {
        return topologicalOrder_;
    }

    /// Get the entry module.
    [[nodiscard]] std::shared_ptr<Module> getEntryModule() const noexcept {
        return entryModule_;
    }

    /// Find module by its canonical file path.
    [[nodiscard]] std::shared_ptr<Module> findByCanonicalPath(const std::string& path) const;

    /// Find module by its logical dotted name.
    [[nodiscard]] std::shared_ptr<Module> findByLogicalName(const std::string& name) const;

    /// Resolve a dotted module path to a canonical filesystem path.
    std::optional<std::string> resolveModulePath(const std::string& dottedName, SourceLocation loc);

private:
    std::shared_ptr<Module> loadModule(const std::string& canonicalPath, const std::string& logicalName, SourceLocation importLoc);
    bool checkCyclesAndOrder();

    SourceManager& sm_;
    DiagnosticEngine& diag_;
    std::vector<std::string> searchRoots_;

    std::shared_ptr<Module> entryModule_;
    std::unordered_map<std::string, std::shared_ptr<Module>> modulesByPath_;
    std::unordered_map<std::string, std::shared_ptr<Module>> modulesByName_;
    std::vector<std::shared_ptr<Module>> topologicalOrder_;
};

} // namespace tamarin::driver
