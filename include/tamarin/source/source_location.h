// ============================================================================
// source_location.h - Source code position and range types
// ============================================================================
#pragma once

#include <cstdint>
#include <string_view>

namespace tamarin {

// ── SourceLocation ──────────────────────────────────────────────────────────
/// A precise position within a source file.
struct SourceLocation {
    std::string_view filename{};
    uint32_t line   = 0;  // 1-based line number
    uint32_t column = 0;  // 1-based column number
    uint32_t offset = 0;  // 0-based byte offset from start of file

    [[nodiscard]] constexpr bool isValid() const noexcept {
        return line > 0 && column > 0;
    }

    constexpr bool operator==(const SourceLocation&) const noexcept = default;
    constexpr auto operator<=>(const SourceLocation& other) const noexcept {
        return offset <=> other.offset;
    }
};

// ── SourceRange ─────────────────────────────────────────────────────────────
/// A contiguous range within a source file, defined by [begin, end).
struct SourceRange {
    SourceLocation begin{};
    SourceLocation end{};

    [[nodiscard]] constexpr bool isValid() const noexcept {
        return begin.isValid() && end.isValid();
    }

    [[nodiscard]] constexpr bool contains(const SourceLocation& loc) const noexcept {
        return loc.offset >= begin.offset && loc.offset < end.offset;
    }

    constexpr bool operator==(const SourceRange&) const noexcept = default;
};

} // namespace tamarin
