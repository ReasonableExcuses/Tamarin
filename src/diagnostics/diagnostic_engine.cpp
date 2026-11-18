// ============================================================================
// diagnostic_engine.cpp - Compiler diagnostic formatting and output
// ============================================================================

#include "tamarin/diagnostics/diagnostic_engine.h"
#include "tamarin/source/source_manager.h"

#include <iostream>

namespace tamarin {

// ── Severity helpers ────────────────────────────────────────────────────────

const char* severityToString(Severity sev) noexcept {
    switch (sev) {
        case Severity::Note:    return "note";
        case Severity::Warning: return "warning";
        case Severity::Error:   return "error";
    }
    return "unknown";
}

// ── DiagnosticEngine ────────────────────────────────────────────────────────

DiagnosticEngine::DiagnosticEngine(const SourceManager& sm,
                                   std::ostream& os)
    : sm_(sm), os_(os) {}

DiagnosticEngine::DiagnosticEngine(const SourceManager& sm)
    : sm_(sm), os_(std::cerr) {}

void DiagnosticEngine::report(SourceLocation loc, Severity sev,
                              std::string message) {
    Diagnostic diag{
        .location = loc,
        .severity = sev,
        .message  = std::move(message),
    };

    switch (sev) {
        case Severity::Error:   ++errorCount_;   break;
        case Severity::Warning: ++warningCount_; break;
        case Severity::Note:    break;
    }

    emit(diag);
}

void DiagnosticEngine::report(Severity sev, std::string message) {
    report(SourceLocation{}, sev, std::move(message));
}

void DiagnosticEngine::emit(const Diagnostic& diag) {
    // ── Header line: filename:line:column: severity: message ─────────────
    if (diag.location.isValid()) {
        os_ << diag.location.filename
            << ':' << diag.location.line
            << ':' << diag.location.column
            << ": ";
    }

    os_ << severityToString(diag.severity)
        << ": " << diag.message << "\n\n";

    // ── Source context with caret ────────────────────────────────────────
    if (!diag.location.isValid() || diag.location.filename.empty()) {
        return;
    }

    // Attempt to find the file in the source manager.  The filename in the
    // location is a string_view into a SourceFile's filename string, so we
    // can iterate to find the matching file and extract the line text.
    for (uint32_t i = 0; i < static_cast<uint32_t>(sm_.fileCount()); ++i) {
        auto fileId = static_cast<SourceFileId>(i);
        const auto& file = sm_.getFile(fileId);
        if (file.filename == diag.location.filename) {
            auto lineText = sm_.getLineText(fileId, diag.location.line);
            if (!lineText.empty()) {
                // Print the source line
                os_ << "    " << lineText << "\n\n";

                // Print the caret indicator
                os_ << "    ";
                // Advance to the error column, respecting tab stops
                for (uint32_t c = 0; c + 1 < diag.location.column && c < lineText.size(); ++c) {
                    os_ << (lineText[c] == '\t' ? '\t' : ' ');
                }
                os_ << '^' << "\n\n";
            }
            break;
        }
    }
}

} // namespace tamarin
