// ============================================================================
// completion.h - Tamarin LSP Code Completion & Signature Help
// ============================================================================
#pragma once

#include "lsp/protocol.h"
#include "lsp/vfs.h"
#include "lsp/analyzer.h"

#include <optional>
#include <string>
#include <vector>

namespace tamarin::lsp {

/// Compute code completion candidates at the given cursor position
std::vector<CompletionItem> getCompletions(
    const VfsDocument& doc,
    const AnalysisResult& analysis,
    const Position& pos);

/// Compute signature help tooltip and active parameter at the given cursor position
std::optional<SignatureHelp> getSignatureHelp(
    const VfsDocument& doc,
    const AnalysisResult& analysis,
    const Position& pos);

} // namespace tamarin::lsp
