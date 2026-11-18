/// @file token.cpp
/// @brief tokenKindToString implementation — maps every TokenKind to a readable name.

#include "tamarin/lexer/token.h"

namespace tamarin::lexer {

const char* tokenKindToString(TokenKind kind) noexcept {
    switch (kind) {
    // ── Special ──────────────────────────────────────────────────────
    case TokenKind::Eof:            return "Eof";
    case TokenKind::Invalid:        return "Invalid";
    case TokenKind::Identifier:     return "Identifier";

    // ── Literals ─────────────────────────────────────────────────────
    case TokenKind::IntLiteral:     return "IntLiteral";
    case TokenKind::FloatLiteral:   return "FloatLiteral";
    case TokenKind::StringLiteral:  return "StringLiteral";
    case TokenKind::CharLiteral:    return "CharLiteral";

    // ── Keywords — primitive types ───────────────────────────────────
    case TokenKind::Void:           return "void";
    case TokenKind::Bool:           return "bool";
    case TokenKind::Char:           return "char";
    case TokenKind::Byte:           return "byte";
    case TokenKind::Short:          return "short";
    case TokenKind::Int:            return "int";
    case TokenKind::Long:           return "long";
    case TokenKind::Unsigned:       return "unsigned";
    case TokenKind::Float:          return "float";
    case TokenKind::Double:         return "double";
    case TokenKind::SizeT:          return "size_t";
    case TokenKind::PtrdiffT:       return "ptrdiff_t";
    case TokenKind::U8:             return "u8";
    case TokenKind::U16:            return "u16";
    case TokenKind::U32:            return "u32";
    case TokenKind::U64:            return "u64";
    case TokenKind::U128:           return "u128";
    case TokenKind::Usize:          return "usize";
    case TokenKind::I8:             return "i8";
    case TokenKind::I16:            return "i16";
    case TokenKind::I32:            return "i32";
    case TokenKind::I64:            return "i64";
    case TokenKind::I128:           return "i128";
    case TokenKind::Isize:          return "isize";
    case TokenKind::Float4:         return "float4";
    case TokenKind::Float8:         return "float8";
    case TokenKind::Int4:           return "int4";
    case TokenKind::Int8:           return "int8";
    case TokenKind::Int16:          return "int16";

    // ── Keywords — qualifiers & storage ──────────────────────────────
    case TokenKind::Const:          return "const";
    case TokenKind::Static:         return "static";
    case TokenKind::Extern:         return "extern";
    case TokenKind::Auto:           return "auto";

    // ── Keywords — control flow ──────────────────────────────────────
    case TokenKind::If:             return "if";
    case TokenKind::Else:           return "else";
    case TokenKind::While:          return "while";
    case TokenKind::For:            return "for";
    case TokenKind::Do:             return "do";
    case TokenKind::Switch:         return "switch";
    case TokenKind::Case:           return "case";
    case TokenKind::Default:        return "default";
    case TokenKind::Break:          return "break";
    case TokenKind::Continue:       return "continue";
    case TokenKind::Return:         return "return";
    case TokenKind::Match:          return "match";

    // ── Keywords — declarations ──────────────────────────────────────
    case TokenKind::Struct:         return "struct";
    case TokenKind::Class:          return "class";
    case TokenKind::Interface:      return "interface";
    case TokenKind::Enum:           return "enum";
    case TokenKind::Trait:          return "trait";
    case TokenKind::Impl:           return "impl";
    case TokenKind::Typedef:        return "typedef";
    case TokenKind::Sizeof:         return "sizeof";

    // ── Keywords — OOP & access ──────────────────────────────────────
    case TokenKind::Public:         return "public";
    case TokenKind::Private:        return "private";
    case TokenKind::Protected:      return "protected";
    case TokenKind::Virtual:        return "virtual";
    case TokenKind::Override:       return "override";
    case TokenKind::This:           return "this";
    case TokenKind::Operator:       return "operator";

    // ── Keywords — literals / values ─────────────────────────────────
    case TokenKind::Null:           return "null";
    case TokenKind::True:           return "true";
    case TokenKind::False:          return "false";

    // ── Keywords — Tamarin extensions ────────────────────────────────
    case TokenKind::Import:         return "import";
    case TokenKind::As:             return "as";
    case TokenKind::Region:         return "region";
    case TokenKind::Handle:         return "Handle";
    case TokenKind::Slab:           return "Slab";
    case TokenKind::Unsafe:         return "unsafe";
    case TokenKind::Defer:          return "defer";
    case TokenKind::Pub:            return "pub";
    case TokenKind::Tag:            return "tag";
    case TokenKind::Parallel:       return "parallel";
    case TokenKind::Reduce:         return "reduce";
    case TokenKind::Async:          return "async";
    case TokenKind::Await:          return "await";
    case TokenKind::TaskScope:      return "task_scope";
    case TokenKind::Spawn:          return "spawn";
    case TokenKind::Where:          return "where";
    case TokenKind::Cold:           return "cold";
    case TokenKind::Span:           return "span";
    case TokenKind::Min:            return "min";
    case TokenKind::Max:            return "max";
    case TokenKind::Atomic:         return "atomic";
    case TokenKind::Chan:           return "chan";
    case TokenKind::Reduction:      return "reduction";
    case TokenKind::Select:         return "select";

    // ── Operators — arithmetic ───────────────────────────────────────
    case TokenKind::Plus:           return "+";
    case TokenKind::Minus:          return "-";
    case TokenKind::Star:           return "*";
    case TokenKind::Slash:          return "/";
    case TokenKind::Percent:        return "%";

    // ── Operators — bitwise ──────────────────────────────────────────
    case TokenKind::Amp:            return "&";
    case TokenKind::Pipe:           return "|";
    case TokenKind::Caret:          return "^";
    case TokenKind::Tilde:          return "~";
    case TokenKind::Bang:           return "!";

    // ── Operators — comparison ───────────────────────────────────────
    case TokenKind::Lt:             return "<";
    case TokenKind::Gt:             return ">";
    case TokenKind::LtEq:          return "<=";
    case TokenKind::GtEq:          return ">=";
    case TokenKind::EqEq:          return "==";
    case TokenKind::BangEq:        return "!=";

    // ── Operators — logical ──────────────────────────────────────────
    case TokenKind::AmpAmp:        return "&&";
    case TokenKind::PipePipe:      return "||";

    // ── Operators — assignment ───────────────────────────────────────
    case TokenKind::Eq:            return "=";
    case TokenKind::PlusEq:        return "+=";
    case TokenKind::MinusEq:       return "-=";
    case TokenKind::StarEq:        return "*=";
    case TokenKind::SlashEq:       return "/=";
    case TokenKind::PercentEq:     return "%=";
    case TokenKind::AmpEq:         return "&=";
    case TokenKind::PipeEq:        return "|=";
    case TokenKind::CaretEq:       return "^=";
    case TokenKind::LtLtEq:       return "<<=";
    case TokenKind::GtGtEq:       return ">>=";

    // ── Operators — shift ────────────────────────────────────────────
    case TokenKind::LtLt:          return "<<";
    case TokenKind::GtGt:          return ">>";

    // ── Operators — increment / decrement ────────────────────────────
    case TokenKind::PlusPlus:      return "++";
    case TokenKind::MinusMinus:    return "--";

    // ── Operators — member access ────────────────────────────────────
    case TokenKind::Arrow:         return "->";
    case TokenKind::FatArrow:      return "=>";
    case TokenKind::Dot:           return ".";
    case TokenKind::ColonColon:    return "::";
    case TokenKind::Colon:         return ":";

    // ── Punctuation ──────────────────────────────────────────────────
    case TokenKind::Semicolon:     return ";";
    case TokenKind::Comma:         return ",";
    case TokenKind::Question:      return "?";
    case TokenKind::LParen:        return "(";
    case TokenKind::RParen:        return ")";
    case TokenKind::LBrace:        return "{";
    case TokenKind::RBrace:        return "}";
    case TokenKind::LBracket:      return "[";
    case TokenKind::RBracket:      return "]";
    case TokenKind::At:            return "@";
    case TokenKind::Hash:          return "#";
    }

    return "<unknown>";
}

} // namespace tamarin::lexer
