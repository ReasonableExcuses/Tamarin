// ============================================================================
// lexer.h - Tamarin lexer: transforms source text into a token stream
// ============================================================================
#pragma once

#include "tamarin/lexer/token.h"
#include "tamarin/source/source_manager.h"
#include "tamarin/diagnostics/diagnostic_engine.h"

#include <deque>
#include <string_view>
#include <unordered_map>

namespace tamarin::lexer {

/// Transforms Tamarin source text into a stream of tokens.
class Lexer {
public:
    Lexer(const SourceManager& sm, SourceFileId fileId, DiagnosticEngine& diag);

    /// Lex the next token and return it.
    [[nodiscard]] Token nextToken();

    /// Peek at the next token without consuming it.
    [[nodiscard]] const Token& peek(size_t n = 0);

private:
    // ── Character access ──────────────────────────────────────────────
    [[nodiscard]] char current() const noexcept;
    [[nodiscard]] char peekChar(int offset = 1) const noexcept;
    char advance() noexcept;
    [[nodiscard]] bool isAtEnd() const noexcept;

    // ── Scanning helpers ──────────────────────────────────────────────
    void skipWhitespace();
    void skipLineComment();
    bool skipBlockComment();

    Token scanNextToken();
    Token scanIdentifierOrKeyword();
    Token scanNumber();
    Token scanString();
    Token scanCharLiteral();

    Token makeToken(TokenKind kind, uint32_t startOffset) const;
    Token makeToken(TokenKind kind, uint32_t startOffset, LiteralValue val) const;
    SourceLocation locationAt(uint32_t offset) const;
    void skipIntegerSuffix();

    // ── Keyword map ───────────────────────────────────────────────────
    static const std::unordered_map<std::string_view, TokenKind> keywords_;

    // ── State ─────────────────────────────────────────────────────────
    const SourceManager& sm_;
    SourceFileId         fileId_;
    DiagnosticEngine&    diag_;
    std::string_view     source_;
    uint32_t             pos_    = 0;
    uint32_t             line_   = 1;
    uint32_t             col_    = 1;
    std::deque<Token>    peekBuffer_;
};

} // namespace tamarin::lexer
