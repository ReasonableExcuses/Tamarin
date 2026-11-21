// ============================================================================
// analyzer.h - Tamarin LSP Document Analyzer & Diagnostic Engine
// ============================================================================
#pragma once

#include "lsp/protocol.h"
#include "lsp/vfs.h"
#include "tamarin/ast/ast.h"
#include "tamarin/sema/sema.h"
#include "tamarin/source/source_manager.h"

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>

namespace tamarin::lsp {

/// Result of parsing and analyzing an in-memory document.
struct AnalysisResult {
    ast::Program program;
    std::shared_ptr<sema::Sema> sema;
    std::vector<Diagnostic> diagnostics;
};

/// High-level analysis coordinator that runs Lexer -> Parser -> Sema
/// and converts compiler diagnostics to LSP squiggles.
class DocumentAnalyzer {
public:
    DocumentAnalyzer() = default;

    /// Parse and type-check a document snapshot, returning extracted diagnostics.
    AnalysisResult analyze(const VfsDocument& doc);

    /// Store the latest analysis result in the cache.
    void updateCache(const std::string& uri, AnalysisResult result);

    /// Query cached analysis result for hover/definition/references.
    std::shared_ptr<AnalysisResult> getCached(const std::string& uri) const;

    /// Evict cached analysis result on document close.
    void evictCache(const std::string& uri);

    /// Clear all cached results.
    void clear();

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<AnalysisResult>> cache_;
};

} // namespace tamarin::lsp
