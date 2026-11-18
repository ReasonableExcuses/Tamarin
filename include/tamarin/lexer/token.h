#ifndef TAMARIN_LEXER_TOKEN_H
#define TAMARIN_LEXER_TOKEN_H

/// @file token.h
/// @brief Token types, Token struct, and SourceLocation-aware lexeme data
///        for the Tamarin lexer.

#include "tamarin/source/source_location.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

namespace tamarin::lexer {

// ─── TokenKind ──────────────────────────────────────────────────────────

/// Every distinct lexeme class produced by the lexer.
enum class TokenKind : uint16_t {
  // ── Special ──────────────────────────────────────────────────────
  Eof,
  Invalid,
  Identifier,

  // ── Literals ─────────────────────────────────────────────────────
  IntLiteral,
  FloatLiteral,
  StringLiteral,
  CharLiteral,

  // ── Keywords — primitive types ───────────────────────────────────
  Void,
  Bool,
  Char,
  Byte,
  Short,
  Int,
  Long,
  Unsigned,
  Float,
  Double,
  SizeT,
  PtrdiffT,
  U8,
  U16,
  U32,
  U64,
  U128,
  Usize,
  I8,
  I16,
  I32,
  I64,
  I128,
  Isize,

  // ── Keywords — native SIMD vectors ──────────────────────────────
  Float4,
  Float8,
  Int4,
  Int8,
  Int16,

  // ── Keywords — qualifiers & storage class ────────────────────────
  Const,
  Static,
  Extern,
  Auto,

  // ── Keywords — control flow ──────────────────────────────────────
  If,
  Else,
  While,
  For,
  Do,
  Switch,
  Case,
  Default,
  Break,
  Continue,
  Return,
  Match,

  // ── Keywords — declarations ──────────────────────────────────────
  Struct,
  Class,
  Interface,
  Enum,
  Trait,
  Impl,
  Typedef,
  Sizeof,
  Fn,

  // ── Keywords — OOP & access ──────────────────────────────────────
  Public,
  Private,
  Protected,
  Virtual,
  Override,
  This,
  Operator,

  // ── Keywords — literals / values ─────────────────────────────────
  Null,
  True,
  False,

  // ── Keywords — Tamarin extensions ────────────────────────────────
  Import,
  As,
  Region,
  Handle,
  Slab,
  Unsafe,
  Defer,
  Pub,
  Tag,
  Parallel,
  Reduce,
  Async,
  Await,
  TaskScope,
  Spawn,
  Where,
  Cold,
  Span,
  Min,
  Max,
  Atomic,
  Chan,
  Reduction,
  Select,

  // ── Operators — arithmetic ───────────────────────────────────────
  Plus,    // +
  Minus,   // -
  Star,    // *
  Slash,   // /
  Percent, // %

  // ── Operators — bitwise ──────────────────────────────────────────
  Amp,   // &
  Pipe,  // |
  Caret, // ^
  Tilde, // ~
  Bang,  // !

  // ── Operators — comparison ───────────────────────────────────────
  Lt,     // <
  Gt,     // >
  LtEq,   // <=
  GtEq,   // >=
  EqEq,   // ==
  BangEq, // !=

  // ── Operators — logical ──────────────────────────────────────────
  AmpAmp,   // &&
  PipePipe, // ||

  // ── Operators — assignment ───────────────────────────────────────
  Eq,        // =
  PlusEq,    // +=
  MinusEq,   // -=
  StarEq,    // *=
  SlashEq,   // /=
  PercentEq, // %=
  AmpEq,     // &=
  PipeEq,    // |=
  CaretEq,   // ^=
  LtLtEq,    // <<=
  GtGtEq,    // >>=

  // ── Operators — shift ────────────────────────────────────────────
  LtLt, // <<
  GtGt, // >>

  // ── Operators — increment / decrement ────────────────────────────
  PlusPlus,   // ++
  MinusMinus, // --

  // ── Operators — member access ────────────────────────────────────
  Arrow,      // ->
  FatArrow,   // =>
  Dot,        // .
  DotDot,     // ..
  ColonColon, // ::
  Colon,      // :

  // ── Punctuation ──────────────────────────────────────────────────
  Semicolon, // ;
  Comma,     // ,
  Question,  // ?
  LParen,    // (
  RParen,    // )
  LBrace,    // {
  RBrace,    // }
  LBracket,  // [
  RBracket,  // ]
  At,        // @
  Hash,      // #
};

// ─── Literal value storage ──────────────────────────────────────────────

/// Holds the parsed value of a literal token.
/// std::monostate represents "no value" (for non-literal tokens).
using LiteralValue = std::variant<std::monostate, int64_t, double, std::string>;

// ─── Token ──────────────────────────────────────────────────────────────

/// A single token produced by the lexer.
struct Token {
  TokenKind kind = TokenKind::Eof;
  std::string_view text;   ///< The raw lexeme in source.
  SourceLocation location; ///< Where it appears.
  LiteralValue value;      ///< Parsed value for literals.

  // ── Convenience predicates ───────────────────────────────────────

  [[nodiscard]] bool is(TokenKind k) const noexcept { return kind == k; }
  [[nodiscard]] bool isNot(TokenKind k) const noexcept { return kind != k; }

  template <typename... Kinds>
  [[nodiscard]] bool isOneOf(Kinds... kinds) const noexcept {
    return (is(kinds) || ...);
  }

  [[nodiscard]] bool isLiteral() const noexcept {
    return isOneOf(TokenKind::IntLiteral, TokenKind::FloatLiteral,
                   TokenKind::StringLiteral, TokenKind::CharLiteral);
  }

  [[nodiscard]] bool isKeyword() const noexcept {
    return kind >= TokenKind::Void && kind <= TokenKind::Select;
  }

  /// Get the int value (undefined if not an IntLiteral).
  [[nodiscard]] int64_t intValue() const { return std::get<int64_t>(value); }

  /// Get the float value (undefined if not a FloatLiteral).
  [[nodiscard]] double floatValue() const { return std::get<double>(value); }

  /// Get the string value (undefined if not a StringLiteral / CharLiteral).
  [[nodiscard]] const std::string &stringValue() const {
    return std::get<std::string>(value);
  }
};

// ─── Utility ────────────────────────────────────────────────────────────

/// Return a human-readable name for a TokenKind (e.g. "Identifier", "+", "if").
[[nodiscard]] const char *tokenKindToString(TokenKind kind) noexcept;

} // namespace tamarin::lexer

#endif // TAMARIN_LEXER_TOKEN_H
