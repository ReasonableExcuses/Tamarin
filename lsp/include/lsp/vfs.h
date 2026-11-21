// ============================================================================
// vfs.h - Tamarin LSP In-Memory Virtual File System (VFS)
// ============================================================================
#pragma once

#include "lsp/protocol.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <mutex>
#include <utility>

namespace tamarin::lsp {

/// URI utility functions
std::string normalizeUri(const std::string& uri);
std::string uriToPath(const std::string& uri);
std::string pathToUri(const std::string& path);

/// An in-memory snapshot of an open text document.
class VfsDocument {
public:
    VfsDocument() = default;
    VfsDocument(std::string uri, std::string languageId, int version, std::string text);

    const std::string& uri() const { return uri_; }
    const std::string& languageId() const { return languageId_; }
    int version() const { return version_; }
    const std::string& text() const { return text_; }
    size_t lineCount() const { return lineOffsets_.size(); }

    /// Replace full text
    void update(int newVersion, std::string newText);

    /// Apply an LSP content change event (full or incremental)
    bool applyChange(int newVersion, const TextDocumentContentChangeEvent& change);

    /// Convert LSP Position (0-based UTF-16 code units) to UTF-8 byte offset
    size_t positionToOffset(const Position& pos) const;

    /// Convert UTF-8 byte offset to LSP Position (0-based UTF-16 code units)
    Position offsetToPosition(size_t byteOffset) const;

    /// Convert LSP Range to byte offset pair [start, end)
    std::pair<size_t, size_t> rangeToByteSpan(const Range& range) const;

    /// Convert byte offset pair [start, end) to LSP Range
    Range byteSpanToRange(size_t startByte, size_t endByte) const;

private:
    void rebuildLineOffsets();

    std::string uri_;
    std::string languageId_;
    int version_ = 0;
    std::string text_;
    std::vector<size_t> lineOffsets_;
};

/// Thread-safe in-memory Virtual File System tracking all editor-opened documents.
class Vfs {
public:
    Vfs() = default;

    /// Open or register a document
    void openDocument(const TextDocumentItem& item);

    /// Replace the entire text of a document
    void updateDocument(const std::string& uri, int version, const std::string& newText);

    /// Apply content changes (supports incremental range edits and full replacement)
    bool applyChanges(const DidChangeTextDocumentParams& params);

    /// Close or evict a document from in-memory tracking
    void closeDocument(const std::string& uri);

    /// Check if a document is tracked in memory
    bool hasDocument(const std::string& uri) const;

    /// Retrieve a snapshot of the document
    std::optional<VfsDocument> getDocument(const std::string& uri) const;

    /// List all currently opened document URIs
    std::vector<std::string> openDocumentUris() const;

    /// Clear all documents
    void clear();

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, VfsDocument> docs_;
};

} // namespace tamarin::lsp
