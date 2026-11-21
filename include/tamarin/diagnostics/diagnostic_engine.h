// ============================================================================
// diagnostic_engine.h - Compiler diagnostic reporting
// ============================================================================
#pragma once

#include "tamarin/source/source_location.h"

#include <cstdint>
#include <functional>
#include <ostream>
#include <string>

namespace tamarin {

// Forward declaration — avoids circular header dependency.
class SourceManager;

// ── Severity ────────────────────────────────────────────────────────────────
enum class Severity : uint8_t {
    Note,
    Warning,
    Error,
};

/// Convert severity to its display string (e.g. "error").
[[nodiscard]] const char* severityToString(Severity sev) noexcept;

// ── Diagnostic ──────────────────────────────────────────────────────────────
/// A single diagnostic message with its associated source location.
struct Diagnostic {
    SourceLocation location;
    Severity       severity;
    std::string    message;
};

// ── DiagnosticEngine ────────────────────────────────────────────────────────
/// Central hub for emitting, formatting, and counting compiler diagnostics.
///
/// Usage:
///   DiagnosticEngine diag(sourceManager);
///   diag.report(loc, Severity::Error, "undeclared identifier 'foo'");
///   if (diag.hasErrors()) { /* abort compilation */ }
///
class DiagnosticEngine {
public:
    /// Construct a diagnostic engine that resolves locations via `sm`.
    /// Output is written to `os` (defaults to std::cerr).
    explicit DiagnosticEngine(const SourceManager& sm,
                              std::ostream& os);

    /// Overload defaulting to std::cerr.
    explicit DiagnosticEngine(const SourceManager& sm);

    ~DiagnosticEngine() = default;

    // Non-copyable, movable
    DiagnosticEngine(const DiagnosticEngine&) = delete;
    DiagnosticEngine& operator=(const DiagnosticEngine&) = delete;
    DiagnosticEngine(DiagnosticEngine&&) noexcept = default;
    DiagnosticEngine& operator=(DiagnosticEngine&&) = delete;

    /// Report a diagnostic at the given source location.
    void report(SourceLocation loc, Severity sev, std::string message);

    /// Report a diagnostic without a source location (e.g. driver errors).
    void report(Severity sev, std::string message);

    /// True if at least one error has been reported.
    [[nodiscard]] bool hasErrors() const noexcept { return errorCount_ > 0; }

    /// Number of errors reported so far.
    [[nodiscard]] uint32_t errorCount() const noexcept { return errorCount_; }

    /// Number of warnings reported so far.
    [[nodiscard]] uint32_t warningCount() const noexcept { return warningCount_; }

    /// Return all diagnostics recorded so far.
    [[nodiscard]] const std::vector<Diagnostic>& getDiagnostics() const noexcept { return diagnostics_; }

    /// Clear all diagnostics and reset counters.
    void clearDiagnostics() noexcept {
        diagnostics_.clear();
        errorCount_ = 0;
        warningCount_ = 0;
    }

private:
    /// Format and write a single diagnostic to the output stream.
    void emit(const Diagnostic& diag);

    const SourceManager& sm_;
    std::ostream&        os_;
    uint32_t             errorCount_   = 0;
    uint32_t             warningCount_ = 0;
    std::vector<Diagnostic> diagnostics_;
};

} // namespace tamarin
