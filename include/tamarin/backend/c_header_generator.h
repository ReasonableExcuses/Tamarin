// ============================================================================
// c_header_generator.h - Generates C99 headers for exported Tamarin symbols
// ============================================================================
#pragma once

#include "tamarin/ast/ast.h"
#include "tamarin/sema/sema.h"
#include "tamarin/diagnostics/diagnostic_engine.h"

#include <filesystem>
#include <ostream>
#include <string>
#include <vector>

namespace tamarin::backend {

class CHeaderGenerator {
public:
    explicit CHeaderGenerator(DiagnosticEngine& diag);

    /// Generate C header content to an output stream.
    bool generate(const ast::Program& prog, sema::TypeChecker& checker,
                  const std::string& moduleName, std::ostream& os);

    /// Generate C header and write to a file.
    bool generateToFile(const ast::Program& prog, sema::TypeChecker& checker,
                        const std::string& moduleName, const std::filesystem::path& outPath);

    /// Generate C header content from a list of AST declarations.
    bool generateDecls(const std::vector<const ast::Decl*>& decls, sema::TypeChecker& checker,
                       const std::string& moduleName, std::ostream& os);

    /// Generate C header from a list of AST declarations and write to a file.
    bool generateDeclsToFile(const std::vector<const ast::Decl*>& decls, sema::TypeChecker& checker,
                             const std::string& moduleName, const std::filesystem::path& outPath);

    /// Map a Tamarin sema::Type to its C99 type string.
    static std::string toCType(const sema::Type* ty);

    /// Map an AST TypeNode to its C99 type string.
    static std::string toCType(const ast::TypeNode& node, sema::TypeChecker& checker);

private:
    DiagnosticEngine& diag_;
};

} // namespace tamarin::backend
