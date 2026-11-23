// ============================================================================
// lexer.cpp - Tamarin lexer implementation
// ============================================================================

#include "tamarin/lexer/lexer.h"

#include <cctype>
#include <charconv>
#include <cstdlib>

namespace tamarin::lexer {

// ── Keyword table ───────────────────────────────────────────────────────────

const std::unordered_map<std::string_view, TokenKind> Lexer::keywords_ = {
    // Primitive types
    {"void", TokenKind::Void}, {"bool", TokenKind::Bool},
    {"char", TokenKind::Char}, {"byte", TokenKind::Byte},
    {"short", TokenKind::Short}, {"int", TokenKind::Int},
    {"long", TokenKind::Long}, {"unsigned", TokenKind::Unsigned},
    {"float", TokenKind::Float}, {"double", TokenKind::Double},
    {"size_t", TokenKind::SizeT}, {"ptrdiff_t", TokenKind::PtrdiffT},
    {"u8", TokenKind::U8}, {"u16", TokenKind::U16},
    {"u32", TokenKind::U32}, {"u64", TokenKind::U64},
    {"u128", TokenKind::U128}, {"usize", TokenKind::Usize},
    {"i8", TokenKind::I8}, {"i16", TokenKind::I16},
    {"i32", TokenKind::I32}, {"i64", TokenKind::I64},
    {"i128", TokenKind::I128}, {"isize", TokenKind::Isize},
    // SIMD vectors
    {"float4", TokenKind::Float4}, {"float8", TokenKind::Float8},
    {"int4", TokenKind::Int4}, {"int8", TokenKind::Int8}, {"int16", TokenKind::Int16},
    // Qualifiers
    {"const", TokenKind::Const}, {"static", TokenKind::Static},
    {"extern", TokenKind::Extern}, {"auto", TokenKind::Auto},
    // Control flow
    {"if", TokenKind::If}, {"else", TokenKind::Else},
    {"while", TokenKind::While}, {"for", TokenKind::For},
    {"do", TokenKind::Do}, {"switch", TokenKind::Switch},
    {"case", TokenKind::Case}, {"default", TokenKind::Default},
    {"break", TokenKind::Break}, {"continue", TokenKind::Continue},
    {"return", TokenKind::Return}, {"match", TokenKind::Match},
    // Declarations
    {"struct", TokenKind::Struct}, {"class", TokenKind::Class},
    {"interface", TokenKind::Interface}, {"enum", TokenKind::Enum},
    {"trait", TokenKind::Trait}, {"impl", TokenKind::Impl},
    {"typedef", TokenKind::Typedef}, {"sizeof", TokenKind::Sizeof},
    {"fn", TokenKind::Fn},
    // OOP & access
    {"public", TokenKind::Public}, {"private", TokenKind::Private},
    {"protected", TokenKind::Protected}, {"virtual", TokenKind::Virtual},
    {"override", TokenKind::Override}, {"this", TokenKind::This},
    {"operator", TokenKind::Operator},
    // Values
    {"null", TokenKind::Null}, {"true", TokenKind::True},
    {"false", TokenKind::False},
    // Tamarin extensions
    {"import", TokenKind::Import}, {"as", TokenKind::As}, {"region", TokenKind::Region},
    {"Handle", TokenKind::Handle}, {"Slab", TokenKind::Slab},
    {"unsafe", TokenKind::Unsafe}, {"defer", TokenKind::Defer},
    {"pub", TokenKind::Pub}, {"tag", TokenKind::Tag},
    {"parallel", TokenKind::Parallel}, {"reduce", TokenKind::Reduce},
    {"async", TokenKind::Async}, {"await", TokenKind::Await},
    {"task_scope", TokenKind::TaskScope}, {"spawn", TokenKind::Spawn}, {"where", TokenKind::Where},
    {"cold", TokenKind::Cold}, {"span", TokenKind::Span},
    {"min", TokenKind::Min}, {"max", TokenKind::Max},
    {"atomic", TokenKind::Atomic}, {"chan", TokenKind::Chan}, {"reduction", TokenKind::Reduction},
    {"select", TokenKind::Select},
};

// ── Constructor ─────────────────────────────────────────────────────────────

Lexer::Lexer(const SourceManager& sm, SourceFileId fileId, DiagnosticEngine& diag)
    : sm_(sm), fileId_(fileId), diag_(diag),
      source_(sm.getFile(fileId).content) {
    if (source_.size() >= 3 &&
        static_cast<unsigned char>(source_[0]) == 0xEF &&
        static_cast<unsigned char>(source_[1]) == 0xBB &&
        static_cast<unsigned char>(source_[2]) == 0xBF) {
        pos_ = 3;
    }
}

// ── Character access ────────────────────────────────────────────────────────

char Lexer::current() const noexcept {
    return isAtEnd() ? '\0' : source_[pos_];
}

char Lexer::peekChar(int offset) const noexcept {
    auto idx = pos_ + static_cast<uint32_t>(offset);
    return idx < source_.size() ? source_[idx] : '\0';
}

char Lexer::advance() noexcept {
    if (isAtEnd()) return '\0';
    char c = source_[pos_++];
    if (c == '\n') { ++line_; col_ = 1; }
    else { ++col_; }
    return c;
}

bool Lexer::isAtEnd() const noexcept {
    return pos_ >= source_.size();
}

// ── Token helpers ───────────────────────────────────────────────────────────

SourceLocation Lexer::locationAt(uint32_t offset) const {
    return sm_.getLocation(fileId_, offset);
}

Token Lexer::makeToken(TokenKind kind, uint32_t startOffset) const {
    return Token{
        .kind     = kind,
        .text     = source_.substr(startOffset, pos_ - startOffset),
        .location = locationAt(startOffset),
        .value    = std::monostate{},
    };
}

Token Lexer::makeToken(TokenKind kind, uint32_t startOffset, LiteralValue val) const {
    return Token{
        .kind     = kind,
        .text     = source_.substr(startOffset, pos_ - startOffset),
        .location = locationAt(startOffset),
        .value    = std::move(val),
    };
}

// ── Whitespace & comments ───────────────────────────────────────────────────

void Lexer::skipWhitespace() {
    while (!isAtEnd()) {
        char c = current();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance();
        } else if (c == '/' && peekChar() == '/') {
            skipLineComment();
        } else if (c == '/' && peekChar() == '*') {
            if (!skipBlockComment()) return;
        } else {
            break;
        }
    }
}

void Lexer::skipLineComment() {
    advance(); advance(); // consume //
    while (!isAtEnd() && current() != '\n') advance();
}

bool Lexer::skipBlockComment() {
    uint32_t startOff = pos_;
    advance(); advance(); // consume /*
    while (!isAtEnd()) {
        if (current() == '*' && peekChar() == '/') {
            advance(); advance(); // consume */
            return true;
        }
        advance();
    }
    diag_.report(locationAt(startOff), Severity::Error,
                 "unterminated block comment");
    return false;
}

// ── Identifier / keyword ───────────────────────────────────────────────────

Token Lexer::scanIdentifierOrKeyword() {
    uint32_t start = pos_;
    while (!isAtEnd() && (std::isalnum(static_cast<unsigned char>(current())) ||
                          current() == '_')) {
        advance();
    }
    auto text = source_.substr(start, pos_ - start);
    auto it = keywords_.find(text);
    if (it != keywords_.end()) {
        return makeToken(it->second, start);
    }
    return makeToken(TokenKind::Identifier, start);
}

// ── Numbers ─────────────────────────────────────────────────────────────────

Token Lexer::scanNumber() {
    uint32_t start = pos_;
    bool isFloat = false;

    // Hex: 0x...
    if (current() == '0' && (peekChar() == 'x' || peekChar() == 'X')) {
        advance(); advance();
        while (!isAtEnd() && (std::isxdigit(static_cast<unsigned char>(current())) ||
                              current() == '_')) {
            advance();
        }
        // Check for hex float (e.g., 0x1.0p5)
        if (current() == '.' && std::isxdigit(static_cast<unsigned char>(peekChar()))) {
            isFloat = true;
            advance();
            while (!isAtEnd() && (std::isxdigit(static_cast<unsigned char>(current())) ||
                                  current() == '_')) {
                advance();
            }
        }
        if (current() == 'p' || current() == 'P') {
            isFloat = true;
            advance();
            if (current() == '+' || current() == '-') advance();
            while (!isAtEnd() && std::isdigit(static_cast<unsigned char>(current()))) advance();
        }
        if (isFloat) {
            // Parse hex float
            std::string clean;
            for (auto c : source_.substr(start, pos_ - start)) {
                if (c != '_') clean += c;
            }
            double val = std::strtod(clean.c_str(), nullptr);
            // Check for float suffix
            if (pos_ + 2 < source_.size() && (source_.substr(pos_, 3) == "f32" || source_.substr(pos_, 3) == "f64")) {
                pos_ += 3;
            } else if ((current() == 'f' || current() == 'd' || current() == 'F' || current() == 'D') &&
                       !std::isalnum(static_cast<unsigned char>(peekChar()))) {
                advance();
            }
            return makeToken(TokenKind::FloatLiteral, start, val);
        }
        // Parse hex int — strip underscores
        std::string clean;
        for (auto c : source_.substr(start + 2, pos_ - start - 2)) {
            if (c != '_') clean += c;
        }
        if (clean.empty()) {
            diag_.report(locationAt(start), Severity::Error, "invalid hex literal: expected hex digits after '0x'");
            return makeToken(TokenKind::Invalid, start);
        }
        int64_t val = 0;
        auto [ptr, ec] = std::from_chars(clean.data(), clean.data() + clean.size(), val, 16);
        if (ec != std::errc()) {
            // Try unsigned
            uint64_t uval = 0;
            std::from_chars(clean.data(), clean.data() + clean.size(), uval, 16);
            val = static_cast<int64_t>(uval);
        }
        // Skip integer suffix
        skipIntegerSuffix();
        return makeToken(TokenKind::IntLiteral, start, val);
    }

    // Binary: 0b...
    if (current() == '0' && (peekChar() == 'b' || peekChar() == 'B')) {
        advance(); advance();
        while (!isAtEnd() && (current() == '0' || current() == '1' || current() == '_')) {
            advance();
        }
        std::string clean;
        for (auto c : source_.substr(start + 2, pos_ - start - 2)) {
            if (c != '_') clean += c;
        }
        if (clean.empty()) {
            diag_.report(locationAt(start), Severity::Error, "invalid binary literal: expected binary digits after '0b'");
            return makeToken(TokenKind::Invalid, start);
        }
        int64_t val = 0;
        auto [ptr, ec] = std::from_chars(clean.data(), clean.data() + clean.size(), val, 2);
        if (ec != std::errc()) {
            uint64_t uval = 0;
            std::from_chars(clean.data(), clean.data() + clean.size(), uval, 2);
            val = static_cast<int64_t>(uval);
        }
        skipIntegerSuffix();
        return makeToken(TokenKind::IntLiteral, start, val);
    }

    // Decimal
    while (!isAtEnd() && (std::isdigit(static_cast<unsigned char>(current())) ||
                          current() == '_')) {
        advance();
    }

    // Float with dot
    if (current() == '.' && std::isdigit(static_cast<unsigned char>(peekChar()))) {
        isFloat = true;
        advance(); // consume .
        while (!isAtEnd() && (std::isdigit(static_cast<unsigned char>(current())) ||
                              current() == '_')) {
            advance();
        }
    }

    // Exponent
    if (current() == 'e' || current() == 'E') {
        isFloat = true;
        advance();
        if (current() == '+' || current() == '-') advance();
        while (!isAtEnd() && (std::isdigit(static_cast<unsigned char>(current())) ||
                              current() == '_')) {
            advance();
        }
    }

    // Float suffix
    if (isFloat) {
        if (pos_ + 2 < source_.size() && (source_.substr(pos_, 3) == "f32" || source_.substr(pos_, 3) == "f64")) {
            pos_ += 3;
        } else if ((current() == 'f' || current() == 'd' || current() == 'F' || current() == 'D') &&
                   !std::isalnum(static_cast<unsigned char>(peekChar()))) {
            advance();
        }
        std::string clean;
        for (auto c : source_.substr(start, pos_ - start)) {
            if (c != '_') clean += c;
        }
        double val = std::strtod(clean.c_str(), nullptr);
        return makeToken(TokenKind::FloatLiteral, start, val);
    }

    // Check for float suffix directly on integer digits (e.g. 10f, 10d, 10f32, 10f64)
    if (pos_ + 2 < source_.size() && (source_.substr(pos_, 3) == "f32" || source_.substr(pos_, 3) == "f64")) {
        std::string clean;
        for (auto c : source_.substr(start, pos_ - start)) {
            if (c != '_') clean += c;
        }
        pos_ += 3;
        double val = std::strtod(clean.c_str(), nullptr);
        return makeToken(TokenKind::FloatLiteral, start, val);
    }
    if ((current() == 'f' || current() == 'd' || current() == 'F' || current() == 'D') &&
        !std::isalnum(static_cast<unsigned char>(peekChar()))) {
        std::string clean;
        for (auto c : source_.substr(start, pos_ - start)) {
            if (c != '_') clean += c;
        }
        advance();
        double val = std::strtod(clean.c_str(), nullptr);
        return makeToken(TokenKind::FloatLiteral, start, val);
    }

    // Integer
    std::string clean;
    for (auto c : source_.substr(start, pos_ - start)) {
        if (c != '_') clean += c;
    }
    int64_t val = 0;
    auto [ptr, ec] = std::from_chars(clean.data(), clean.data() + clean.size(), val);
    if (ec != std::errc()) {
        uint64_t uval = 0;
        std::from_chars(clean.data(), clean.data() + clean.size(), uval);
        val = static_cast<int64_t>(uval);
    }
    skipIntegerSuffix();
    return makeToken(TokenKind::IntLiteral, start, val);
}

// ── String ──────────────────────────────────────────────────────────────────

Token Lexer::scanString() {
    uint32_t start = pos_;
    advance(); // consume opening "
    std::string value;

    while (!isAtEnd() && current() != '"') {
        if (current() == '\n') {
            diag_.report(locationAt(pos_), Severity::Error,
                         "unterminated string literal");
            return makeToken(TokenKind::Invalid, start);
        }
        if (current() == '\\') {
            advance();
            switch (current()) {
                case 'n':  value += '\n'; advance(); break;
                case 'r':  value += '\r'; advance(); break;
                case 't':  value += '\t'; advance(); break;
                case '0':  value += '\0'; advance(); break;
                case '\\': value += '\\'; advance(); break;
                case '"':  value += '"';  advance(); break;
                case '\'': value += '\''; advance(); break;
                case 'u': {
                    advance(); // consume u
                    if (current() != '{') {
                        diag_.report(locationAt(pos_), Severity::Error,
                                     "expected '{' after \\u");
                        return makeToken(TokenKind::Invalid, start);
                    }
                    advance(); // consume {
                    std::string hex;
                    while (!isAtEnd() && current() != '}') {
                        hex += current();
                        advance();
                    }
                    if (current() == '}') advance();
                    unsigned long cp = std::strtoul(hex.c_str(), nullptr, 16);
                    // Encode as UTF-8
                    if (cp <= 0x7F) {
                        value += static_cast<char>(cp);
                    } else if (cp <= 0x7FF) {
                        value += static_cast<char>(0xC0 | (cp >> 6));
                        value += static_cast<char>(0x80 | (cp & 0x3F));
                    } else if (cp <= 0xFFFF) {
                        value += static_cast<char>(0xE0 | (cp >> 12));
                        value += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                        value += static_cast<char>(0x80 | (cp & 0x3F));
                    } else if (cp <= 0x10FFFF) {
                        value += static_cast<char>(0xF0 | (cp >> 18));
                        value += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                        value += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                        value += static_cast<char>(0x80 | (cp & 0x3F));
                    }
                    break;
                }
                default:
                    diag_.report(locationAt(pos_ - 1), Severity::Error,
                                 std::string("invalid escape sequence: \\") + current());
                    advance();
                    break;
            }
        } else {
            value += current();
            advance();
        }
    }

    if (isAtEnd()) {
        diag_.report(locationAt(start), Severity::Error,
                     "unterminated string literal");
        return makeToken(TokenKind::Invalid, start);
    }
    advance(); // consume closing "
    return makeToken(TokenKind::StringLiteral, start, std::move(value));
}

// ── Character literal ───────────────────────────────────────────────────────

Token Lexer::scanCharLiteral() {
    uint32_t start = pos_;
    advance(); // consume opening '
    std::string value;

    if (current() == '\\') {
        advance();
        switch (current()) {
            case 'n':  value += '\n'; advance(); break;
            case 'r':  value += '\r'; advance(); break;
            case 't':  value += '\t'; advance(); break;
            case '0':  value += '\0'; advance(); break;
            case '\\': value += '\\'; advance(); break;
            case '\'': value += '\''; advance(); break;
            case '"':  value += '"';  advance(); break;
            default:
                diag_.report(locationAt(pos_ - 1), Severity::Error,
                             std::string("invalid escape sequence in character literal: \\") + current());
                advance();
                break;
        }
    } else if (!isAtEnd() && current() != '\'') {
        unsigned char lead = static_cast<unsigned char>(current());
        int extraBytes = 0;
        if ((lead & 0xE0) == 0xC0) extraBytes = 1;
        else if ((lead & 0xF0) == 0xE0) extraBytes = 2;
        else if ((lead & 0xF8) == 0xF0) extraBytes = 3;
        value += current();
        advance();
        while (extraBytes-- > 0 && !isAtEnd() && (static_cast<unsigned char>(current()) & 0xC0) == 0x80) {
            value += current();
            advance();
        }
    }

    if (value.empty()) {
        diag_.report(locationAt(start), Severity::Error,
                     "empty character literal");
        if (current() == '\'') advance();
        return makeToken(TokenKind::Invalid, start);
    }

    if (current() != '\'') {
        diag_.report(locationAt(start), Severity::Error,
                     "unterminated character literal");
        return makeToken(TokenKind::Invalid, start);
    }
    advance(); // consume closing '
    return makeToken(TokenKind::CharLiteral, start, std::move(value));
}

// ── Main lex ────────────────────────────────────────────────────────────────

const Token& Lexer::peek(size_t n) {
    while (peekBuffer_.size() <= n) {
        peekBuffer_.push_back(scanNextToken());
    }
    return peekBuffer_[n];
}

Token Lexer::nextToken() {
    if (!peekBuffer_.empty()) {
        Token tok = std::move(peekBuffer_.front());
        peekBuffer_.pop_front();
        return tok;
    }
    return scanNextToken();
}

Token Lexer::scanNextToken() {
    skipWhitespace();

    if (isAtEnd()) {
        return makeToken(TokenKind::Eof, pos_);
    }

    uint32_t start = pos_;
    char c = current();

    // Identifiers and keywords
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
        return scanIdentifierOrKeyword();
    }

    // Numbers
    if (std::isdigit(static_cast<unsigned char>(c))) {
        return scanNumber();
    }

    // Strings
    if (c == '"') return scanString();

    // Character literals
    if (c == '\'') return scanCharLiteral();

    // Operators and punctuation
    advance();
    switch (c) {
        case '(': return makeToken(TokenKind::LParen,    start);
        case ')': return makeToken(TokenKind::RParen,    start);
        case '{': return makeToken(TokenKind::LBrace,    start);
        case '}': return makeToken(TokenKind::RBrace,    start);
        case '[': return makeToken(TokenKind::LBracket,  start);
        case ']': return makeToken(TokenKind::RBracket,  start);
        case ';': return makeToken(TokenKind::Semicolon, start);
        case ',': return makeToken(TokenKind::Comma,     start);
        case '~': return makeToken(TokenKind::Tilde,     start);
        case '?': return makeToken(TokenKind::Question,  start);
        case '@': return makeToken(TokenKind::At,        start);
        case '#': return makeToken(TokenKind::Hash,      start);

        case '.':
            if (current() == '.') { advance(); return makeToken(TokenKind::DotDot, start); }
            return makeToken(TokenKind::Dot,       start);

        case ':':
            if (current() == ':') { advance(); return makeToken(TokenKind::ColonColon, start); }
            return makeToken(TokenKind::Colon, start);

        case '+':
            if (current() == '+') { advance(); return makeToken(TokenKind::PlusPlus,  start); }
            if (current() == '=') { advance(); return makeToken(TokenKind::PlusEq,    start); }
            return makeToken(TokenKind::Plus, start);

        case '-':
            if (current() == '-') { advance(); return makeToken(TokenKind::MinusMinus, start); }
            if (current() == '=') { advance(); return makeToken(TokenKind::MinusEq,    start); }
            if (current() == '>') { advance(); return makeToken(TokenKind::Arrow,      start); }
            return makeToken(TokenKind::Minus, start);

        case '*':
            if (current() == '=') { advance(); return makeToken(TokenKind::StarEq,    start); }
            return makeToken(TokenKind::Star, start);

        case '/':
            if (current() == '=') { advance(); return makeToken(TokenKind::SlashEq,   start); }
            return makeToken(TokenKind::Slash, start);

        case '%':
            if (current() == '=') { advance(); return makeToken(TokenKind::PercentEq, start); }
            return makeToken(TokenKind::Percent, start);

        case '&':
            if (current() == '&') { advance(); return makeToken(TokenKind::AmpAmp,    start); }
            if (current() == '=') { advance(); return makeToken(TokenKind::AmpEq,     start); }
            return makeToken(TokenKind::Amp, start);

        case '|':
            if (current() == '|') { advance(); return makeToken(TokenKind::PipePipe,  start); }
            if (current() == '=') { advance(); return makeToken(TokenKind::PipeEq,    start); }
            return makeToken(TokenKind::Pipe, start);

        case '^':
            if (current() == '=') { advance(); return makeToken(TokenKind::CaretEq,   start); }
            return makeToken(TokenKind::Caret, start);

        case '!':
            if (current() == '=') { advance(); return makeToken(TokenKind::BangEq,    start); }
            return makeToken(TokenKind::Bang, start);

        case '=':
            if (current() == '=') { advance(); return makeToken(TokenKind::EqEq,      start); }
            if (current() == '>') { advance(); return makeToken(TokenKind::FatArrow,  start); }
            return makeToken(TokenKind::Eq, start);

        case '<':
            if (current() == '<') {
                advance();
                if (current() == '=') { advance(); return makeToken(TokenKind::LtLtEq, start); }
                return makeToken(TokenKind::LtLt, start);
            }
            if (current() == '=') { advance(); return makeToken(TokenKind::LtEq,      start); }
            return makeToken(TokenKind::Lt, start);

        case '>':
            if (current() == '>') {
                advance();
                if (current() == '=') { advance(); return makeToken(TokenKind::GtGtEq, start); }
                return makeToken(TokenKind::GtGt, start);
            }
            if (current() == '=') { advance(); return makeToken(TokenKind::GtEq,      start); }
            return makeToken(TokenKind::Gt, start);

        default:
            diag_.report(locationAt(start), Severity::Error,
                         std::string("unexpected character: '") + c + "'");
            return makeToken(TokenKind::Invalid, start);
    }
}

// ── Suffix helper ───────────────────────────────────────────────────────────

void Lexer::skipIntegerSuffix() {
    // Check for integer suffixes: u8, u16, u32, u64, u128, usize, i8, i16, i32, i64, i128, isize
    // or standard C suffixes: u, l, ul, lu, ll, ull, llu (case-insensitive)
    if (isAtEnd()) return;
    auto safeChar = [&](size_t offset) -> char {
        return (pos_ + offset < source_.size()) ? source_[pos_ + offset] : '\0';
    };
    uint32_t saved = pos_;
    if (current() == 'u' || current() == 'i') {
        advance();
        if (current() == '8' && !std::isalnum(static_cast<unsigned char>(peekChar()))) {
            advance(); return;
        }
        if (current() == '1') {
            advance();
            if (current() == '6' && !std::isalnum(static_cast<unsigned char>(peekChar()))) { advance(); return; }
            if (current() == '2' && peekChar() == '8' && !std::isalnum(static_cast<unsigned char>(safeChar(2)))) {
                advance(); advance(); return;
            }
            pos_ = saved;
        } else if (current() == '3' && peekChar() == '2' && !std::isalnum(static_cast<unsigned char>(safeChar(2)))) {
            advance(); advance(); return;
        } else if (current() == '6' && peekChar() == '4' && !std::isalnum(static_cast<unsigned char>(safeChar(2)))) {
            advance(); advance(); return;
        } else if (source_.substr(pos_, 4) == "size" &&
                   (pos_ + 4 >= source_.size() || !std::isalnum(static_cast<unsigned char>(source_[pos_ + 4])))) {
            pos_ += 4; return;
        } else {
            pos_ = saved; // rollback to test C suffixes
        }
    }

    // Standard C suffixes
    char c = current();
    if (c == 'u' || c == 'U' || c == 'l' || c == 'L') {
        size_t len = 0;
        char c0 = static_cast<char>(std::tolower(static_cast<unsigned char>(safeChar(0))));
        char c1 = static_cast<char>(std::tolower(static_cast<unsigned char>(safeChar(1))));
        char c2 = static_cast<char>(std::tolower(static_cast<unsigned char>(safeChar(2))));
        if ((c0 == 'u' && c1 == 'l' && c2 == 'l') || (c0 == 'l' && c1 == 'l' && c2 == 'u')) {
            len = 3;
        } else if ((c0 == 'u' && c1 == 'l') || (c0 == 'l' && c1 == 'u') || (c0 == 'l' && c1 == 'l')) {
            len = 2;
        } else if (c0 == 'u' || c0 == 'l') {
            len = 1;
        }
        if (len > 0) {
            char after = safeChar(len);
            if (!std::isalnum(static_cast<unsigned char>(after)) && after != '_') {
                pos_ += static_cast<uint32_t>(len);
                return;
            }
        }
    }
}

} // namespace tamarin::lexer

