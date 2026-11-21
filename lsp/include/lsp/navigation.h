// ============================================================================
// navigation.h - Tamarin LSP Hover, Go-To-Definition, References & Symbols
// ============================================================================
#pragma once

#include "lsp/protocol.h"
#include "lsp/vfs.h"
#include "lsp/analyzer.h"

#include <optional>
#include <string>
#include <vector>

namespace tamarin::lsp {

/// Extract the identifier under the cursor at `pos` and compute its token Range
std::string getIdentifierAtPosition(const VfsDocument& doc, const Position& pos, Range* outRange = nullptr);

/// Format an AST TypeNode into a human-readable Tamarin type signature
std::string formatType(const ast::TypeNode* type);

/// Generate hover markdown tooltip for the symbol at `pos`
std::optional<Hover> getHover(const VfsDocument& doc, const AnalysisResult& analysis, const Position& pos);

/// Resolve go-to-definition target location for the symbol at `pos`
std::optional<Location> getDefinition(const VfsDocument& doc, const AnalysisResult& analysis, const Position& pos);

/// Find all references to the symbol at `pos` across the document
std::vector<Location> getReferences(const VfsDocument& doc, const AnalysisResult& analysis, const Position& pos);

/// Generate hierarchical document symbols (outline) for editor breadcrumbs
std::vector<DocumentSymbol> getDocumentSymbols(const VfsDocument& doc, const AnalysisResult& analysis);

} // namespace tamarin::lsp
