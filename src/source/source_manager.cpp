// ============================================================================
// source_manager.cpp - Source file loading and line/column resolution
// ============================================================================

#include "tamarin/source/source_manager.h"

#include <algorithm>
#include <cassert>
#include <fstream>
#include <sstream>

namespace tamarin {

// ── SourceFile ──────────────────────────────────────────────────────────────

void SourceFile::buildLineOffsets() const {
    if (lineOffsetsBuilt) return;

    lineOffsets.clear();
    lineOffsets.push_back(0);  // Line 1 starts at offset 0

    for (uint32_t i = 0; i < static_cast<uint32_t>(content.size()); ++i) {
        if (content[i] == '\n') {
            lineOffsets.push_back(i + 1);
        }
    }

    lineOffsetsBuilt = true;
}

// ── SourceManager ───────────────────────────────────────────────────────────

std::expected<SourceFileId, std::string>
SourceManager::loadFile(const std::string& path) {
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file) {
        return std::unexpected("could not open file: " + path);
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    if (file.bad()) {
        return std::unexpected("error reading file: " + path);
    }

    std::string content = ss.str();
    if (content.size() >= 3 &&
        static_cast<unsigned char>(content[0]) == 0xEF &&
        static_cast<unsigned char>(content[1]) == 0xBB &&
        static_cast<unsigned char>(content[2]) == 0xBF) {
        content.erase(0, 3);
    }

    return addSource(path, std::move(content));
}

SourceFileId
SourceManager::addSource(std::string filename, std::string content) {
    auto id = static_cast<SourceFileId>(static_cast<uint32_t>(files_.size()));
    files_.push_back(SourceFile{
        .id       = id,
        .filename = std::move(filename),
        .content  = std::move(content),
    });
    return id;
}

const SourceFile& SourceManager::getFile(SourceFileId id) const {
    auto index = static_cast<uint32_t>(id);
    assert(index < files_.size() && "invalid SourceFileId");
    return files_[index];
}

SourceLocation
SourceManager::getLocation(SourceFileId fileId, uint32_t offset) const {
    const auto& file = getFile(fileId);
    file.buildLineOffsets();

    // Binary-search for the line containing `offset`.
    // upper_bound gives the first line starting *after* offset;
    // subtracting 1 gives the line containing offset.
    auto it = std::upper_bound(file.lineOffsets.begin(),
                               file.lineOffsets.end(),
                               offset);
    uint32_t lineIndex = static_cast<uint32_t>(
        std::distance(file.lineOffsets.begin(), it)) - 1;

    uint32_t lineStart = file.lineOffsets[lineIndex];
    uint32_t column    = offset - lineStart + 1;  // 1-based

    return SourceLocation{
        .filename = file.filename,
        .line     = lineIndex + 1,  // 1-based
        .column   = column,
        .offset   = offset,
    };
}

std::string_view
SourceManager::getLineText(SourceFileId fileId, uint32_t line) const {
    const auto& file = getFile(fileId);
    file.buildLineOffsets();

    if (line == 0 || line > file.lineOffsets.size()) {
        return {};
    }

    uint32_t start = file.lineOffsets[line - 1];
    uint32_t end;

    if (line < file.lineOffsets.size()) {
        end = file.lineOffsets[line];
        // Strip the trailing newline
        if (end > start && file.content[end - 1] == '\n') {
            --end;
        }
        // Strip trailing \r for Windows line endings
        if (end > start && file.content[end - 1] == '\r') {
            --end;
        }
    } else {
        end = static_cast<uint32_t>(file.content.size());
    }

    return std::string_view(file.content).substr(start, end - start);
}

} // namespace tamarin
