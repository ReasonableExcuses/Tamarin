// ============================================================================
// source_manager.h - Source file loading and line/column resolution
// ============================================================================
#pragma once

#include "tamarin/source/source_location.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <utility>

namespace tamarin {

template<typename T, typename E>
class Expected {
    bool hasVal_{false};
    T val_{};
    E err_{};

public:
    Expected(const T& v) : hasVal_(true), val_(v) {}
    Expected(T&& v) : hasVal_(true), val_(std::move(v)) {}

    struct Unexpected { E err; };
    Expected(Unexpected u) : hasVal_(false), err_(std::move(u.err)) {}

    explicit operator bool() const noexcept { return hasVal_; }
    bool has_value() const noexcept { return hasVal_; }
    const T& value() const { return val_; }
    const T& operator*() const { return val_; }
    const E& error() const noexcept { return err_; }
};

// ── SourceFileId ────────────────────────────────────────────────────────────
/// Strongly-typed handle into the SourceManager's file registry.
enum class SourceFileId : uint32_t {};

// ── SourceFile ──────────────────────────────────────────────────────────────
/// Immutable view of a loaded source file.
struct SourceFile {
    SourceFileId    id;
    std::string     filename;
    std::string     content;

    // Lazily-built line offset table: lineOffsets[i] is the byte offset of
    // the start of line (i+1).  Built once on first line/column query.
    mutable std::vector<uint32_t> lineOffsets;
    mutable bool lineOffsetsBuilt = false;

    /// Build the line offset table from content.
    void buildLineOffsets() const;
};

// ── SourceManager ───────────────────────────────────────────────────────────
/// Owns all loaded source files and resolves byte offsets to line/column.
class SourceManager {
public:
    SourceManager() = default;
    ~SourceManager() = default;

    // Non-copyable, movable
    SourceManager(const SourceManager&) = delete;
    SourceManager& operator=(const SourceManager&) = delete;
    SourceManager(SourceManager&&) noexcept = default;
    SourceManager& operator=(SourceManager&&) noexcept = default;

    /// Load a source file from disk.  Returns its ID, or an error string.
    [[nodiscard]] Expected<SourceFileId, std::string>
    loadFile(const std::string& path);

    /// Register source text directly (e.g. for REPL or tests).
    [[nodiscard]] SourceFileId
    addSource(std::string filename, std::string content);

    /// Retrieve a loaded file by ID.
    [[nodiscard]] const SourceFile& getFile(SourceFileId id) const;

    /// Resolve a byte offset within a file to a full SourceLocation.
    [[nodiscard]] SourceLocation
    getLocation(SourceFileId fileId, uint32_t offset) const;

    /// Extract the text of a specific 1-based line number.
    [[nodiscard]] std::string_view
    getLineText(SourceFileId fileId, uint32_t line) const;

    /// Return the total number of loaded files.
    [[nodiscard]] std::size_t fileCount() const noexcept { return files_.size(); }

private:
    std::vector<SourceFile> files_;
};

} // namespace tamarin
