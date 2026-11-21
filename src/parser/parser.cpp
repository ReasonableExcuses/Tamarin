// ============================================================================
// parser.cpp - Tamarin recursive descent parser implementation
// ============================================================================

#include "tamarin/parser/parser.h"
#include <stdexcept>
#include <format>

namespace tamarin::parser {

using namespace lexer;
using namespace ast;

Parser::Parser(Lexer& lex, DiagnosticEngine& diag)
    : lex_(lex), diag_(diag), current_(lex.nextToken()) {
    knownTypes_.insert("Option");
    knownTypes_.insert("Result");
}

// ═══════════════════════════════════════════════════════════════════════════
//  Token utilities
// ═══════════════════════════════════════════════════════════════════════════

const Token& Parser::peek() { return current_; }

Token Parser::advance() {
    Token prev = std::move(current_);
    current_ = lex_.nextToken();
    return prev;
}

bool Parser::check(TokenKind kind) { return current_.kind == kind; }

bool Parser::checkIdentifier() {
    return current_.kind == TokenKind::Identifier ||
           current_.kind == TokenKind::Min ||
           current_.kind == TokenKind::Max;
}

std::string Parser::parseIdentifier(const std::string& msg) {
    if (checkIdentifier()) {
        auto name = std::string(current_.text);
        advance();
        return name;
    }
    diag_.report(current_.location, Severity::Error, msg);
    hadError_ = true;
    return std::string(advance().text);
}

std::string Parser::parseQualifiedIdentifier(const std::string& msg) {
    auto name = parseIdentifier(msg);
    while (match(TokenKind::ColonColon)) {
        name += "::" + parseIdentifier("expected identifier after '::'");
    }
    return name;
}

bool Parser::match(TokenKind kind) {
    if (check(kind)) { advance(); return true; }
    return false;
}

Token Parser::expect(TokenKind kind, const std::string& msg) {
    if (check(kind)) return advance();
    diag_.report(current_.location, Severity::Error, msg);
    hadError_ = true;
    // Try to recover by advancing
    return advance();
}

bool Parser::expectClosingAngle(const std::string& msg) {
    if (check(TokenKind::Gt)) {
        advance();
        return true;
    }
    if (check(TokenKind::GtGt)) {
        current_.kind = TokenKind::Gt;
        current_.text = ">";
        return true;
    }
    diag_.report(current_.location, Severity::Error, msg);
    hadError_ = true;
    return false;
}

void Parser::error(const std::string& msg) {
    diag_.report(current_.location, Severity::Error, msg);
    hadError_ = true;
    throw std::runtime_error("parse error");
}

// ═══════════════════════════════════════════════════════════════════════════
//  Type checking helper
// ═══════════════════════════════════════════════════════════════════════════

bool Parser::isTypeNameKind(TokenKind k) const {
    return k == TokenKind::Auto || k == TokenKind::Fn ||
           k == TokenKind::Void || k == TokenKind::Bool ||
           k == TokenKind::Char || k == TokenKind::Byte ||
           k == TokenKind::Short || k == TokenKind::Int ||
           k == TokenKind::Long || k == TokenKind::Unsigned ||
           k == TokenKind::Float || k == TokenKind::Double ||
           k == TokenKind::SizeT || k == TokenKind::PtrdiffT ||
           k == TokenKind::U8 || k == TokenKind::U16 ||
           k == TokenKind::U32 || k == TokenKind::U64 || k == TokenKind::U128 ||
           k == TokenKind::Usize || k == TokenKind::I8 || k == TokenKind::I16 ||
           k == TokenKind::I32 || k == TokenKind::I64 || k == TokenKind::I128 ||
           k == TokenKind::Isize || k == TokenKind::Handle || k == TokenKind::Slab ||
           k == TokenKind::Float4 || k == TokenKind::Float8 ||
           k == TokenKind::Int4 || k == TokenKind::Int8 || k == TokenKind::Int16;
}

bool Parser::isTypeName() {
    auto k = current_.kind;
    if (k == TokenKind::Star) {
        size_t offset = 0;
        while (lex_.peek(offset).kind == TokenKind::Star) {
            offset++;
        }
        auto nextTok = lex_.peek(offset);
        auto nextK = nextTok.kind;
        if (isTypeNameKind(nextK) || nextK == TokenKind::Const ||
            nextK == TokenKind::Struct || nextK == TokenKind::Class ||
            nextK == TokenKind::Interface) {
            return true;
        }
        if (nextK == TokenKind::Identifier) {
            std::string name(nextTok.text);
            if (knownTypes_.count(name)) return true;
            if (lex_.peek(offset + 1).kind == TokenKind::ColonColon &&
                lex_.peek(offset + 2).kind == TokenKind::Identifier) {
                return true;
            }
            if (lex_.peek(offset + 1).kind == TokenKind::Lt) {
                return true;
            }
            if (lex_.peek(offset + 1).kind == TokenKind::Identifier) {
                return true;
            }
        }
        return false;
    }
    if (isTypeNameKind(k) || k == TokenKind::Const || k == TokenKind::Struct ||
        k == TokenKind::Class || k == TokenKind::Interface) {
        return true;
    }
    if (k == TokenKind::Identifier) {
        std::string name(current_.text);
        if (knownTypes_.count(name)) return true;
        if ((name == "Handle" || name == "Slab" || name == "slab" || name == "Option" || name == "Result" || name == "Task") &&
            (lex_.peek().kind == TokenKind::Lt || lex_.peek().kind == TokenKind::LParen)) {
            return true;
        }

        auto isGenericTypeAhead = [this](size_t startOffset) -> bool {
            if (lex_.peek(startOffset).kind != TokenKind::Lt) return false;
            int depth = 1;
            size_t idx = startOffset + 1;
            while (depth > 0 && idx < 64) {
                auto tk = lex_.peek(idx).kind;
                if (tk == TokenKind::Eof || tk == TokenKind::Semicolon || tk == TokenKind::LBrace || tk == TokenKind::RBrace) return false;
                if (tk == TokenKind::Plus || tk == TokenKind::Minus || tk == TokenKind::Slash ||
                    tk == TokenKind::Percent || tk == TokenKind::EqEq || tk == TokenKind::BangEq ||
                    tk == TokenKind::LtEq || tk == TokenKind::GtEq || tk == TokenKind::AmpAmp ||
                    tk == TokenKind::PipePipe || tk == TokenKind::Eq || tk == TokenKind::Question) {
                    return false;
                }
                if (tk == TokenKind::Lt) depth++;
                else if (tk == TokenKind::Gt) depth--;
                else if (tk == TokenKind::GtGt) depth -= 2;
                idx++;
            }
            if (depth <= 0) {
                auto after = lex_.peek(idx).kind;
                return after == TokenKind::Identifier || after == TokenKind::Amp ||
                       after == TokenKind::LBracket || after == TokenKind::RParen ||
                       after == TokenKind::Comma;
            }
            return false;
        };

        if (lex_.peek(0).kind == TokenKind::ColonColon) {
            if (lex_.peek(1).kind == TokenKind::Identifier) {
                std::string qName = name + "::" + std::string(lex_.peek(1).text);
                if (knownTypes_.count(qName)) return true;
                auto k2 = lex_.peek(2).kind;
                if (k2 == TokenKind::Identifier || k2 == TokenKind::Amp) {
                    return true;
                }
                if (isGenericTypeAhead(2)) {
                    return true;
                }
            }
        }

        if (isGenericTypeAhead(0)) {
            return true;
        }

        // If an identifier is followed by an identifier or &, it's a type name in a variable declaration (e.g. Vec2 v or Point& p)
        auto nextKind = lex_.peek().kind;
        if (nextKind == TokenKind::Identifier || nextKind == TokenKind::Amp) return true;
    }
    if (k == TokenKind::LParen) {
        int depth = 1;
        size_t idx = 0;
        bool hasComma = false;
        while (depth > 0 && idx < 64) {
            auto tk = lex_.peek(idx).kind;
            if (tk == TokenKind::Eof || tk == TokenKind::Semicolon || tk == TokenKind::LBrace || tk == TokenKind::RBrace) return false;
            if (tk == TokenKind::LParen) depth++;
            else if (tk == TokenKind::RParen) depth--;
            else if (tk == TokenKind::Comma && depth == 1) hasComma = true;
            idx++;
        }
        if (depth == 0) {
            auto after = lex_.peek(idx).kind;
            if (after == TokenKind::Arrow) return true;
            if (hasComma) {
                return after == TokenKind::Identifier || after == TokenKind::Amp;
            }
        }
    }
    return false;
}

bool Parser::isGenericCallLookahead() {
    int depth = 1;
    size_t i = 0;
    while (true) {
        const auto& tok = lex_.peek(i++);
        if (tok.kind == TokenKind::Eof || tok.kind == TokenKind::Semicolon ||
            tok.kind == TokenKind::LBrace || tok.kind == TokenKind::RBrace ||
            tok.kind == TokenKind::Eq || tok.kind == TokenKind::Plus ||
            tok.kind == TokenKind::Minus || tok.kind == TokenKind::Star ||
            tok.kind == TokenKind::Slash || tok.kind == TokenKind::Percent ||
            tok.kind == TokenKind::AmpAmp || tok.kind == TokenKind::PipePipe ||
            tok.kind == TokenKind::EqEq || tok.kind == TokenKind::BangEq ||
            tok.kind == TokenKind::LtEq || tok.kind == TokenKind::GtEq) {
            return false;
        }
        if (tok.kind == TokenKind::Lt) {
            depth++;
        } else if (tok.kind == TokenKind::Gt) {
            depth--;
            if (depth == 0) {
                return lex_.peek(i).kind == TokenKind::LParen;
            }
        } else if (tok.kind == TokenKind::GtGt) {
            depth -= 2;
            if (depth == 0) {
                return lex_.peek(i).kind == TokenKind::LParen;
            }
            if (depth < 0) return false;
        }
        if (i > 32) return false;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Type parsing
// ═══════════════════════════════════════════════════════════════════════════

TypePtr Parser::parseType() {
    auto loc = current_.location;
    bool isConst = match(TokenKind::Const);

    if (match(TokenKind::Star)) {
        bool pointeeConst = match(TokenKind::Const);
        auto pointee = parseType();
        return std::make_unique<ReferenceTypeNode>(std::move(pointee), isConst || pointeeConst, loc, /*isPtr=*/true);
    }

    TypePtr baseType;

    // Map keywords to PrimitiveKind
    auto mapPrimitive = [](TokenKind k) -> std::optional<PrimitiveKind> {
        switch (k) {
            case TokenKind::Void:     return PrimitiveKind::Void;
            case TokenKind::Bool:     return PrimitiveKind::Bool;
            case TokenKind::Char:     return PrimitiveKind::Char;
            case TokenKind::Byte:     return PrimitiveKind::Byte;
            case TokenKind::Short:    return PrimitiveKind::Short;
            case TokenKind::Int:      return PrimitiveKind::Int;
            case TokenKind::Long:     return PrimitiveKind::Long;
            case TokenKind::Float:    return PrimitiveKind::Float;
            case TokenKind::Double:   return PrimitiveKind::Double;
            case TokenKind::SizeT:    return PrimitiveKind::SizeT;
            case TokenKind::PtrdiffT: return PrimitiveKind::PtrdiffT;
            case TokenKind::U8:       return PrimitiveKind::U8;
            case TokenKind::U16:      return PrimitiveKind::U16;
            case TokenKind::U32:      return PrimitiveKind::U32;
            case TokenKind::U64:      return PrimitiveKind::U64;
            case TokenKind::U128:     return PrimitiveKind::U128;
            case TokenKind::Usize:    return PrimitiveKind::Usize;
            case TokenKind::I8:       return PrimitiveKind::I8;
            case TokenKind::I16:      return PrimitiveKind::I16;
            case TokenKind::I32:      return PrimitiveKind::I32;
            case TokenKind::I64:      return PrimitiveKind::I64;
            case TokenKind::I128:     return PrimitiveKind::I128;
            case TokenKind::Isize:    return PrimitiveKind::Isize;
            case TokenKind::Float4:   return PrimitiveKind::Float4;
            case TokenKind::Float8:   return PrimitiveKind::Float8;
            case TokenKind::Int4:     return PrimitiveKind::Int4;
            case TokenKind::Int8:     return PrimitiveKind::Int8;
            case TokenKind::Int16:    return PrimitiveKind::Int16;
            default: return std::nullopt;
        }
    };

    // Handle 'auto'
    if (match(TokenKind::Auto)) {
        baseType = std::make_unique<AutoTypeNode>(loc, isConst);
    }
    // Handle function type: fn(T1, T2, ...) -> Ret
    else if (match(TokenKind::Fn)) {
        expect(TokenKind::LParen, "expected '(' after 'fn'");
        std::vector<TypePtr> params;
        if (!check(TokenKind::RParen)) {
            params.push_back(parseType());
            while (match(TokenKind::Comma)) {
                params.push_back(parseType());
            }
        }
        expect(TokenKind::RParen, "expected ')' after fn parameter types");
        expect(TokenKind::Arrow, "expected '->' in fn type");
        auto retType = parseType();
        baseType = std::make_unique<FunctionTypeNode>(std::move(params), std::move(retType), loc);
    }
    // Handle 'unsigned' prefix
    else if (check(TokenKind::Unsigned)) {
        advance();
        if (check(TokenKind::Short)) { advance(); baseType = std::make_unique<PrimitiveTypeNode>(PrimitiveKind::UnsignedShort, loc); }
        else if (check(TokenKind::Int))   { advance(); baseType = std::make_unique<PrimitiveTypeNode>(PrimitiveKind::UnsignedInt, loc); }
        else if (check(TokenKind::Long))  { advance(); baseType = std::make_unique<PrimitiveTypeNode>(PrimitiveKind::UnsignedLong, loc); }
        else if (check(TokenKind::Char))  { advance(); baseType = std::make_unique<PrimitiveTypeNode>(PrimitiveKind::UnsignedChar, loc); }
        // bare "unsigned" means unsigned int
        else baseType = std::make_unique<PrimitiveTypeNode>(PrimitiveKind::UnsignedInt, loc);
    }
    // Primitive types
    else if (auto pk = mapPrimitive(current_.kind); pk.has_value()) {
        advance();
        baseType = std::make_unique<PrimitiveTypeNode>(*pk, loc);
    }
    // Handle 'Handle<T>'
    else if (check(TokenKind::Handle) || (check(TokenKind::Identifier) && current_.text == "Handle" && lex_.peek().kind == TokenKind::Lt)) {
        advance();
        expect(TokenKind::Lt, "expected '<' after Handle");
        auto inner = parseType();
        expectClosingAngle("expected '>' after Handle type argument");
        baseType = std::make_unique<HandleTypeNode>(std::move(inner), loc);
    }
    // Handle 'Task<T>'
    else if (check(TokenKind::Identifier) && current_.text == "Task" && lex_.peek().kind == TokenKind::Lt) {
        advance();
        expect(TokenKind::Lt, "expected '<' after Task");
        auto inner = parseType();
        expectClosingAngle("expected '>' after Task type argument");
        baseType = std::make_unique<TaskTypeNode>(std::move(inner), loc);
    }
    // Handle 'Slab<T>' or 'slab(T, capacity)'
    else if (check(TokenKind::Slab) || (check(TokenKind::Identifier) && (current_.text == "Slab" || current_.text == "slab") && (lex_.peek().kind == TokenKind::Lt || lex_.peek().kind == TokenKind::LParen))) {
        advance();
        if (match(TokenKind::Lt)) {
            auto elem = parseType();
            expectClosingAngle("expected '>' after Slab element type");
            baseType = std::make_unique<SlabTypeNode>(std::move(elem), nullptr, loc);
        } else if (match(TokenKind::LParen)) {
            auto elem = parseType();
            ExprPtr cap = nullptr;
            if (match(TokenKind::Comma)) {
                cap = parseExpr();
            }
            expect(TokenKind::RParen, "expected ')' after slab parameters");
            baseType = std::make_unique<SlabTypeNode>(std::move(elem), std::move(cap), loc);
        } else {
            diag_.report(loc, Severity::Error, "expected '<' or '(' after Slab");
            baseType = std::make_unique<PrimitiveTypeNode>(PrimitiveKind::Int, loc);
        }
    }
    // Handle 'struct', 'class', 'interface', 'enum' prefixes e.g. "struct Vec2", "enum Color"
    else if (check(TokenKind::Struct) || check(TokenKind::Class) || check(TokenKind::Interface) || check(TokenKind::Enum)) {
        advance();
        auto name = parseQualifiedIdentifier("expected type name");
        knownTypes_.insert(name);
        std::vector<TypePtr> typeArgs;
        if (match(TokenKind::Lt)) {
            while (!check(TokenKind::Gt) && !check(TokenKind::GtGt) && !check(TokenKind::Eof)) {
                typeArgs.push_back(parseType());
                if (!match(TokenKind::Comma)) break;
            }
            expectClosingAngle("expected '>' after type arguments");
        }
        baseType = std::make_unique<NamedTypeNode>(std::move(name), std::move(typeArgs), loc);
    }
    // Handle tuple type: (T1, T2, ...)
    else if (match(TokenKind::LParen)) {
        std::vector<TypePtr> elements;
        bool hasComma = false;
        if (!check(TokenKind::RParen)) {
            elements.push_back(parseType());
            while (match(TokenKind::Comma)) {
                hasComma = true;
                if (check(TokenKind::RParen)) break;
                elements.push_back(parseType());
            }
        }
        expect(TokenKind::RParen, "expected ')' after tuple type");
        if (match(TokenKind::Arrow)) {
            auto retType = parseType();
            baseType = std::make_unique<FunctionTypeNode>(std::move(elements), std::move(retType), loc);
        } else if (elements.size() == 1 && !hasComma) {
            baseType = std::move(elements[0]);
        } else {
            baseType = std::make_unique<TupleTypeNode>(std::move(elements), loc);
        }
    }
    // Named type (struct/class names, Option<T>, etc.)
    else if (check(TokenKind::Identifier)) {
        auto name = parseQualifiedIdentifier("expected type name");
        std::vector<TypePtr> typeArgs;
        if (match(TokenKind::Lt)) {
            while (!check(TokenKind::Gt) && !check(TokenKind::GtGt) && !check(TokenKind::Eof)) {
                typeArgs.push_back(parseType());
                if (!match(TokenKind::Comma)) break;
            }
            expectClosingAngle("expected '>' after type arguments");
        }
        baseType = std::make_unique<NamedTypeNode>(std::move(name), std::move(typeArgs), loc);
    } else {
        diag_.report(loc, Severity::Error, "expected type name");
        hadError_ = true;
        baseType = std::make_unique<PrimitiveTypeNode>(PrimitiveKind::Int, loc); // recovery
    }

    // Check for array suffix [] or [N] on type, e.g. char[] or int[10]
    std::vector<ExprPtr> typeDims;
    while (match(TokenKind::LBracket)) {
        ExprPtr sz = nullptr;
        if (!check(TokenKind::RBracket)) {
            sz = parseExpr();
        }
        expect(TokenKind::RBracket, "expected ']' in array type");
        typeDims.push_back(std::move(sz));
    }
    if (!typeDims.empty()) {
        for (size_t k = typeDims.size(); k-- > 0;) {
            baseType = std::make_unique<ArrayTypeNode>(std::move(baseType), std::move(typeDims[k]), loc, isConst);
        }
    }

    // Check for reference & or pointer *
    while (true) {
        if (match(TokenKind::Amp)) {
            baseType = std::make_unique<ReferenceTypeNode>(std::move(baseType), isConst, loc, /*isPtr=*/false);
        } else if (match(TokenKind::Star)) {
            baseType = std::make_unique<ReferenceTypeNode>(std::move(baseType), isConst, loc, /*isPtr=*/true);
        } else {
            break;
        }
    }

    return baseType;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Program and declarations
// ═══════════════════════════════════════════════════════════════════════════

ast::ImportDecl Parser::parseImport() {
    auto loc = current_.location;
    expect(TokenKind::Import, "expected 'import'");

    std::string modulePath = parseIdentifier("expected module path");
    std::string lastComponent = modulePath;

    while (match(TokenKind::Dot)) {
        auto comp = parseIdentifier("expected module path component after '.'");
        modulePath += "." + comp;
        lastComponent = comp;
    }

    std::string alias = lastComponent;
    if (match(TokenKind::As)) {
        alias = parseIdentifier("expected alias after 'as'");
    }

    expect(TokenKind::Semicolon, "expected ';' after import declaration");
    return ast::ImportDecl{
        .modulePath = std::move(modulePath),
        .alias = std::move(alias),
        .loc = loc
    };
}

std::shared_ptr<ast::CfgExpr> Parser::parseCfgExpr() {
    expect(TokenKind::LParen, "expected '(' after '@cfg'");
    auto expr = parseCfgItem();
    expect(TokenKind::RParen, "expected ')' after '@cfg' expression");
    return expr;
}

std::shared_ptr<ast::CfgExpr> Parser::parseCfgItem() {
    auto loc = current_.location;
    auto name = parseIdentifier("expected predicate name or combinator in '@cfg'");
    if (name == "all" || name == "any") {
        ast::CfgOp op = (name == "all") ? ast::CfgOp::All : ast::CfgOp::Any;
        expect(TokenKind::LParen, "expected '(' after '" + name + "'");
        std::vector<std::shared_ptr<ast::CfgExpr>> children;
        if (!check(TokenKind::RParen)) {
            while (true) {
                children.push_back(parseCfgItem());
                if (match(TokenKind::Comma)) {
                    if (check(TokenKind::RParen)) break;
                    continue;
                }
                break;
            }
        }
        expect(TokenKind::RParen, "expected ')' after '" + name + "(...)'");
        return std::make_shared<ast::CfgExpr>(op, "", std::move(children), loc);
    } else if (name == "not") {
        expect(TokenKind::LParen, "expected '(' after 'not'");
        auto child = parseCfgItem();
        expect(TokenKind::RParen, "expected ')' after 'not(...)'");
        std::vector<std::shared_ptr<ast::CfgExpr>> children;
        if (child) children.push_back(std::move(child));
        return std::make_shared<ast::CfgExpr>(ast::CfgOp::Not, "", std::move(children), loc);
    } else if (name == "target_os") {
        expect(TokenKind::Eq, "expected '=' after 'target_os'");
        std::string val;
        if (check(TokenKind::StringLiteral)) {
            val = current_.stringValue();
            advance();
        } else {
            val = parseIdentifier("expected OS name string or identifier");
        }
        return std::make_shared<ast::CfgExpr>(ast::CfgOp::TargetOS, std::move(val), std::vector<std::shared_ptr<ast::CfgExpr>>{}, loc);
    } else if (name == "target_arch") {
        expect(TokenKind::Eq, "expected '=' after 'target_arch'");
        std::string val;
        if (check(TokenKind::StringLiteral)) {
            val = current_.stringValue();
            advance();
        } else {
            val = parseIdentifier("expected architecture name string or identifier");
        }
        return std::make_shared<ast::CfgExpr>(ast::CfgOp::TargetArch, std::move(val), std::vector<std::shared_ptr<ast::CfgExpr>>{}, loc);
    } else {
        diag_.report(loc, Severity::Error, "unknown @cfg predicate or combinator '" + name + "'");
        return nullptr;
    }
}

Program Parser::parseProgram() {
    Program prog;

    // 1. Imports at the beginning of the file (can have @cfg)
    while (true) {
        if (check(TokenKind::Import)) {
            try {
                prog.imports.push_back(parseImport());
            } catch (const std::runtime_error&) {
                while (!check(TokenKind::Eof) && !check(TokenKind::Semicolon)) {
                    advance();
                }
                if (check(TokenKind::Semicolon)) advance();
            }
        } else if (check(TokenKind::At) && lex_.peek().kind == TokenKind::Identifier && lex_.peek().text == "cfg") {
            try {
                advance(); // eat '@'
                advance(); // eat 'cfg'
                auto cfg = parseCfgExpr();
                if (check(TokenKind::Import)) {
                    auto imp = parseImport();
                    imp.cfg = cfg;
                    if (!cfg || cfg->evaluate(targetTriple_)) {
                        prog.imports.push_back(std::move(imp));
                    }
                } else {
                    // Not followed by import -> top-level declaration with this cfg
                    if (auto d = parseTopDecl(/*defaultExtern=*/false, /*defaultLinkage=*/"", cfg)) {
                        if (!d->cfg || d->cfg->evaluate(targetTriple_)) {
                            prog.decls.push_back(std::move(d));
                        }
                    }
                    break;
                }
            } catch (const std::runtime_error&) {
                while (!check(TokenKind::Eof) && !check(TokenKind::Semicolon) && !check(TokenKind::RBrace)) {
                    advance();
                }
                if (check(TokenKind::Semicolon) || check(TokenKind::RBrace)) advance();
            }
        } else {
            break;
        }
    }

    // 2. Declarations
    while (!check(TokenKind::Eof)) {
        try {
            if (check(TokenKind::Import)) {
                diag_.report(current_.location, Severity::Error,
                             "import declarations must appear at the beginning of the file before ordinary declarations");
                hadError_ = true;
                advance();
                continue;
            }
            if (check(TokenKind::Extern)) {
                advance();
                std::string linkage = "C";
                if (check(TokenKind::StringLiteral)) {
                    linkage = current_.stringValue();
                    advance();
                }
                if (match(TokenKind::LBrace)) {
                    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
                        if (auto d = parseTopDecl(/*defaultExtern=*/true, linkage)) {
                            if (!d->cfg || d->cfg->evaluate(targetTriple_)) {
                                prog.decls.push_back(std::move(d));
                            }
                        }
                    }
                    expect(TokenKind::RBrace, "expected '}' after extern block");
                    continue;
                } else {
                    if (auto d = parseTopDecl(/*defaultExtern=*/true, linkage)) {
                        if (!d->cfg || d->cfg->evaluate(targetTriple_)) {
                            prog.decls.push_back(std::move(d));
                        }
                    }
                    continue;
                }
            }
            if (auto d = parseTopDecl()) {
                if (!d->cfg || d->cfg->evaluate(targetTriple_)) {
                    prog.decls.push_back(std::move(d));
                }
            } else {
                if (!check(TokenKind::Eof)) advance();
            }
        } catch (const std::runtime_error&) {
            // Skip to next semicolon or brace for recovery, or next top-level declaration keyword
            while (!check(TokenKind::Eof) && !check(TokenKind::Semicolon) &&
                   !check(TokenKind::RBrace)) {
                if (isTypeName() || check(TokenKind::Struct) ||
                    check(TokenKind::Class) || check(TokenKind::Enum) ||
                    check(TokenKind::Trait) || check(TokenKind::Impl) ||
                    check(TokenKind::Import) || check(TokenKind::Extern) ||
                    check(TokenKind::At)) {
                    break;
                }
                advance();
            }
            if (check(TokenKind::Semicolon) || check(TokenKind::RBrace)) advance();
        }
    }
    return prog;
}

DeclPtr Parser::parseTopDecl(bool defaultExtern, const std::string& defaultLinkage, std::shared_ptr<ast::CfgExpr> initialCfg) {
    auto loc = current_.location;
    bool isPublic = false;
    bool isNoInline = false;
    bool isAlwaysInline = false;
    bool isExtern = defaultExtern;
    std::string linkage = defaultLinkage;
    bool isExported = false;
    bool isAsync = false;
    LayoutDirective layout = LayoutDirective::Default;
    std::shared_ptr<ast::CfgExpr> cfg = std::move(initialCfg);

    while (true) {
        if (!isPublic && match(TokenKind::Public)) {
            isPublic = true;
            continue;
        }
        if (!isAsync && match(TokenKind::Async)) {
            isAsync = true;
            continue;
        }
        if (match(TokenKind::Extern)) {
            isExtern = true;
            if (check(TokenKind::StringLiteral)) {
                linkage = current_.stringValue();
                advance();
            } else if (linkage.empty()) {
                linkage = "C";
            }
            continue;
        }
        if (check(TokenKind::At)) {
            advance();
            auto attr = parseIdentifier("expected attribute name after '@'");
            if (attr == "cfg") {
                cfg = parseCfgExpr();
            } else if (attr == "noinline") {
                isNoInline = true;
            } else if (attr == "inline" || attr == "always_inline") {
                isAlwaysInline = true;
            } else if (attr == "export") {
                isExported = true;
                if (linkage.empty()) linkage = "C";
            } else if (attr == "c_layout") {
                layout = LayoutDirective::CLayout;
            } else if (attr == "layout") {
                expect(TokenKind::LParen, "expected '(' after '@layout'");
                auto kind = parseIdentifier("expected layout kind ('point', 'scan', 'grouped')");
                expect(TokenKind::RParen, "expected ')' after layout kind");
                if (kind == "point") layout = LayoutDirective::Point;
                else if (kind == "scan" || kind == "soa") layout = LayoutDirective::Scan;
                else if (kind == "grouped") layout = LayoutDirective::Grouped;
                else diag_.report(loc, Severity::Error, "unknown layout kind '" + kind + "'");
            } else {
                diag_.report(loc, Severity::Error, "unknown top-level attribute '@" + attr + "'");
            }
            continue;
        }
        break;
    }

    auto attachCfg = [&](DeclPtr d) -> DeclPtr {
        if (d && cfg) {
            d->cfg = cfg;
        }
        return d;
    };

    if (check(TokenKind::Struct)) {
        return attachCfg(parseStructOrClassDecl(layout, loc, /*isClass=*/false, /*isInterface=*/false, isPublic));
    }
    if (check(TokenKind::Class)) {
        return attachCfg(parseStructOrClassDecl(layout, loc, /*isClass=*/true, /*isInterface=*/false, isPublic));
    }
    if (check(TokenKind::Interface)) {
        return attachCfg(parseStructOrClassDecl(layout, loc, /*isClass=*/false, /*isInterface=*/true, isPublic));
    }
    if (check(TokenKind::Enum)) {
        return attachCfg(parseEnumDecl(isPublic));
    }
    if (check(TokenKind::Trait)) {
        return attachCfg(parseTraitDecl(isPublic));
    }
    if (check(TokenKind::Impl)) {
        return attachCfg(parseImplDecl());
    }

    if (layout != LayoutDirective::Default) {
        diag_.report(loc, Severity::Error, "layout attributes may only be applied to struct or class declarations");
    }

    bool isConst = false;

    if (check(TokenKind::Const)) {
        isConst = true;
        advance();
    }

    auto type = parseType();
    auto name = parseIdentifier("expected declaration name");

    std::vector<std::string> typeParams;
    std::vector<std::pair<std::string, std::string>> traitBounds;
    if (match(TokenKind::Lt)) {
        while (!check(TokenKind::Gt) && !check(TokenKind::GtGt) && !check(TokenKind::Eof)) {
            auto pName = parseIdentifier("expected type parameter name");
            typeParams.push_back(pName);
            if (match(TokenKind::Colon)) {
                auto bName = parseIdentifier("expected trait bound name");
                traitBounds.push_back({pName, bName});
            }
            if (!match(TokenKind::Comma)) break;
        }
        expectClosingAngle("expected '>' after function type parameters");
    }

    // Function declaration
    if (check(TokenKind::LParen)) {
        return attachCfg(parseFunctionDecl(std::move(type), name, loc, isConst, isPublic, isNoInline, isAlwaysInline, std::move(typeParams), std::move(traitBounds), isExtern, linkage, isExported, isAsync));
    }

    bool isArray = false;
    ExprPtr arraySize;
    if (type->kind == TypeNodeKind::Array) {
        auto at = static_cast<ArrayTypeNode*>(type.get());
        isArray = true;
        arraySize = std::move(at->sizeExpr);
        if (at->isConst) isConst = true;
        type = std::move(at->elementType);
    }

    std::vector<ExprPtr> gDims;
    while (check(TokenKind::LBracket)) {
        advance();
        ExprPtr sz = nullptr;
        if (!check(TokenKind::RBracket)) {
            sz = parseExpr();
        }
        expect(TokenKind::RBracket, "expected ']'");
        gDims.push_back(std::move(sz));
    }
    if (!gDims.empty()) {
        isArray = true;
        arraySize = std::move(gDims[0]);
        for (size_t k = gDims.size() - 1; k >= 1; --k) {
            type = std::make_unique<ArrayTypeNode>(std::move(type), std::move(gDims[k]), loc, isConst);
        }
    }

    // Global const
    if (isConst) {
        expect(TokenKind::Eq, "expected '=' in const declaration");
        auto value = parseExpr();
        expect(TokenKind::Semicolon, "expected ';' after const declaration");
        return attachCfg(std::make_unique<ConstDecl>(std::move(type), std::move(name),
                                                     std::move(value), loc, isPublic));
    }

    // Global variable
    ExprPtr init;
    if (match(TokenKind::Eq)) {
        init = parseExpr();
    }
    expect(TokenKind::Semicolon, "expected ';' after variable declaration");
    return attachCfg(std::make_unique<GlobalVarDecl>(std::move(type), std::move(name),
                                                    std::move(init), isConst, isArray,
                                                    std::move(arraySize), loc, isPublic));
}

DeclPtr Parser::parseStructOrClassDecl(LayoutDirective layout, SourceLocation loc, bool isClass, bool isInterface, bool isPublic) {
    if (isInterface) {
        expect(TokenKind::Interface, "expected 'interface'");
    } else if (isClass) {
        expect(TokenKind::Class, "expected 'class'");
    } else {
        expect(TokenKind::Struct, "expected 'struct'");
    }

    auto name = parseIdentifier("expected name");
    knownTypes_.insert(name);

    std::vector<std::string> typeParams;
    std::vector<std::pair<std::string, std::string>> traitBounds;
    if (match(TokenKind::Lt)) {
        while (!check(TokenKind::Gt) && !check(TokenKind::GtGt) && !check(TokenKind::Eof)) {
            auto pName = parseIdentifier("expected type parameter name");
            typeParams.push_back(pName);
            knownTypes_.insert(pName);
            if (match(TokenKind::Colon)) {
                auto bName = parseIdentifier("expected trait bound name");
                traitBounds.push_back({pName, bName});
            }
            if (!match(TokenKind::Comma)) break;
        }
        expectClosingAngle("expected '>' after struct type parameters");
    }

    if (match(TokenKind::Where)) {
        do {
            auto pName = parseIdentifier("expected type parameter in where clause");
            expect(TokenKind::Colon, "expected ':' after type parameter in where clause");
            auto bName = parseIdentifier("expected trait bound in where clause");
            traitBounds.push_back({pName, bName});
        } while (match(TokenKind::Comma));
    }

    if (match(TokenKind::Semicolon)) {
        for (const auto& tp : typeParams) {
            knownTypes_.erase(tp);
        }
        return std::make_unique<StructDecl>(name, std::move(typeParams), std::move(traitBounds), std::vector<StructField>{}, layout, loc, isClass, isInterface, isPublic);
    }

    std::vector<BaseClassSpec> baseClasses;
    if (match(TokenKind::Colon)) {
        do {
            auto bLoc = current_.location;
            AccessSpecifier bAccess = isClass ? AccessSpecifier::Private : AccessSpecifier::Public;
            if (match(TokenKind::Public)) bAccess = AccessSpecifier::Public;
            else if (match(TokenKind::Private)) bAccess = AccessSpecifier::Private;
            else if (match(TokenKind::Protected)) bAccess = AccessSpecifier::Protected;
            auto bName = parseQualifiedIdentifier("expected base class name");
            baseClasses.push_back(BaseClassSpec{std::move(bName), bAccess, bLoc});
        } while (match(TokenKind::Comma));
    }

    expect(TokenKind::LBrace, "expected '{' after declaration name");

    AccessSpecifier curAccess = isClass ? AccessSpecifier::Private : AccessSpecifier::Public;
    if (isInterface) curAccess = AccessSpecifier::Public;

    std::vector<StructField> fields;
    std::vector<MethodDecl> methods;
    std::vector<ConstructorDecl> constructors;
    std::unique_ptr<DestructorDecl> destructor;

    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
        if (check(TokenKind::Public) || check(TokenKind::Private) || check(TokenKind::Protected)) {
            auto specKind = current_.kind;
            advance();
            expect(TokenKind::Colon, "expected ':' after access specifier");
            if (specKind == TokenKind::Public) curAccess = AccessSpecifier::Public;
            else if (specKind == TokenKind::Private) curAccess = AccessSpecifier::Private;
            else if (specKind == TokenKind::Protected) curAccess = AccessSpecifier::Protected;
            continue;
        }

        auto mLoc = current_.location;
        bool isVirtual = false;
        bool isAsync = false;
        if (match(TokenKind::Virtual)) {
            isVirtual = true;
        }
        if (match(TokenKind::Async)) {
            isAsync = true;
        }

        if (match(TokenKind::Tilde)) {
            auto dtorName = parseIdentifier("expected destructor name");
            expect(TokenKind::LParen, "expected '(' after destructor name");
            expect(TokenKind::RParen, "expected ')' after destructor parameters");
            std::unique_ptr<BlockStmt> body;
            if (match(TokenKind::Semicolon)) {
                // empty or pure virtual declaration
            } else {
                body = parseBlock();
            }
            destructor = std::make_unique<DestructorDecl>(DestructorDecl{
                dtorName, std::move(body), isVirtual, curAccess, mLoc
            });
            continue;
        }

        // Constructor: ClassName(params) { ... }
        if (check(TokenKind::Identifier) && current_.text == name && lex_.peek().kind == TokenKind::LParen) {
            auto ctorName = parseIdentifier("expected constructor name");
            expect(TokenKind::LParen, "expected '(' after constructor name");
            std::vector<ParamDecl> params;
            if (!check(TokenKind::RParen)) {
                do {
                    params.push_back(parseParam());
                } while (match(TokenKind::Comma));
            }
            expect(TokenKind::RParen, "expected ')' after constructor parameters");
            auto body = parseBlock();
            constructors.push_back(ConstructorDecl{
                std::move(ctorName), std::move(params), std::move(body), curAccess, mLoc
            });
            continue;
        }

        bool fCold = false;
        if (check(TokenKind::At)) {
            advance();
            auto attr = parseIdentifier("expected attribute name");
            if (attr == "cold") fCold = true;
            else diag_.report(mLoc, Severity::Error, "unknown field attribute '@" + attr + "'");
        }

        bool isConst = match(TokenKind::Const);
        auto memberType = parseType();
        if (isConst && memberType) {
            if (memberType->kind == TypeNodeKind::Array) {
                static_cast<ArrayTypeNode*>(memberType.get())->isConst = true;
            } else if (memberType->kind == TypeNodeKind::Reference) {
                static_cast<ReferenceTypeNode*>(memberType.get())->isConst = true;
            }
        }
        bool isOperatorMethod = false;
        std::string memberName;
        if (check(TokenKind::Operator)) {
            advance(); // consume 'operator'
            isOperatorMethod = true;
            if (match(TokenKind::Plus)) memberName = "operator+";
            else if (match(TokenKind::Minus)) memberName = "operator-";
            else if (match(TokenKind::Star)) memberName = "operator*";
            else if (match(TokenKind::Slash)) memberName = "operator/";
            else if (match(TokenKind::Percent)) memberName = "operator%";
            else if (match(TokenKind::EqEq)) memberName = "operator==";
            else if (match(TokenKind::BangEq)) memberName = "operator!=";
            else if (match(TokenKind::LtEq)) memberName = "operator<=";
            else if (match(TokenKind::GtEq)) memberName = "operator>=";
            else if (match(TokenKind::Lt)) memberName = "operator<";
            else if (match(TokenKind::Gt)) memberName = "operator>";
            else if (match(TokenKind::Amp)) memberName = "operator&";
            else if (match(TokenKind::Pipe)) memberName = "operator|";
            else if (match(TokenKind::Caret)) memberName = "operator^";
            else if (match(TokenKind::Bang)) memberName = "operator!";
            else if (match(TokenKind::Tilde)) memberName = "operator~";
            else if (match(TokenKind::LBracket)) {
                expect(TokenKind::RBracket, "expected ']' in 'operator[]'");
                memberName = "operator[]";
            } else {
                error("expected operator symbol after 'operator'");
            }
        } else {
            memberName = parseIdentifier("expected member name");
        }

        if (isOperatorMethod || check(TokenKind::LParen)) {
            // Method
            expect(TokenKind::LParen, "expected '(' after method name");
            std::vector<ParamDecl> params;
            if (!check(TokenKind::RParen)) {
                do {
                    params.push_back(parseParam());
                } while (match(TokenKind::Comma));
            }
            expect(TokenKind::RParen, "expected ')' after method parameters");

            bool isConstMethod = match(TokenKind::Const);
            bool isOverride = match(TokenKind::Override);
            bool isPureVirtual = false;

            if (match(TokenKind::Eq)) {
                expect(TokenKind::IntLiteral, "expected 0 for pure virtual method");
                isPureVirtual = true;
                isVirtual = true;
            }

            std::unique_ptr<BlockStmt> body;
            if (isPureVirtual) {
                expect(TokenKind::Semicolon, "expected ';' after pure virtual method");
            } else if (match(TokenKind::Semicolon)) {
                // declaration only
            } else {
                body = parseBlock();
            }

            MethodDecl md{
                std::move(memberType), std::move(memberName), std::move(params),
                std::move(body), isConstMethod, isVirtual, isOverride, isPureVirtual,
                isOperatorMethod, curAccess, mLoc
            };
            md.isAsync = isAsync;
            methods.push_back(std::move(md));
        } else {
            // Field
            while (check(TokenKind::LBracket)) {
                advance();
                ExprPtr sz = nullptr;
                if (!check(TokenKind::RBracket)) sz = parseExpr();
                expect(TokenKind::RBracket, "expected ']'");
                memberType = std::make_unique<ArrayTypeNode>(std::move(memberType), std::move(sz), mLoc, isConst);
            }
            expect(TokenKind::Semicolon, "expected ';' after field declaration");
            fields.push_back(StructField{
                std::move(memberType), std::move(memberName), isConst, fCold, curAccess, mLoc
            });
        }
    }

    expect(TokenKind::RBrace, "expected '}' after body");
    expect(TokenKind::Semicolon, "expected ';' after declaration");

    for (const auto& tp : typeParams) {
        knownTypes_.erase(tp);
    }

    auto decl = std::make_unique<StructDecl>(name, std::move(typeParams), std::move(traitBounds), std::move(fields), layout, loc, isClass, isInterface, isPublic);
    decl->methods = std::move(methods);
    decl->constructors = std::move(constructors);
    decl->destructor = std::move(destructor);
    decl->baseClasses = std::move(baseClasses);
    return decl;
}

DeclPtr Parser::parseEnumDecl(bool isPublic) {
    auto loc = current_.location;
    expect(TokenKind::Enum, "expected 'enum'");
    auto name = parseIdentifier("expected enum name");
    knownTypes_.insert(name);

    std::vector<std::string> typeParams;
    if (match(TokenKind::Lt)) {
        while (!check(TokenKind::Gt) && !check(TokenKind::Eof)) {
            typeParams.push_back(parseIdentifier("expected type parameter name"));
            if (!match(TokenKind::Comma)) break;
        }
        expect(TokenKind::Gt, "expected '>' after enum type parameters");
    }

    expect(TokenKind::LBrace, "expected '{' after enum name");
    std::vector<EnumVariant> variants;

    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
        auto vLoc = current_.location;
        auto vName = parseIdentifier("expected variant name");
        std::vector<TypePtr> payloads;
        if (match(TokenKind::LParen)) {
            while (!check(TokenKind::RParen) && !check(TokenKind::Eof)) {
                payloads.push_back(parseType());
                if (!match(TokenKind::Comma)) break;
            }
            expect(TokenKind::RParen, "expected ')' after variant payload");
        }
        variants.emplace_back(std::move(vName), std::move(payloads), vLoc);
        match(TokenKind::Comma); // optional trailing comma
    }
    expect(TokenKind::RBrace, "expected '}' after enum variants");
    match(TokenKind::Semicolon); // optional trailing semicolon for enum

    if (!typeParams.empty()) {
        return std::make_unique<EnumDecl>(std::move(name), std::move(typeParams), std::move(variants), loc, isPublic);
    }
    return std::make_unique<EnumDecl>(std::move(name), std::move(variants), loc, isPublic);
}

DeclPtr Parser::parseTraitDecl(bool isPublic) {
    auto loc = current_.location;
    expect(TokenKind::Trait, "expected 'trait'");
    auto name = parseIdentifier("expected trait name");
    knownTypes_.insert(name);

    std::vector<std::string> typeParams;
    if (match(TokenKind::Lt)) {
        while (!check(TokenKind::Gt) && !check(TokenKind::GtGt) && !check(TokenKind::Eof)) {
            typeParams.push_back(parseIdentifier("expected type parameter name"));
            if (!match(TokenKind::Comma)) break;
        }
        expectClosingAngle("expected '>' after trait type parameters");
    }

    expect(TokenKind::LBrace, "expected '{' after trait name");
    std::vector<TraitMethod> methods;
    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
        auto mLoc = current_.location;
        auto retType = parseType();
        auto mName = parseIdentifier("expected method name");
        expect(TokenKind::LParen, "expected '(' after method name");
        std::vector<ParamDecl> params;
        if (!check(TokenKind::RParen)) {
            do {
                params.push_back(parseParam());
            } while (match(TokenKind::Comma));
        }
        expect(TokenKind::RParen, "expected ')' after method parameters");
        std::unique_ptr<BlockStmt> defaultBody;
        if (match(TokenKind::Semicolon)) {
            // pure trait signature
        } else if (check(TokenKind::LBrace)) {
            defaultBody = parseBlock();
        } else {
            diag_.report(current_.location, Severity::Error, "expected ';' or '{' in trait method declaration");
            advance();
        }
        methods.push_back(TraitMethod{std::move(retType), std::move(mName), std::move(params), std::move(defaultBody), mLoc});
    }
    expect(TokenKind::RBrace, "expected '}' after trait body");
    match(TokenKind::Semicolon);
    return std::make_unique<TraitDecl>(std::move(name), std::move(typeParams), std::move(methods), loc, isPublic);
}

DeclPtr Parser::parseImplDecl() {
    auto loc = current_.location;
    expect(TokenKind::Impl, "expected 'impl'");

    std::vector<std::string> typeParams;
    if (match(TokenKind::Lt)) {
        while (!check(TokenKind::Gt) && !check(TokenKind::GtGt) && !check(TokenKind::Eof)) {
            auto pName = parseIdentifier("expected type parameter name");
            typeParams.push_back(pName);
            knownTypes_.insert(pName);
            if (!match(TokenKind::Comma)) break;
        }
        expectClosingAngle("expected '>' after impl type parameters");
    }

    auto traitName = parseIdentifier("expected trait name");
    expect(TokenKind::For, "expected 'for' in impl block");
    auto targetType = parseType();

    expect(TokenKind::LBrace, "expected '{' after impl target type");
    std::vector<MethodDecl> methods;
    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
        auto mLoc = current_.location;
        auto retType = parseType();
        auto mName = parseIdentifier("expected method name");
        expect(TokenKind::LParen, "expected '(' after method name");
        std::vector<ParamDecl> params;
        if (!check(TokenKind::RParen)) {
            do {
                params.push_back(parseParam());
            } while (match(TokenKind::Comma));
        }
        expect(TokenKind::RParen, "expected ')' after method parameters");
        auto body = parseBlock();
        methods.push_back(MethodDecl{
            std::move(retType), std::move(mName), std::move(params),
            std::move(body), false, false, false, false, false,
            AccessSpecifier::Public, mLoc
        });
    }
    expect(TokenKind::RBrace, "expected '}' after impl body");
    match(TokenKind::Semicolon);

    for (const auto& tp : typeParams) {
        knownTypes_.erase(tp);
    }

    return std::make_unique<ImplDecl>(std::move(traitName), std::move(targetType), std::move(typeParams), std::move(methods), loc);
}

DeclPtr Parser::parseFunctionDecl(TypePtr retType, const std::string& name,
                                   SourceLocation loc, bool isConst, bool isPublic,
                                   bool isNoInline, bool isAlwaysInline,
                                   std::vector<std::string> typeParams,
                                   std::vector<std::pair<std::string, std::string>> traitBounds,
                                   bool isExtern,
                                   const std::string& linkage,
                                   bool isExported,
                                   bool isAsync) {
    for (const auto& tp : typeParams) {
        knownTypes_.insert(tp);
    }

    if (isConst && retType) {
        if (retType->kind == TypeNodeKind::Array) {
            static_cast<ArrayTypeNode*>(retType.get())->isConst = true;
        } else if (retType->kind == TypeNodeKind::Reference) {
            static_cast<ReferenceTypeNode*>(retType.get())->isConst = true;
        }
    }
    expect(TokenKind::LParen, "expected '('");

    std::vector<ParamDecl> params;
    if (check(TokenKind::Void) && lex_.peek().kind == TokenKind::RParen) {
        advance(); // consume void in f(void)
    } else if (!check(TokenKind::RParen)) {
        do {
            params.push_back(parseParam());
        } while (match(TokenKind::Comma));
    }

    expect(TokenKind::RParen, "expected ')'");

    if (match(TokenKind::Where)) {
        do {
            auto pName = parseIdentifier("expected type parameter in where clause");
            expect(TokenKind::Colon, "expected ':' after type parameter in where clause");
            auto bName = parseIdentifier("expected trait bound in where clause");
            traitBounds.push_back({pName, bName});
        } while (match(TokenKind::Comma));
    }

    std::unique_ptr<BlockStmt> body = nullptr;
    if (match(TokenKind::Semicolon)) {
        // Prototype / forward declaration / extern declaration without body
        isExtern = true;
    } else {
        body = parseBlock();
    }

    for (const auto& tp : typeParams) {
        knownTypes_.erase(tp);
    }

    auto fnDecl = std::make_unique<FunctionDecl>(
        std::move(retType), std::string(name), std::move(typeParams), std::move(traitBounds),
        std::move(params), std::move(body), loc, isPublic, isNoInline, isAlwaysInline);
    fnDecl->isExtern = isExtern;
    fnDecl->linkage = linkage;
    fnDecl->isExported = isExported;
    fnDecl->isAsync = isAsync;
    return fnDecl;
}

ParamDecl Parser::parseParam() {
    auto loc = current_.location;
    bool isConst = false;

    if (check(TokenKind::Const)) {
        isConst = true;
        advance();
    }

    auto type = parseType();
    if (isConst && type && type->kind == TypeNodeKind::Reference) {
        static_cast<ReferenceTypeNode*>(type.get())->isConst = true;
    }
    bool isArray = false;
    ExprPtr arraySize;

    if (type->kind == TypeNodeKind::Array) {
        auto at = static_cast<ArrayTypeNode*>(type.get());
        isArray = true;
        arraySize = std::move(at->sizeExpr);
        if (at->isConst) isConst = true;
        type = std::move(at->elementType);
    }

    auto name = parseIdentifier("expected parameter name");

    // Check for array suffix []  or [N]
    std::vector<ExprPtr> gDims;
    while (check(TokenKind::LBracket)) {
        advance();
        ExprPtr sz = nullptr;
        if (!check(TokenKind::RBracket)) {
            sz = parseExpr();
        }
        expect(TokenKind::RBracket, "expected ']'");
        gDims.push_back(std::move(sz));
    }
    if (!gDims.empty()) {
        isArray = true;
        arraySize = std::move(gDims[0]);
        for (size_t k = gDims.size() - 1; k >= 1; --k) {
            type = std::make_unique<ArrayTypeNode>(std::move(type), std::move(gDims[k]), loc, isConst);
        }
    }

    // Check for default argument = expr
    ExprPtr defaultValue = nullptr;
    if (match(TokenKind::Eq)) {
        defaultValue = parseExpr();
    }

    return ParamDecl{
        .type = std::move(type),
        .name = std::move(name),
        .isConst = isConst,
        .isArray = isArray,
        .arraySize = std::move(arraySize),
        .loc = loc,
        .defaultValue = std::move(defaultValue),
    };
}

// ═══════════════════════════════════════════════════════════════════════════
//  Statements
// ═══════════════════════════════════════════════════════════════════════════

std::unique_ptr<BlockStmt> Parser::parseBlock() {
    if (++stmtDepth_ > MAX_STMT_DEPTH) {
        diag_.report(current_.location, Severity::Error,
                     "statement nesting depth exceeds maximum supported limit of " +
                     std::to_string(MAX_STMT_DEPTH));
        --stmtDepth_;
        hadError_ = true;
        return nullptr;
    }
    struct StmtDepthScope {
        int& d;
        ~StmtDepthScope() { --d; }
    } scope{stmtDepth_};

    auto loc = current_.location;
    expect(TokenKind::LBrace, "expected '{'");

    std::vector<StmtPtr> stmts;
    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
        try {
            auto s = parseStmt();
            if (s) {
                stmts.push_back(std::move(s));
            } else {
                while (!check(TokenKind::Eof) && !check(TokenKind::Semicolon) &&
                       !check(TokenKind::RBrace)) {
                    advance();
                }
                if (check(TokenKind::Semicolon)) advance();
            }
        } catch (const std::runtime_error&) {
            while (!check(TokenKind::Eof) && !check(TokenKind::Semicolon) &&
                   !check(TokenKind::RBrace)) {
                if (check(TokenKind::Auto) || check(TokenKind::Const) ||
                    check(TokenKind::If) || check(TokenKind::While) ||
                    check(TokenKind::For) || check(TokenKind::Return) ||
                    isTypeName()) {
                    break;
                }
                advance();
            }
            if (check(TokenKind::Semicolon)) advance();
        }
    }

    expect(TokenKind::RBrace, "expected '}'");
    return std::make_unique<BlockStmt>(std::move(stmts), loc);
}

StmtPtr Parser::parseStatementOrBlock() {
    if (check(TokenKind::LBrace)) {
        return parseBlock();
    }
    return parseStmt();
}

StmtPtr Parser::parseStmt() {
    if (check(TokenKind::At)) {
        LoopAttributes loopAttrs;
        bool hasLoopAttrs = false;

        while (check(TokenKind::At)) {
            advance(); // consume '@'
            auto attr = parseIdentifier("expected attribute name after '@'");
            if (attr == "unroll") {
                hasLoopAttrs = true;
                loopAttrs.unrollCount = 4; // default
                if (match(TokenKind::LParen)) {
                    if (check(TokenKind::IntLiteral)) {
                        loopAttrs.unrollCount = std::stoi(std::string(advance().text));
                    } else if (checkIdentifier()) {
                        auto param = advance().text;
                        if (param == "disable") {
                            loopAttrs.unrollCount = -1;
                        } else if (param == "count") {
                            expect(TokenKind::Colon, "expected ':' after 'count'");
                            if (check(TokenKind::IntLiteral)) {
                                loopAttrs.unrollCount = std::stoi(std::string(advance().text));
                            }
                        } else if (param == "enable") {
                            expect(TokenKind::Colon, "expected ':' after 'enable'");
                            if (check(TokenKind::True)) { advance(); loopAttrs.unrollCount = 4; }
                            else if (check(TokenKind::False)) { advance(); loopAttrs.unrollCount = -1; }
                        }
                    }
                    expect(TokenKind::RParen, "expected ')' after unroll options");
                }
            } else if (attr == "vectorize") {
                hasLoopAttrs = true;
                loopAttrs.vectorizeEnable = 1;
                loopAttrs.interleaveCount = 4; // default interleave
                if (match(TokenKind::LParen)) {
                    while (!check(TokenKind::RParen) && !check(TokenKind::Eof)) {
                        if (check(TokenKind::IntLiteral)) {
                            loopAttrs.vectorizeWidth = std::stoi(std::string(advance().text));
                            if (match(TokenKind::Comma) && check(TokenKind::IntLiteral)) {
                                loopAttrs.interleaveCount = std::stoi(std::string(advance().text));
                            }
                        } else if (checkIdentifier()) {
                            auto param = advance().text;
                            if (param == "enable") {
                                expect(TokenKind::Colon, "expected ':' after 'enable'");
                                if (check(TokenKind::True)) { advance(); loopAttrs.vectorizeEnable = 1; }
                                else if (check(TokenKind::False)) { advance(); loopAttrs.vectorizeEnable = -1; }
                            } else if (param == "width") {
                                expect(TokenKind::Colon, "expected ':' after 'width'");
                                if (check(TokenKind::IntLiteral)) {
                                    loopAttrs.vectorizeWidth = std::stoi(std::string(advance().text));
                                }
                            } else if (param == "interleave") {
                                expect(TokenKind::Colon, "expected ':' after 'interleave'");
                                if (check(TokenKind::IntLiteral)) {
                                    loopAttrs.interleaveCount = std::stoi(std::string(advance().text));
                                }
                            }
                            match(TokenKind::Comma);
                        } else {
                            advance();
                        }
                    }
                    expect(TokenKind::RParen, "expected ')' after vectorize options");
                }
            } else {
                diag_.report(current_.location, Severity::Error, "unknown statement attribute '@" + attr + "'");
                hadError_ = true;
                break;
            }
        }

        if (hasLoopAttrs) {
            if (check(TokenKind::For)) return parseForStmt(loopAttrs);
            if (check(TokenKind::While)) return parseWhileStmt(loopAttrs);
            if (check(TokenKind::Parallel)) return parseParallelForStmt(loopAttrs);
            diag_.report(current_.location, Severity::Error, "loop attribute must be followed by a for, while, or parallel loop");
            hadError_ = true;
        }
    }

    switch (current_.kind) {
        case TokenKind::Semicolon: {
            auto loc = current_.location;
            advance();
            return std::make_unique<BlockStmt>(std::vector<StmtPtr>{}, loc);
        }
        case TokenKind::LBrace:    return parseBlock();
        case TokenKind::If:        return parseIfStmt();
        case TokenKind::While:     return parseWhileStmt();
        case TokenKind::For:       return parseForStmt();
        case TokenKind::Parallel:  return parseParallelForStmt();
        case TokenKind::TaskScope: return parseTaskScopeStmt();
        case TokenKind::Spawn:     return parseSpawnStmt();
        case TokenKind::Return:    return parseReturnStmt();
        case TokenKind::Break:     return parseBreakStmt();
        case TokenKind::Continue:  return parseContinueStmt();
        case TokenKind::Switch:    return parseSwitchStmt();
        case TokenKind::Region:    return parseRegionStmt();
        case TokenKind::Match:     return parseMatchStmt();
        case TokenKind::Select:    return parseSelectStmt();
        case TokenKind::Const:
            if (lex_.peek().kind == TokenKind::Auto &&
                (lex_.peek(1).kind == TokenKind::LParen || lex_.peek(1).kind == TokenKind::LBracket)) {
                auto loc = current_.location;
                advance(); // consume const
                advance(); // consume auto
                return parseDestructureVarDeclStmt(true, loc);
            }
            return parseVarDeclOrExprStmt(true);
        case TokenKind::Auto:
            if (lex_.peek().kind == TokenKind::LParen || lex_.peek().kind == TokenKind::LBracket) {
                auto loc = current_.location;
                advance(); // consume auto
                return parseDestructureVarDeclStmt(false, loc);
            }
            return parseVarDeclOrExprStmt(false);
        default:
            if (isTypeName()) return parseVarDeclOrExprStmt(false);
            // Expression statement
            {
                auto loc = current_.location;
                auto expr = parseExpr();
                expect(TokenKind::Semicolon, "expected ';' after expression");
                return std::make_unique<ExprStmt>(std::move(expr), loc);
            }
    }
}

StmtPtr Parser::parseVarDeclOrExprStmt(bool isConst) {
    auto loc = current_.location;

    if (isConst) advance(); // consume 'const'

    // Try to parse as variable declaration
    // We need to distinguish "int x = ..." from "x + y;"
    // If we see a type keyword followed by identifier, it's a decl
    auto type = parseType();
    if (isConst && type && type->kind == TypeNodeKind::Reference) {
        static_cast<ReferenceTypeNode*>(type.get())->isConst = true;
    }
    bool isArray = false;
    ExprPtr arraySize;

    if (type->kind == TypeNodeKind::Array) {
        auto at = static_cast<ArrayTypeNode*>(type.get());
        isArray = true;
        arraySize = std::move(at->sizeExpr);
        if (at->isConst) isConst = true;
        type = std::move(at->elementType);
    }
    
    if (!checkIdentifier()) {
        // Not a declaration, but we already consumed the type keyword.
        // This shouldn't normally happen for valid code.
        diag_.report(loc, Severity::Error, "expected variable name");
        hadError_ = true;
        return std::make_unique<ExprStmt>(nullptr, loc);
    }

    auto name = parseIdentifier("expected variable name");

    // Check for array suffix [N] or []
    std::vector<ExprPtr> gDims;
    while (check(TokenKind::LBracket)) {
        advance();
        ExprPtr sz = nullptr;
        if (!check(TokenKind::RBracket)) {
            sz = parseExpr();
        }
        expect(TokenKind::RBracket, "expected ']'");
        gDims.push_back(std::move(sz));
    }
    if (!gDims.empty()) {
        isArray = true;
        arraySize = std::move(gDims[0]);
        for (size_t k = gDims.size() - 1; k >= 1; --k) {
            type = std::make_unique<ArrayTypeNode>(std::move(type), std::move(gDims[k]), loc, isConst);
        }
    }

    // Initializer
    ExprPtr init;
    if (match(TokenKind::Eq)) {
        if (check(TokenKind::LBrace)) {
            TypePtr initType;
            if (!isArray && type->kind == TypeNodeKind::Named) {
                initType = std::make_unique<NamedTypeNode>(
                    static_cast<const NamedTypeNode*>(type.get())->name, type->loc);
            }
            init = parseBracedInitializer(std::move(initType));
        } else {
            init = parseExpr();
        }
    } else if (check(TokenKind::LParen)) {
        // Direct constructor call: Player player(100);
        advance();
        std::vector<ExprPtr> args;
        if (!check(TokenKind::RParen)) {
            do {
                args.push_back(parseExpr());
            } while (match(TokenKind::Comma));
        }
        expect(TokenKind::RParen, "expected ')' after constructor arguments");
        std::string typeName;
        if (type->kind == TypeNodeKind::Named) {
            typeName = static_cast<const NamedTypeNode*>(type.get())->name;
        } else if (type->kind == TypeNodeKind::Slab) {
            typeName = "Slab";
        } else if (type->kind == TypeNodeKind::Primitive) {
            auto pk = static_cast<const PrimitiveTypeNode*>(type.get())->primKind;
            if (pk == PrimitiveKind::Float4) typeName = "float4";
            else if (pk == PrimitiveKind::Float8) typeName = "float8";
            else if (pk == PrimitiveKind::Int4) typeName = "int4";
            else if (pk == PrimitiveKind::Int8) typeName = "int8";
            else if (pk == PrimitiveKind::Int16) typeName = "int16";
        }
        init = std::make_unique<ConstructExpr>(typeName, std::move(args), loc);
    }

    expect(TokenKind::Semicolon, "expected ';' after variable declaration");

    return std::make_unique<VarDeclStmt>(
        std::move(type), std::move(name), std::move(init),
        isConst, isArray, std::move(arraySize), loc);
}

StmtPtr Parser::parseDestructureVarDeclStmt(bool isConst, SourceLocation loc) {
    bool isBracket = check(TokenKind::LBracket);
    if (isBracket) advance();
    else expect(TokenKind::LParen, "expected '(' or '[' in destructuring declaration");

    std::vector<std::string> names;
    do {
        names.push_back(parseIdentifier("expected identifier in destructuring declaration"));
    } while (match(TokenKind::Comma));

    if (isBracket) expect(TokenKind::RBracket, "expected ']' after destructuring variable list");
    else expect(TokenKind::RParen, "expected ')' after destructuring variable list");

    expect(TokenKind::Eq, "expected '=' in destructuring declaration");
    auto init = parseExpr();
    expect(TokenKind::Semicolon, "expected ';' after destructuring declaration");

    return std::make_unique<DestructureVarDeclStmt>(std::move(names), std::move(init), isConst, loc);
}

StmtPtr Parser::parseIfStmt() {
    auto loc = current_.location;
    expect(TokenKind::If, "expected 'if'");
    expect(TokenKind::LParen, "expected '('");
    auto cond = parseExpr();
    expect(TokenKind::RParen, "expected ')'");
    auto then = parseStatementOrBlock();

    StmtPtr elseBlock;
    if (match(TokenKind::Else)) {
        if (check(TokenKind::If)) {
            elseBlock = parseIfStmt();
        } else {
            elseBlock = parseStatementOrBlock();
        }
    }

    return std::make_unique<IfStmt>(std::move(cond), std::move(then),
                                     std::move(elseBlock), loc);
}

StmtPtr Parser::parseWhileStmt(LoopAttributes attrs) {
    auto loc = current_.location;
    expect(TokenKind::While, "expected 'while'");
    expect(TokenKind::LParen, "expected '('");
    auto cond = parseExpr();
    expect(TokenKind::RParen, "expected ')'");
    auto body = parseStatementOrBlock();
    auto stmt = std::make_unique<WhileStmt>(std::move(cond), std::move(body), loc);
    stmt->unrollCount = attrs.unrollCount;
    stmt->vectorizeWidth = attrs.vectorizeWidth;
    stmt->interleaveCount = attrs.interleaveCount;
    stmt->vectorizeEnable = attrs.vectorizeEnable;
    return stmt;
}

StmtPtr Parser::parseForStmt(LoopAttributes attrs) {
    auto loc = current_.location;
    expect(TokenKind::For, "expected 'for'");
    expect(TokenKind::LParen, "expected '('");

    // Init
    StmtPtr init;
    if (!check(TokenKind::Semicolon)) {
        if (isTypeName() || check(TokenKind::Const)) {
            init = parseVarDeclOrExprStmt(check(TokenKind::Const));
            // parseVarDeclOrExprStmt already consumed the semicolon
        } else {
            auto initLoc = current_.location;
            auto expr = parseExpr();
            expect(TokenKind::Semicolon, "expected ';'");
            init = std::make_unique<ExprStmt>(std::move(expr), initLoc);
        }
    } else {
        advance(); // consume ;
    }

    // Condition
    ExprPtr cond;
    if (!check(TokenKind::Semicolon)) {
        cond = parseExpr();
    }
    expect(TokenKind::Semicolon, "expected ';'");

    // Increment
    ExprPtr inc;
    if (!check(TokenKind::RParen)) {
        inc = parseExpr();
    }
    expect(TokenKind::RParen, "expected ')'");

    auto body = parseStatementOrBlock();
    auto stmt = std::make_unique<ForStmt>(std::move(init), std::move(cond),
                                          std::move(inc), std::move(body), loc);
    stmt->unrollCount = attrs.unrollCount;
    stmt->vectorizeWidth = attrs.vectorizeWidth;
    stmt->interleaveCount = attrs.interleaveCount;
    stmt->vectorizeEnable = attrs.vectorizeEnable;
    return stmt;
}

StmtPtr Parser::parseParallelForStmt(LoopAttributes attrs) {
    auto loc = current_.location;
    expect(TokenKind::Parallel, "expected 'parallel'");

    ExprPtr grainSize;
    std::vector<ReductionClause> reductions;

    if (match(TokenKind::LParen)) {
        while (!check(TokenKind::RParen) && !check(TokenKind::Eof)) {
            if (check(TokenKind::Reduction) || check(TokenKind::Reduce)) {
                advance(); // consume 'reduction' or 'reduce'
                expect(TokenKind::LParen, "expected '(' after 'reduction'");
                ReductionOp op = ReductionOp::Add;
                auto opLoc = current_.location;
                if (match(TokenKind::Plus)) {
                    op = ReductionOp::Add;
                } else if (match(TokenKind::Star)) {
                    op = ReductionOp::Mul;
                } else if (match(TokenKind::Amp)) {
                    op = ReductionOp::BitAnd;
                } else if (match(TokenKind::Pipe)) {
                    op = ReductionOp::BitOr;
                } else if (match(TokenKind::Caret)) {
                    op = ReductionOp::BitXor;
                } else if (match(TokenKind::Min) || (check(TokenKind::Identifier) && current_.text == "min")) {
                    if (check(TokenKind::Identifier)) advance();
                    op = ReductionOp::Min;
                } else if (match(TokenKind::Max) || (check(TokenKind::Identifier) && current_.text == "max")) {
                    if (check(TokenKind::Identifier)) advance();
                    op = ReductionOp::Max;
                } else {
                    error("expected reduction operator (+, *, min, max, &, |, ^)");
                }
                expect(TokenKind::Colon, "expected ':' after reduction operator");
                auto varTok = expect(TokenKind::Identifier, "expected reduction variable name");
                reductions.push_back(ReductionClause{op, std::string(varTok.text), opLoc});
                expect(TokenKind::RParen, "expected ')' after reduction clause");
            } else {
                grainSize = parseExpr();
            }

            if (!check(TokenKind::RParen)) {
                expect(TokenKind::Comma, "expected ',' or ')' in parallel clause");
            }
        }
        expect(TokenKind::RParen, "expected ')' after parallel clauses");
    }

    expect(TokenKind::For, "expected 'for' after 'parallel'");
    expect(TokenKind::LParen, "expected '(' after 'for'");

    // Init
    StmtPtr init;
    if (!check(TokenKind::Semicolon)) {
        if (isTypeName() || check(TokenKind::Const)) {
            init = parseVarDeclOrExprStmt(check(TokenKind::Const));
        } else {
            auto initLoc = current_.location;
            auto expr = parseExpr();
            expect(TokenKind::Semicolon, "expected ';'");
            init = std::make_unique<ExprStmt>(std::move(expr), initLoc);
        }
    } else {
        advance(); // consume ;
    }

    // Condition
    ExprPtr cond;
    if (!check(TokenKind::Semicolon)) {
        cond = parseExpr();
    }
    expect(TokenKind::Semicolon, "expected ';'");

    // Increment
    ExprPtr inc;
    if (!check(TokenKind::RParen)) {
        inc = parseExpr();
    }
    expect(TokenKind::RParen, "expected ')'");

    auto body = parseStatementOrBlock();
    auto stmt = std::make_unique<ParallelForStmt>(std::move(init), std::move(cond),
                                            std::move(inc), std::move(body),
                                            std::move(grainSize), std::move(reductions), loc);
    stmt->unrollCount = attrs.unrollCount;
    stmt->vectorizeWidth = attrs.vectorizeWidth;
    stmt->interleaveCount = attrs.interleaveCount;
    stmt->vectorizeEnable = attrs.vectorizeEnable;
    return stmt;
}

StmtPtr Parser::parseTaskScopeStmt() {
    auto loc = current_.location;
    expect(TokenKind::TaskScope, "expected 'task_scope'");
    auto body = parseBlock();
    return std::make_unique<TaskScopeStmt>(
        std::unique_ptr<BlockStmt>(static_cast<BlockStmt*>(body.release())), loc);
}

StmtPtr Parser::parseSpawnStmt() {
    auto loc = current_.location;
    expect(TokenKind::Spawn, "expected 'spawn'");
    StmtPtr body;
    if (check(TokenKind::LBrace)) {
        body = parseBlock();
    } else {
        auto exprLoc = current_.location;
        auto expr = parseExpr();
        expect(TokenKind::Semicolon, "expected ';' after spawn expression");
        body = std::make_unique<ExprStmt>(std::move(expr), exprLoc);
    }
    return std::make_unique<SpawnStmt>(std::move(body), loc);
}

StmtPtr Parser::parseReturnStmt() {
    auto loc = current_.location;
    advance(); // consume 'return'
    ExprPtr value;
    if (!check(TokenKind::Semicolon)) {
        value = parseExpr();
    }
    expect(TokenKind::Semicolon, "expected ';' after return");
    return std::make_unique<ReturnStmt>(std::move(value), loc);
}

StmtPtr Parser::parseBreakStmt() {
    auto loc = current_.location;
    advance();
    expect(TokenKind::Semicolon, "expected ';'");
    return std::make_unique<BreakStmt>(loc);
}

StmtPtr Parser::parseContinueStmt() {
    auto loc = current_.location;
    advance();
    expect(TokenKind::Semicolon, "expected ';'");
    return std::make_unique<ContinueStmt>(loc);
}

StmtPtr Parser::parseSwitchStmt() {
    auto loc = current_.location;
    advance(); // consume 'switch'
    expect(TokenKind::LParen, "expected '('");
    auto cond = parseExpr();
    expect(TokenKind::RParen, "expected ')'");
    expect(TokenKind::LBrace, "expected '{'");

    std::vector<CaseClause> cases;
    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
        CaseClause clause;
        if (match(TokenKind::Case)) {
            clause.value = parseExpr();
            expect(TokenKind::Colon, "expected ':'");
        } else if (match(TokenKind::Default)) {
            expect(TokenKind::Colon, "expected ':'");
        }
        while (!check(TokenKind::Case) && !check(TokenKind::Default) &&
               !check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
            clause.body.push_back(parseStmt());
        }
        cases.push_back(std::move(clause));
    }

    expect(TokenKind::RBrace, "expected '}'");
    return std::make_unique<SwitchStmt>(std::move(cond), std::move(cases), loc);
}

StmtPtr Parser::parseRegionStmt() {
    auto loc = current_.location;
    advance(); // consume 'region'

    std::string name;
    if (checkIdentifier()) {
        name = current_.text;
        advance();
    }

    ExprPtr capacityExpr = nullptr;
    if (match(TokenKind::LParen)) {
        capacityExpr = parseExpr();
        expect(TokenKind::RParen, "expected ')' after region capacity");
    }

    auto body = parseBlock();
    return std::make_unique<RegionStmt>(std::move(name), std::move(capacityExpr), std::move(body), loc);
}

StmtPtr Parser::parseSelectStmt() {
    auto loc = current_.location;
    advance(); // consume 'select'
    expect(TokenKind::LBrace, "expected '{' after 'select'");

    std::vector<SelectCase> cases;
    std::vector<StmtPtr> defaultBody;
    bool hasDefault = false;

    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
        if (match(TokenKind::Case)) {
            SelectCase sc;
            sc.loc = current_.location;
            sc.condition = parseExpr();
            expect(TokenKind::Colon, "expected ':' after case condition");
            while (!check(TokenKind::Case) && !check(TokenKind::Default) &&
                   !check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
                sc.body.push_back(parseStmt());
            }
            cases.push_back(std::move(sc));
        } else if (match(TokenKind::Default)) {
            hasDefault = true;
            expect(TokenKind::Colon, "expected ':' after 'default'");
            while (!check(TokenKind::Case) && !check(TokenKind::Default) &&
                   !check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
                defaultBody.push_back(parseStmt());
            }
        } else {
            error("expected 'case' or 'default' inside 'select' block");
            advance();
        }
    }
    expect(TokenKind::RBrace, "expected '}' after select cases");
    return std::make_unique<SelectStmt>(std::move(cases), std::move(defaultBody), hasDefault, loc);
}

StmtPtr Parser::parseMatchStmt() {
    auto loc = current_.location;
    advance(); // consume 'match'
    bool hasParen = match(TokenKind::LParen);
    auto target = parseExpr();
    if (hasParen) {
        expect(TokenKind::RParen, "expected ')' after match target");
    }

    expect(TokenKind::LBrace, "expected '{' after match target");
    std::vector<MatchCase> cases;
    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
        cases.push_back(parseMatchCase());
    }
    expect(TokenKind::RBrace, "expected '}' after match cases");

    return std::make_unique<MatchStmt>(std::move(target), std::move(cases), loc);
}

ExprPtr Parser::parseMatchExpr() {
    auto loc = current_.location;
    advance(); // consume 'match'
    bool hasParen = match(TokenKind::LParen);
    auto target = parseExpr();
    if (hasParen) {
        expect(TokenKind::RParen, "expected ')' after match target");
    }

    expect(TokenKind::LBrace, "expected '{' after match target");
    std::vector<MatchCase> cases;
    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
        cases.push_back(parseMatchCase());
    }
    expect(TokenKind::RBrace, "expected '}' after match cases");

    return std::make_unique<MatchExpr>(std::move(target), std::move(cases), loc);
}

MatchPattern Parser::parseMatchPattern() {
    MatchPattern pat;
    pat.loc = current_.location;

    // 1. Wildcard '_'
    if (check(TokenKind::Identifier) && current_.text == "_") {
        advance();
        pat.kind = MatchPatternKind::Wildcard;
        return pat;
    }

    // 2. Literals
    if (check(TokenKind::IntLiteral) || check(TokenKind::FloatLiteral) ||
        check(TokenKind::StringLiteral) || check(TokenKind::CharLiteral) ||
        check(TokenKind::True) || check(TokenKind::False)) {
        pat.kind = MatchPatternKind::Literal;
        pat.literalExpr = parsePrimary();
        return pat;
    }

    // 3. Identifier: could be Enum::Variant, Variant(bindings), or binding
    if (check(TokenKind::Identifier)) {
        std::string firstId = std::string(current_.text);
        advance();

        if (match(TokenKind::ColonColon)) {
            // Must be Enum::Variant
            pat.kind = MatchPatternKind::Variant;
            pat.enumName = std::move(firstId);
            pat.variantName = parseIdentifier("expected variant name after '::'");
            if (match(TokenKind::LParen)) {
                while (!check(TokenKind::RParen) && !check(TokenKind::Eof)) {
                    pat.bindings.push_back(parseIdentifier("expected binding variable name"));
                    if (!match(TokenKind::Comma)) break;
                }
                expect(TokenKind::RParen, "expected ')' after pattern bindings");
            }
            return pat;
        }

        if (match(TokenKind::LParen)) {
            // Variant with payload: Variant(bindings)
            pat.kind = MatchPatternKind::Variant;
            pat.variantName = std::move(firstId);
            while (!check(TokenKind::RParen) && !check(TokenKind::Eof)) {
                pat.bindings.push_back(parseIdentifier("expected binding variable name"));
                if (!match(TokenKind::Comma)) break;
            }
            expect(TokenKind::RParen, "expected ')' after pattern bindings");
            return pat;
        }

        // Check if firstId starts with uppercase: variant name
        if (!firstId.empty() && std::isupper(static_cast<unsigned char>(firstId[0]))) {
            pat.kind = MatchPatternKind::Variant;
            pat.variantName = std::move(firstId);
            return pat;
        }

        // Otherwise, variable binding pattern 'x'
        pat.kind = MatchPatternKind::Binding;
        pat.bindings.push_back(std::move(firstId));
        return pat;
    }

    diag_.report(current_.location, Severity::Error, "expected pattern in match case");
    hadError_ = true;
    pat.kind = MatchPatternKind::Wildcard;
    return pat;
}

MatchCase Parser::parseMatchCase() {
    MatchCase c;
    c.loc = current_.location;
    c.pattern = parseMatchPattern();

    // Optional guard: if (cond)
    if (match(TokenKind::If)) {
        expect(TokenKind::LParen, "expected '(' after 'if' in match guard");
        c.guard = parseExpr();
        expect(TokenKind::RParen, "expected ')' after match guard condition");
    }

    expect(TokenKind::FatArrow, "expected '=>' after pattern");

    if (check(TokenKind::LBrace)) {
        c.body = parseBlock();
        match(TokenKind::Comma); // optional trailing comma
    } else {
        auto exprLoc = current_.location;
        auto expr = parseExpr();
        if (check(TokenKind::Semicolon)) {
            advance();
        } else {
            match(TokenKind::Comma);
        }
        c.body = std::make_unique<ExprStmt>(std::move(expr), exprLoc);
    }
    return c;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Expressions — precedence climbing
// ═══════════════════════════════════════════════════════════════════════════

ExprPtr Parser::parseExpr() {
    if (++exprDepth_ > MAX_EXPR_DEPTH) {
        diag_.report(current_.location, Severity::Error,
                     "expression nesting depth exceeds maximum supported limit of " +
                     std::to_string(MAX_EXPR_DEPTH));
        --exprDepth_;
        hadError_ = true;
        return nullptr;
    }
    struct ExprDepthScope {
        int& d;
        ~ExprDepthScope() { --d; }
    } scope{exprDepth_};

    return parseAssignment();
}

ExprPtr Parser::parseAssignment() {
    auto expr = parseConditional();
    if (!expr) return nullptr;

    auto mapAssignOp = [](TokenKind k) -> std::optional<AssignOp> {
        switch (k) {
            case TokenKind::Eq:        return AssignOp::Assign;
            case TokenKind::PlusEq:    return AssignOp::AddAssign;
            case TokenKind::MinusEq:   return AssignOp::SubAssign;
            case TokenKind::StarEq:    return AssignOp::MulAssign;
            case TokenKind::SlashEq:   return AssignOp::DivAssign;
            case TokenKind::PercentEq: return AssignOp::ModAssign;
            case TokenKind::AmpEq:     return AssignOp::AndAssign;
            case TokenKind::PipeEq:    return AssignOp::OrAssign;
            case TokenKind::CaretEq:   return AssignOp::XorAssign;
            case TokenKind::LtLtEq:    return AssignOp::ShlAssign;
            case TokenKind::GtGtEq:    return AssignOp::ShrAssign;
            default: return std::nullopt;
        }
    };

    auto op = mapAssignOp(current_.kind);
    if (op) {
        auto loc = current_.location;
        advance();
        auto rhs = parseAssignment(); // right-associative
        return std::make_unique<AssignExpr>(*op, std::move(expr),
                                             std::move(rhs), loc);
    }

    return expr;
}

ExprPtr Parser::parseConditional() {
    auto expr = parseLogicOr();
    if (!expr) return nullptr;
    if (check(TokenKind::Question)) {
        auto loc = current_.location;
        advance(); // consume '?'
        auto thenExpr = parseExpr();
        expect(TokenKind::Colon, "expected ':' in ternary conditional operator");
        auto elseExpr = parseConditional(); // right-associative
        return std::make_unique<TernaryExpr>(std::move(expr), std::move(thenExpr), std::move(elseExpr), loc);
    }
    return expr;
}

ExprPtr Parser::parseLogicOr() {
    auto expr = parseLogicAnd();
    if (!expr) return nullptr;
    while (check(TokenKind::PipePipe)) {
        auto loc = current_.location;
        advance();
        auto rhs = parseLogicAnd();
        expr = std::make_unique<BinaryExpr>(BinaryOp::Or, std::move(expr),
                                             std::move(rhs), loc);
    }
    return expr;
}

ExprPtr Parser::parseLogicAnd() {
    auto expr = parseComparison();
    if (!expr) return nullptr;
    while (check(TokenKind::AmpAmp)) {
        auto loc = current_.location;
        advance();
        auto rhs = parseComparison();
        expr = std::make_unique<BinaryExpr>(BinaryOp::And, std::move(expr),
                                             std::move(rhs), loc);
    }
    return expr;
}

ExprPtr Parser::parseComparison() {
    auto expr = parseRelational();
    if (!expr) return nullptr;

    while (check(TokenKind::EqEq) || check(TokenKind::BangEq)) {
        auto loc = current_.location;
        BinaryOp op = check(TokenKind::EqEq) ? BinaryOp::EqEq : BinaryOp::NotEq;
        advance();
        auto rhs = parseRelational();
        expr = std::make_unique<BinaryExpr>(op, std::move(expr),
                                             std::move(rhs), loc);
    }
    return expr;
}

ExprPtr Parser::parseRelational() {
    auto expr = parseBitOr();
    if (!expr) return nullptr;

    while (check(TokenKind::Lt) || check(TokenKind::Gt) ||
           check(TokenKind::LtEq) || check(TokenKind::GtEq)) {
        auto loc = current_.location;
        BinaryOp op = BinaryOp::Lt;
        if (check(TokenKind::Lt)) op = BinaryOp::Lt;
        else if (check(TokenKind::Gt)) op = BinaryOp::Gt;
        else if (check(TokenKind::LtEq)) op = BinaryOp::LtEq;
        else if (check(TokenKind::GtEq)) op = BinaryOp::GtEq;
        advance();
        auto rhs = parseBitOr();
        expr = std::make_unique<BinaryExpr>(op, std::move(expr),
                                             std::move(rhs), loc);
    }
    return expr;
}

ExprPtr Parser::parseBitOr() {
    auto expr = parseBitXor();
    if (!expr) return nullptr;
    while (check(TokenKind::Pipe)) {
        auto loc = current_.location;
        advance();
        auto rhs = parseBitXor();
        expr = std::make_unique<BinaryExpr>(BinaryOp::BitOr, std::move(expr),
                                             std::move(rhs), loc);
    }
    return expr;
}

ExprPtr Parser::parseBitXor() {
    auto expr = parseBitAnd();
    if (!expr) return nullptr;
    while (check(TokenKind::Caret)) {
        auto loc = current_.location;
        advance();
        auto rhs = parseBitAnd();
        expr = std::make_unique<BinaryExpr>(BinaryOp::BitXor, std::move(expr),
                                             std::move(rhs), loc);
    }
    return expr;
}

ExprPtr Parser::parseBitAnd() {
    auto expr = parseShift();
    if (!expr) return nullptr;
    while (check(TokenKind::Amp)) {
        auto loc = current_.location;
        advance();
        auto rhs = parseShift();
        expr = std::make_unique<BinaryExpr>(BinaryOp::BitAnd, std::move(expr),
                                             std::move(rhs), loc);
    }
    return expr;
}

ExprPtr Parser::parseShift() {
    auto expr = parseAddSub();
    if (!expr) return nullptr;
    while (check(TokenKind::LtLt) || check(TokenKind::GtGt)) {
        auto loc = current_.location;
        auto op = check(TokenKind::LtLt) ? BinaryOp::Shl : BinaryOp::Shr;
        advance();
        auto rhs = parseAddSub();
        expr = std::make_unique<BinaryExpr>(op, std::move(expr),
                                             std::move(rhs), loc);
    }
    return expr;
}

ExprPtr Parser::parseAddSub() {
    auto expr = parseMulDiv();
    if (!expr) return nullptr;
    while (check(TokenKind::Plus) || check(TokenKind::Minus)) {
        auto loc = current_.location;
        auto op = check(TokenKind::Plus) ? BinaryOp::Add : BinaryOp::Sub;
        advance();
        auto rhs = parseMulDiv();
        expr = std::make_unique<BinaryExpr>(op, std::move(expr),
                                             std::move(rhs), loc);
    }
    return expr;
}

ExprPtr Parser::parseMulDiv() {
    auto expr = parseUnary();
    if (!expr) return nullptr;
    while (check(TokenKind::Star) || check(TokenKind::Slash) || check(TokenKind::Percent)) {
        auto loc = current_.location;
        BinaryOp op;
        if (check(TokenKind::Star)) op = BinaryOp::Mul;
        else if (check(TokenKind::Slash)) op = BinaryOp::Div;
        else op = BinaryOp::Mod;
        advance();
        auto rhs = parseUnary();
        expr = std::make_unique<BinaryExpr>(op, std::move(expr),
                                             std::move(rhs), loc);
    }
    return expr;
}

ExprPtr Parser::parseUnary() {
    struct UnaryPrefix {
        UnaryOp op;
        bool isAwait = false;
        SourceLocation loc;
    };
    std::vector<UnaryPrefix> ops;

    while (true) {
        if (ops.size() + exprDepth_ > MAX_EXPR_DEPTH) {
            diag_.report(current_.location, Severity::Error,
                         "expression nesting depth exceeds maximum supported limit of " +
                         std::to_string(MAX_EXPR_DEPTH));
            hadError_ = true;
            return nullptr;
        }
        auto loc = current_.location;
        if (check(TokenKind::Await)) {
            advance();
            ops.push_back({UnaryOp::Neg, true, loc});
        } else if (check(TokenKind::PlusPlus)) {
            advance();
            ops.push_back({UnaryOp::PreInc, false, loc});
        } else if (check(TokenKind::MinusMinus)) {
            advance();
            ops.push_back({UnaryOp::PreDec, false, loc});
        } else if (check(TokenKind::Minus)) {
            advance();
            ops.push_back({UnaryOp::Neg, false, loc});
        } else if (check(TokenKind::Bang)) {
            advance();
            ops.push_back({UnaryOp::Not, false, loc});
        } else if (check(TokenKind::Tilde)) {
            advance();
            ops.push_back({UnaryOp::BitNot, false, loc});
        } else if (check(TokenKind::Amp)) {
            advance();
            ops.push_back({UnaryOp::AddrOf, false, loc});
        } else if (check(TokenKind::Star)) {
            advance();
            ops.push_back({UnaryOp::Deref, false, loc});
        } else {
            break;
        }
    }

    // C-style cast: (Type)expr
    if (check(TokenKind::LParen) && isTypeName()) {
        // Ambiguous: could be (expr) or (Type)expr
        // For now, skip C-style casts — treat as grouped expression
    }

    auto expr = parsePostfix();
    if (!expr) return nullptr;

    for (auto it = ops.rbegin(); it != ops.rend(); ++it) {
        if (it->isAwait) {
            expr = std::make_unique<AwaitExpr>(std::move(expr), it->loc);
        } else {
            expr = std::make_unique<UnaryExpr>(it->op, std::move(expr), it->loc);
        }
    }
    return expr;
}

ExprPtr Parser::parsePostfix() {
    auto expr = parsePrimary();
    if (!expr) return nullptr;

    while (true) {
        auto loc = current_.location;
        // Generic function or method call: foo<T, U>(args) or obj.method<T, U>(args)
        if (check(TokenKind::Lt) && (expr->kind == ExprKind::Identifier || expr->kind == ExprKind::Field) && isGenericCallLookahead()) {
            advance(); // consume '<'
            std::vector<TypePtr> typeArgs;
            while (!check(TokenKind::Gt) && !check(TokenKind::GtGt) && !check(TokenKind::Eof)) {
                typeArgs.push_back(parseType());
                if (!match(TokenKind::Comma)) break;
            }
            expectClosingAngle("expected '>' after type arguments");
            if (check(TokenKind::LParen)) {
                advance(); // consume '('
                std::vector<ExprPtr> args;
                if (!check(TokenKind::RParen)) {
                    do {
                        args.push_back(parseExpr());
                    } while (match(TokenKind::Comma));
                }
                expect(TokenKind::RParen, "expected ')'");

                if (expr->kind == ExprKind::Field) {
                    auto fe = static_cast<FieldExpr*>(expr.get());
                    expr = std::make_unique<MethodCallExpr>(
                        std::move(fe->object), fe->fieldName, std::move(typeArgs), std::move(args), fe->isArrow, loc);
                } else {
                    auto callee = static_cast<IdentifierExpr*>(expr.get())->name;
                    expr = std::make_unique<CallExpr>(std::move(callee), std::move(typeArgs), std::move(args), loc);
                }
                continue;
            }
        }
        if (check(TokenKind::LParen)) {
            // Function call — but only if expr is an identifier
            advance();
            std::vector<ExprPtr> args;
            if (!check(TokenKind::RParen)) {
                do {
                    args.push_back(parseExpr());
                } while (match(TokenKind::Comma));
            }
            expect(TokenKind::RParen, "expected ')'");

            // Extract callee name from identifier expr
            if (expr->kind == ExprKind::Field) {
                auto fe = static_cast<FieldExpr*>(expr.get());
                expr = std::make_unique<MethodCallExpr>(
                    std::move(fe->object), fe->fieldName, std::move(args), fe->isArrow, loc);
            } else if (expr->kind == ExprKind::Identifier) {
                auto callee = static_cast<IdentifierExpr*>(expr.get())->name;
                expr = std::make_unique<CallExpr>(std::move(callee),
                                                   std::move(args), loc);
            } else {
                expr = std::make_unique<CallExpr>(std::move(expr),
                                                   std::move(args), loc);
            }
        }
        else if (check(TokenKind::LBracket)) {
            advance();
            ExprPtr index = nullptr;
            if (match(TokenKind::DotDot)) {
                // [..] or [..end]
                ExprPtr end = nullptr;
                if (!check(TokenKind::RBracket)) {
                    end = parseExpr();
                }
                index = std::make_unique<RangeExpr>(nullptr, std::move(end), loc);
            } else {
                auto first = parseExpr();
                if (match(TokenKind::DotDot)) {
                    // [start..] or [start..end]
                    ExprPtr end = nullptr;
                    if (!check(TokenKind::RBracket)) {
                        end = parseExpr();
                    }
                    index = std::make_unique<RangeExpr>(std::move(first), std::move(end), loc);
                } else {
                    index = std::move(first);
                }
            }
            expect(TokenKind::RBracket, "expected ']'");
            expr = std::make_unique<IndexExpr>(std::move(expr),
                                                std::move(index), loc);
        }
        else if (check(TokenKind::Dot)) {
            advance();
            if (check(TokenKind::IntLiteral)) {
                size_t idx = static_cast<size_t>(current_.intValue());
                advance();
                expr = std::make_unique<TupleIndexExpr>(std::move(expr), idx, loc);
            } else {
                auto field = std::string(current_.text);
                expect(TokenKind::Identifier, "expected field name");
                expr = std::make_unique<FieldExpr>(std::move(expr),
                                                    std::move(field), false, loc);
            }
        }
        else if (check(TokenKind::Arrow)) {
            advance();
            auto field = std::string(current_.text);
            expect(TokenKind::Identifier, "expected field name");
            expr = std::make_unique<FieldExpr>(std::move(expr),
                                                std::move(field), true, loc);
        }
        else if (check(TokenKind::PlusPlus)) {
            advance();
            expr = std::make_unique<UnaryExpr>(UnaryOp::PostInc,
                                                std::move(expr), loc);
        }
        else if (check(TokenKind::MinusMinus)) {
            advance();
            expr = std::make_unique<UnaryExpr>(UnaryOp::PostDec,
                                                std::move(expr), loc);
        }
        else if (check(TokenKind::Question)) {
            auto nextK = lex_.peek().kind;
            bool isExprStarter = (nextK == TokenKind::Identifier ||
                                  nextK == TokenKind::IntLiteral ||
                                  nextK == TokenKind::FloatLiteral ||
                                  nextK == TokenKind::StringLiteral ||
                                  nextK == TokenKind::CharLiteral ||
                                  nextK == TokenKind::True ||
                                  nextK == TokenKind::False ||
                                  nextK == TokenKind::LParen ||
                                  nextK == TokenKind::LBracket ||
                                  nextK == TokenKind::LBrace ||
                                  nextK == TokenKind::Minus ||
                                  nextK == TokenKind::Plus ||
                                  nextK == TokenKind::Bang ||
                                  nextK == TokenKind::Tilde ||
                                  nextK == TokenKind::Star ||
                                  nextK == TokenKind::Amp ||
                                  nextK == TokenKind::PlusPlus ||
                                  nextK == TokenKind::MinusMinus ||
                                  nextK == TokenKind::Await ||
                                  nextK == TokenKind::Match ||
                                  nextK == TokenKind::This ||
                                  nextK == TokenKind::Null);
            if (isExprStarter) {
                break;
            }
            advance();
            expr = std::make_unique<TryExpr>(std::move(expr), loc);
        }
        else {
            break;
        }
    }

    return expr;
}

bool Parser::isLambdaAhead() {
    if (check(TokenKind::Fn) && lex_.peek(0).kind == TokenKind::LParen) {
        return true;
    }
    if (!check(TokenKind::LParen)) return false;
    int depth = 1;
    size_t idx = 0;
    while (depth > 0 && idx < 64) {
        auto tk = lex_.peek(idx).kind;
        if (tk == TokenKind::Eof || tk == TokenKind::Semicolon || tk == TokenKind::LBrace || tk == TokenKind::RBrace) return false;
        if (tk == TokenKind::LParen) depth++;
        else if (tk == TokenKind::RParen) {
            depth--;
            if (depth == 0) {
                auto after = lex_.peek(idx + 1).kind;
                if (after == TokenKind::FatArrow) return true;
                if (after == TokenKind::Arrow) {
                    size_t j = idx + 2;
                    while (j < 64) {
                        auto tk2 = lex_.peek(j).kind;
                        if (tk2 == TokenKind::FatArrow || tk2 == TokenKind::LBrace) return true;
                        if (tk2 == TokenKind::Eof || tk2 == TokenKind::Semicolon) return false;
                        j++;
                    }
                }
                return false;
            }
        }
        idx++;
    }
    return false;
}

ExprPtr Parser::parseLambda() {
    auto loc = current_.location;
    if (match(TokenKind::Fn)) {
        // consumed 'fn'
    }
    expect(TokenKind::LParen, "expected '(' in lambda parameter list");
    std::vector<ParamDecl> params;
    if (!check(TokenKind::RParen)) {
        do {
            auto pLoc = current_.location;
            TypePtr pType = nullptr;
            std::string pName;
            bool isConst = match(TokenKind::Const);

            if (isTypeName()) {
                pType = parseType();
                pName = parseIdentifier("expected parameter name");
            } else {
                pName = parseIdentifier("expected parameter name");
                pType = std::make_unique<AutoTypeNode>(pLoc);
            }

            ExprPtr defVal = nullptr;
            if (match(TokenKind::Eq)) {
                defVal = parseExpr();
            }

            ParamDecl param;
            param.type = std::move(pType);
            param.name = std::move(pName);
            param.isConst = isConst;
            param.isArray = false;
            param.loc = pLoc;
            param.defaultValue = std::move(defVal);
            params.push_back(std::move(param));
        } while (match(TokenKind::Comma));
    }
    expect(TokenKind::RParen, "expected ')' after lambda parameter list");

    TypePtr retType = nullptr;
    if (match(TokenKind::Arrow)) {
        retType = parseType();
    }

    expect(TokenKind::FatArrow, "expected '=>' in lambda expression");

    ExprPtr exprBody = nullptr;
    std::unique_ptr<BlockStmt> blockBody = nullptr;

    if (check(TokenKind::LBrace)) {
        blockBody = parseBlock();
    } else {
        exprBody = parseExpr();
    }

    return std::make_unique<LambdaExpr>(std::move(params), std::move(retType),
                                        std::move(exprBody), std::move(blockBody), loc);
}

ExprPtr Parser::parsePrimary() {
    auto loc = current_.location;

    // Match expression
    if (check(TokenKind::Match)) {
        return parseMatchExpr();
    }

    // Integer literal
    if (check(TokenKind::IntLiteral)) {
        auto val = current_.intValue();
        advance();
        return std::make_unique<IntLiteralExpr>(val, loc);
    }

    // Float literal
    if (check(TokenKind::FloatLiteral)) {
        auto val = current_.floatValue();
        bool isSingle = false;
        if (!current_.text.empty()) {
            char last = current_.text.back();
            if (last == 'f' || last == 'F' || current_.text.ends_with("f32")) {
                isSingle = true;
            }
        }
        advance();
        return std::make_unique<FloatLiteralExpr>(val, loc, isSingle);
    }

    // Bool literals
    if (check(TokenKind::True)) {
        advance();
        return std::make_unique<BoolLiteralExpr>(true, loc);
    }
    if (check(TokenKind::False)) {
        advance();
        return std::make_unique<BoolLiteralExpr>(false, loc);
    }

    // Null literal
    if (match(TokenKind::Null)) {
        return std::make_unique<IntLiteralExpr>(0, loc);
    }

    // String literal
    if (check(TokenKind::StringLiteral)) {
        auto val = current_.stringValue();
        advance();
        return std::make_unique<StringLiteralExpr>(std::move(val), loc);
    }

    // Char literal (treated as integer constant of character value)
    if (check(TokenKind::CharLiteral)) {
        auto val = current_.stringValue();
        advance();
        int64_t code = 0;
        if (!val.empty()) {
            unsigned char b0 = static_cast<unsigned char>(val[0]);
            if (b0 < 0x80) {
                code = b0;
            } else if ((b0 & 0xE0) == 0xC0 && val.size() >= 2) {
                code = ((b0 & 0x1F) << 6) | (static_cast<unsigned char>(val[1]) & 0x3F);
            } else if ((b0 & 0xF0) == 0xE0 && val.size() >= 3) {
                code = ((b0 & 0x0F) << 12) |
                       ((static_cast<unsigned char>(val[1]) & 0x3F) << 6) |
                       (static_cast<unsigned char>(val[2]) & 0x3F);
            } else if ((b0 & 0xF8) == 0xF0 && val.size() >= 4) {
                code = ((b0 & 0x07) << 18) |
                       ((static_cast<unsigned char>(val[1]) & 0x3F) << 12) |
                       ((static_cast<unsigned char>(val[2]) & 0x3F) << 6) |
                       (static_cast<unsigned char>(val[3]) & 0x3F);
            } else {
                code = b0;
            }
        }
        return std::make_unique<IntLiteralExpr>(code, loc);
    }

    // 'this' expression
    if (match(TokenKind::This)) {
        return std::make_unique<ThisExpr>(loc);
    }

    // SIMD vector constructors: float4(...), float8(...), int4(...), int8(...), int16(...)
    if (check(TokenKind::Float4) || check(TokenKind::Float8) ||
        check(TokenKind::Int4) || check(TokenKind::Int8) || check(TokenKind::Int16)) {
        std::string typeName(current_.text);
        advance();
        if (match(TokenKind::LParen)) {
            std::vector<ExprPtr> args;
            if (!check(TokenKind::RParen)) {
                do {
                    args.push_back(parseExpr());
                } while (match(TokenKind::Comma));
            }
            expect(TokenKind::RParen, "expected ')' after " + typeName + " constructor arguments");
            return std::make_unique<ConstructExpr>(typeName, std::move(args), loc);
        } else {
            diag_.report(loc, Severity::Error, "expected '(' after SIMD vector type name");
            return std::make_unique<IntLiteralExpr>(0, loc);
        }
    }

    // Identifier (including contextual keywords used as identifiers and qualified names a::b)
    if (checkIdentifier()) {
        auto name = parseQualifiedIdentifier("");
        return std::make_unique<IdentifierExpr>(std::move(name), loc);
    }

    // Lambda expression
    if (isLambdaAhead()) {
        return parseLambda();
    }

    // Parenthesized expression, C-style cast, or compound literal
    if (check(TokenKind::LParen)) {
        auto parenLoc = current_.location;
        auto nextK = lex_.peek().kind;
        bool isType = false;
        if (isTypeNameKind(nextK) || nextK == TokenKind::Struct ||
            nextK == TokenKind::Class || nextK == TokenKind::Interface ||
            (nextK == TokenKind::Identifier && knownTypes_.count(std::string(lex_.peek().text)))) {
            isType = true;
        } else if (nextK == TokenKind::Star) {
            size_t k = 1;
            while (lex_.peek(k).kind == TokenKind::Star) {
                k++;
            }
            auto afterStarK = lex_.peek(k).kind;
            if (isTypeNameKind(afterStarK) || afterStarK == TokenKind::Struct ||
                afterStarK == TokenKind::Class || afterStarK == TokenKind::Interface ||
                (afterStarK == TokenKind::Identifier && knownTypes_.count(std::string(lex_.peek(k).text)))) {
                isType = true;
            }
        }

        if (isType) {
            advance(); // consume '('
            auto castType = parseType();
            expect(TokenKind::RParen, "expected ')' after type");
            if (check(TokenKind::LBrace)) {
                // Compound literal: (Vec2){ 1.0f, 2.0f }
                return parseBracedInitializer(std::move(castType));
            } else {
                // Cast: (int)expr
                auto operand = parseUnary();
                return std::make_unique<CastExpr>(std::move(castType), std::move(operand), parenLoc);
            }
        }

        advance(); // consume '('
        if (check(TokenKind::RParen)) {
            advance();
            return std::make_unique<TupleExpr>(std::vector<ExprPtr>{}, parenLoc);
        }
        auto firstExpr = parseExpr();
        if (!firstExpr) {
            return nullptr;
        }
        if (match(TokenKind::Comma)) {
            std::vector<ExprPtr> elements;
            elements.push_back(std::move(firstExpr));
            if (!check(TokenKind::RParen)) {
                do {
                    elements.push_back(parseExpr());
                } while (match(TokenKind::Comma));
            }
            expect(TokenKind::RParen, "expected ')' after tuple elements");
            return std::make_unique<TupleExpr>(std::move(elements), parenLoc);
        }
        expect(TokenKind::RParen, "expected ')'");
        return firstExpr;
    }

    // Braced initializer { ... }
    if (check(TokenKind::LBrace)) {
        return parseBracedInitializer(nullptr);
    }

    diag_.report(loc, Severity::Error,
                 std::string("unexpected token: '") + std::string(current_.text) + "'");
    hadError_ = true;
    throw std::runtime_error("parse error");
}

ExprPtr Parser::parseBracedInitializer(TypePtr explicitType) {
    auto loc = current_.location;
    expect(TokenKind::LBrace, "expected '{'");
    std::vector<StructInitField> structFields;
    std::vector<ExprPtr> arrayElems;
    bool isStructInit = (explicitType != nullptr && explicitType->kind == TypeNodeKind::Named);

    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
        auto itemLoc = current_.location;
        if (check(TokenKind::Dot)) {
            isStructInit = true;
            advance(); // consume '.'
            auto fName = parseIdentifier("expected field name in designated initializer");
            expect(TokenKind::Eq, "expected '=' after field name");
            auto val = parseExpr();
            structFields.push_back(StructInitField{std::move(fName), std::move(val), itemLoc});
        } else {
            auto val = parseExpr();
            if (isStructInit) {
                structFields.push_back(StructInitField{"", std::move(val), itemLoc});
            } else {
                arrayElems.push_back(std::move(val));
            }
        }
        if (!match(TokenKind::Comma)) break;
    }
    expect(TokenKind::RBrace, "expected '}'");

    if (isStructInit) {
        if (!arrayElems.empty()) {
            for (auto& el : arrayElems) {
                auto elLoc = el->loc;
                structFields.push_back(StructInitField{"", std::move(el), elLoc});
            }
            arrayElems.clear();
        }
        return std::make_unique<CompoundInitExpr>(std::move(explicitType), std::move(structFields), loc);
    } else {
        return std::make_unique<ArrayInitExpr>(std::move(arrayElems), loc);
    }
}

} // namespace tamarin::parser

