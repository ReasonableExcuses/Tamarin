// ============================================================================
// vfs.cpp - Tamarin LSP In-Memory Virtual File System (VFS) Implementation
// ============================================================================

#include "lsp/vfs.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace tamarin::lsp {

static int hexDigitValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static std::string percentDecode(const std::string& str) {
    std::string out;
    out.reserve(str.size());
    for (size_t i = 0; i < str.size(); ++i) {
        if (str[i] == '%' && i + 2 < str.size()) {
            int h1 = hexDigitValue(str[i + 1]);
            int h2 = hexDigitValue(str[i + 2]);
            if (h1 >= 0 && h2 >= 0) {
                out.push_back(static_cast<char>((h1 << 4) | h2));
                i += 2;
                continue;
            }
        }
        out.push_back(str[i]);
    }
    return out;
}

std::string normalizeUri(const std::string& uri) {
    std::string decoded = percentDecode(uri);
    // Replace backslashes with forward slashes
    for (char& c : decoded) {
        if (c == '\\') c = '/';
    }

    // Standardize file:///c:/ vs file:///C:/
    constexpr std::string_view kFilePrefix = "file:///";
    if (decoded.size() >= kFilePrefix.size() + 2 &&
        decoded.compare(0, kFilePrefix.size(), kFilePrefix) == 0) {
        size_t driveIdx = kFilePrefix.size();
        if (std::isalpha(static_cast<unsigned char>(decoded[driveIdx])) &&
            decoded[driveIdx + 1] == ':') {
            decoded[driveIdx] = static_cast<char>(std::tolower(static_cast<unsigned char>(decoded[driveIdx])));
        }
    }

    return decoded;
}

std::string uriToPath(const std::string& uri) {
    std::string norm = normalizeUri(uri);
    constexpr std::string_view kFilePrefix = "file://";
    if (norm.compare(0, kFilePrefix.size(), kFilePrefix) != 0) {
        return norm;
    }

    std::string pathPart = norm.substr(kFilePrefix.size());
    // On Windows, file:///C:/path -> pathPart is /C:/path or /c:/path
    if (pathPart.size() >= 3 && pathPart[0] == '/' &&
        std::isalpha(static_cast<unsigned char>(pathPart[1])) && pathPart[2] == ':') {
        return pathPart.substr(1);
    }
    return pathPart;
}

std::string pathToUri(const std::string& path) {
    std::string p = path;
    for (char& c : p) {
        if (c == '\\') c = '/';
    }

    // Windows drive: C:/path -> file:///c:/path
    if (p.size() >= 2 && std::isalpha(static_cast<unsigned char>(p[0])) && p[1] == ':') {
        p[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(p[0])));
        return "file:///" + p;
    }

    if (!p.empty() && p[0] == '/') {
        return "file://" + p;
    }
    return "file:///" + p;
}

// ────────────────────────────────────────────────────────────────────────────
// VfsDocument Implementation
// ────────────────────────────────────────────────────────────────────────────

VfsDocument::VfsDocument(std::string uri, std::string languageId, int version, std::string text)
    : uri_(std::move(uri)),
      languageId_(std::move(languageId)),
      version_(version),
      text_(std::move(text)) {
    rebuildLineOffsets();
}

void VfsDocument::rebuildLineOffsets() {
    lineOffsets_.clear();
    lineOffsets_.push_back(0);
    for (size_t i = 0; i < text_.size(); ++i) {
        if (text_[i] == '\n') {
            lineOffsets_.push_back(i + 1);
        }
    }
}

void VfsDocument::update(int newVersion, std::string newText) {
    version_ = newVersion;
    text_ = std::move(newText);
    rebuildLineOffsets();
}

bool VfsDocument::applyChange(int newVersion, const TextDocumentContentChangeEvent& change) {
    version_ = newVersion;
    if (!change.range.has_value()) {
        text_ = change.text;
        rebuildLineOffsets();
        return true;
    }

    auto [startOffset, endOffset] = rangeToByteSpan(*change.range);
    if (startOffset > text_.size() || endOffset > text_.size() || startOffset > endOffset) {
        return false;
    }

    text_.replace(startOffset, endOffset - startOffset, change.text);
    rebuildLineOffsets();
    return true;
}

size_t VfsDocument::positionToOffset(const Position& pos) const {
    if (lineOffsets_.empty()) return 0;
    if (pos.line >= lineOffsets_.size()) return text_.size();

    size_t lineStart = lineOffsets_[pos.line];
    size_t lineEnd = (pos.line + 1 < lineOffsets_.size()) ? lineOffsets_[pos.line + 1] : text_.size();

    // Strip trailing \r\n or \n for line end character clamping
    if (lineEnd > lineStart && text_[lineEnd - 1] == '\n') {
        lineEnd--;
        if (lineEnd > lineStart && text_[lineEnd - 1] == '\r') {
            lineEnd--;
        }
    }

    size_t currOffset = lineStart;
    uint32_t utf16Units = 0;
    uint32_t targetUtf16 = pos.character;

    while (currOffset < lineEnd && utf16Units < targetUtf16) {
        unsigned char c = static_cast<unsigned char>(text_[currOffset]);
        size_t charLen = 1;
        uint32_t codepoint = 0;

        if (c < 0x80) {
            charLen = 1;
            codepoint = c;
        } else if ((c & 0xE0) == 0xC0) {
            charLen = 2;
            if (currOffset + 1 < lineEnd) {
                codepoint = ((c & 0x1F) << 6) | (static_cast<unsigned char>(text_[currOffset + 1]) & 0x3F);
            }
        } else if ((c & 0xF0) == 0xE0) {
            charLen = 3;
            if (currOffset + 2 < lineEnd) {
                codepoint = ((c & 0x0F) << 12) |
                            ((static_cast<unsigned char>(text_[currOffset + 1]) & 0x3F) << 6) |
                            (static_cast<unsigned char>(text_[currOffset + 2]) & 0x3F);
            }
        } else if ((c & 0xF8) == 0xF0) {
            charLen = 4;
            if (currOffset + 3 < lineEnd) {
                codepoint = ((c & 0x07) << 18) |
                            ((static_cast<unsigned char>(text_[currOffset + 1]) & 0x3F) << 12) |
                            ((static_cast<unsigned char>(text_[currOffset + 2]) & 0x3F) << 6) |
                            (static_cast<unsigned char>(text_[currOffset + 3]) & 0x3F);
            }
        }

        if (currOffset + charLen > lineEnd) {
            currOffset++;
            utf16Units++;
            continue;
        }

        uint32_t units = (codepoint > 0xFFFF) ? 2 : 1;
        if (utf16Units + units > targetUtf16) {
            break;
        }

        currOffset += charLen;
        utf16Units += units;
    }

    return currOffset;
}

Position VfsDocument::offsetToPosition(size_t byteOffset) const {
    if (byteOffset > text_.size()) byteOffset = text_.size();
    if (lineOffsets_.empty()) return Position{0, 0};

    auto it = std::upper_bound(lineOffsets_.begin(), lineOffsets_.end(), byteOffset);
    size_t lineIdx = (it == lineOffsets_.begin()) ? 0 : std::distance(lineOffsets_.begin(), it) - 1;

    size_t lineStart = lineOffsets_[lineIdx];
    size_t currOffset = lineStart;
    uint32_t utf16Units = 0;

    while (currOffset < byteOffset && currOffset < text_.size()) {
        unsigned char c = static_cast<unsigned char>(text_[currOffset]);
        size_t charLen = 1;
        uint32_t codepoint = 0;

        if (c < 0x80) {
            charLen = 1;
            codepoint = c;
        } else if ((c & 0xE0) == 0xC0) {
            charLen = 2;
            if (currOffset + 1 < text_.size()) {
                codepoint = ((c & 0x1F) << 6) | (static_cast<unsigned char>(text_[currOffset + 1]) & 0x3F);
            }
        } else if ((c & 0xF0) == 0xE0) {
            charLen = 3;
            if (currOffset + 2 < text_.size()) {
                codepoint = ((c & 0x0F) << 12) |
                            ((static_cast<unsigned char>(text_[currOffset + 1]) & 0x3F) << 6) |
                            (static_cast<unsigned char>(text_[currOffset + 2]) & 0x3F);
            }
        } else if ((c & 0xF8) == 0xF0) {
            charLen = 4;
            if (currOffset + 3 < text_.size()) {
                codepoint = ((c & 0x07) << 18) |
                            ((static_cast<unsigned char>(text_[currOffset + 1]) & 0x3F) << 12) |
                            ((static_cast<unsigned char>(text_[currOffset + 2]) & 0x3F) << 6) |
                            (static_cast<unsigned char>(text_[currOffset + 3]) & 0x3F);
            }
        }

        if (currOffset + charLen > text_.size()) {
            currOffset++;
            utf16Units++;
            continue;
        }

        uint32_t units = (codepoint > 0xFFFF) ? 2 : 1;
        currOffset += charLen;
        utf16Units += units;
    }

    return Position{static_cast<int>(lineIdx), static_cast<int>(utf16Units)};
}

std::pair<size_t, size_t> VfsDocument::rangeToByteSpan(const Range& range) const {
    size_t start = positionToOffset(range.start);
    size_t end = positionToOffset(range.end);
    if (start > end) std::swap(start, end);
    return {start, end};
}

Range VfsDocument::byteSpanToRange(size_t startByte, size_t endByte) const {
    if (startByte > endByte) std::swap(startByte, endByte);
    return Range{offsetToPosition(startByte), offsetToPosition(endByte)};
}

// ────────────────────────────────────────────────────────────────────────────
// Vfs Implementation
// ────────────────────────────────────────────────────────────────────────────

void Vfs::openDocument(const TextDocumentItem& item) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string norm = normalizeUri(item.uri);
    docs_[norm] = VfsDocument(item.uri, item.languageId, item.version, item.text);
}

void Vfs::updateDocument(const std::string& uri, int version, const std::string& newText) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string norm = normalizeUri(uri);
    auto it = docs_.find(norm);
    if (it != docs_.end()) {
        it->second.update(version, newText);
    } else {
        docs_[norm] = VfsDocument(uri, "tamarin", version, newText);
    }
}

bool Vfs::applyChanges(const DidChangeTextDocumentParams& params) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string norm = normalizeUri(params.textDocument.uri);
    auto it = docs_.find(norm);
    if (it == docs_.end()) {
        return false;
    }

    for (const auto& change : params.contentChanges) {
        if (!it->second.applyChange(params.textDocument.version, change)) {
            return false;
        }
    }
    return true;
}

void Vfs::closeDocument(const std::string& uri) {
    std::lock_guard<std::mutex> lock(mutex_);
    docs_.erase(normalizeUri(uri));
}

bool Vfs::hasDocument(const std::string& uri) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return docs_.find(normalizeUri(uri)) != docs_.end();
}

std::optional<VfsDocument> Vfs::getDocument(const std::string& uri) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = docs_.find(normalizeUri(uri));
    if (it != docs_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<std::string> Vfs::openDocumentUris() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> uris;
    uris.reserve(docs_.size());
    for (const auto& [uri, _] : docs_) {
        uris.push_back(uri);
    }
    return uris;
}

void Vfs::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    docs_.clear();
}

} // namespace tamarin::lsp
