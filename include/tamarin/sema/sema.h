// ============================================================================
// sema.h - Semantic analyzer for Tamarin
// ============================================================================
#pragma once

#include "tamarin/ast/ast.h"
#include "tamarin/sema/types.h"
#include "tamarin/sema/symbol_table.h"
#include "tamarin/sema/type_checker.h"
#include "tamarin/sema/module_scope.h"
#include "tamarin/diagnostics/diagnostic_engine.h"

#include <memory>
#include <vector>

namespace tamarin::driver {
struct Module;
}

namespace tamarin::sema {

class Sema {
public:
    Sema(DiagnosticEngine& diag);

    /// Analyze a single program. Returns true if semantically valid.
    bool analyze(ast::Program& prog);

    /// Analyze multiple modules in dependency order. Returns true if semantically valid.
    bool analyzeModules(const std::vector<std::shared_ptr<driver::Module>>& modules);

    TypeContext& context() noexcept { return tc_; }
    SymbolTable& symbols() noexcept { return syms_; }
    TypeChecker& checker() noexcept { return checker_; }
    const ModuleScope* getModuleScope(const std::string& logicalName) const {
        auto it = moduleScopes_.find(logicalName);
        return it != moduleScopes_.end() ? it->second.get() : nullptr;
    }

    void setLibraryMode(bool isLib) noexcept { isLibraryMode_ = isLib; }
    bool isLibraryMode() const noexcept { return isLibraryMode_; }

    void setTargetTriple(target::TargetTriple triple) noexcept { targetTriple_ = std::move(triple); }
    const target::TargetTriple& getTargetTriple() const noexcept { return targetTriple_; }

    static void pruneInactiveDeclarations(ast::Program& prog, const target::TargetTriple& triple);

private:
    void registerTopDecls(ast::Program& prog);
    void checkFunctionBodies(ast::Program& prog);
    void validateMain(const ast::Program& prog);

    TypeContext      tc_;
    SymbolTable      syms_;
    DiagnosticEngine& diag_;
    TypeChecker      checker_;
    std::unordered_map<std::string, std::unique_ptr<ModuleScope>> moduleScopes_;
    bool             isLibraryMode_ = false;
    target::TargetTriple targetTriple_ = target::TargetTriple::getHost();
};

} // namespace tamarin::sema

