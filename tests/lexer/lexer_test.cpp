// ============================================================================
// lexer_test.cpp - Unit tests for the Tamarin lexer
// ============================================================================

#include "test_harness.h"
#include "tamarin/lexer/lexer.h"
#include "tamarin/source/source_manager.h"
#include "tamarin/diagnostics/diagnostic_engine.h"

#include <sstream>

using namespace tamarin;
using namespace tamarin::lexer;

TAMARIN_TEST(Lexer, SimpleTokens) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn", "int x = 42;");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    Lexer lex(sm, fileId, diag);

    auto t1 = lex.nextToken();
    EXPECT_EQ(t1.kind, TokenKind::Int);

    auto t2 = lex.nextToken();
    EXPECT_EQ(t2.kind, TokenKind::Identifier);
    EXPECT_EQ(t2.text, "x");

    auto t3 = lex.nextToken();
    EXPECT_EQ(t3.kind, TokenKind::Eq);

    auto t4 = lex.nextToken();
    EXPECT_EQ(t4.kind, TokenKind::IntLiteral);
    EXPECT_EQ(t4.intValue(), 42);

    auto t5 = lex.nextToken();
    EXPECT_EQ(t5.kind, TokenKind::Semicolon);

    auto t6 = lex.nextToken();
    EXPECT_EQ(t6.kind, TokenKind::Eof);

    return true;
}

TAMARIN_TEST(Lexer, Operators) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn", "+ - * / % == != < <= > >= && || ++ --");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    Lexer lex(sm, fileId, diag);

    EXPECT_EQ(lex.nextToken().kind, TokenKind::Plus);
    EXPECT_EQ(lex.nextToken().kind, TokenKind::Minus);
    EXPECT_EQ(lex.nextToken().kind, TokenKind::Star);
    EXPECT_EQ(lex.nextToken().kind, TokenKind::Slash);
    EXPECT_EQ(lex.nextToken().kind, TokenKind::Percent);
    EXPECT_EQ(lex.nextToken().kind, TokenKind::EqEq);
    EXPECT_EQ(lex.nextToken().kind, TokenKind::BangEq);
    EXPECT_EQ(lex.nextToken().kind, TokenKind::Lt);
    EXPECT_EQ(lex.nextToken().kind, TokenKind::LtEq);
    EXPECT_EQ(lex.nextToken().kind, TokenKind::Gt);
    EXPECT_EQ(lex.nextToken().kind, TokenKind::GtEq);
    EXPECT_EQ(lex.nextToken().kind, TokenKind::AmpAmp);
    EXPECT_EQ(lex.nextToken().kind, TokenKind::PipePipe);
    EXPECT_EQ(lex.nextToken().kind, TokenKind::PlusPlus);
    EXPECT_EQ(lex.nextToken().kind, TokenKind::MinusMinus);

    return true;
}

TAMARIN_TEST(Lexer, NumberLiterals) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn", "100 0xFF 0b1010 1_000_000");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    Lexer lex(sm, fileId, diag);

    auto t1 = lex.nextToken();
    EXPECT_EQ(t1.kind, TokenKind::IntLiteral);
    EXPECT_EQ(t1.intValue(), 100);

    auto t2 = lex.nextToken();
    EXPECT_EQ(t2.kind, TokenKind::IntLiteral);
    EXPECT_EQ(t2.intValue(), 255);

    auto t3 = lex.nextToken();
    EXPECT_EQ(t3.kind, TokenKind::IntLiteral);
    EXPECT_EQ(t3.intValue(), 10);

    auto t4 = lex.nextToken();
    EXPECT_EQ(t4.kind, TokenKind::IntLiteral);
    EXPECT_EQ(t4.intValue(), 1000000);

    return true;
}

TAMARIN_TEST(Lexer, CommentsSkipped) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn", "// line comment\nint /* block comment */ x;");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    Lexer lex(sm, fileId, diag);

    EXPECT_EQ(lex.nextToken().kind, TokenKind::Int);
    EXPECT_EQ(lex.nextToken().kind, TokenKind::Identifier);
    EXPECT_EQ(lex.nextToken().kind, TokenKind::Semicolon);
    EXPECT_EQ(lex.nextToken().kind, TokenKind::Eof);

    return true;
}

TAMARIN_TEST(Lexer, ArrayViewSyntax) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn", "int values[]");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    Lexer lex(sm, fileId, diag);

    EXPECT_EQ(lex.nextToken().kind, TokenKind::Int);
    EXPECT_EQ(lex.nextToken().kind, TokenKind::Identifier);
    EXPECT_EQ(lex.nextToken().kind, TokenKind::LBracket);
    EXPECT_EQ(lex.nextToken().kind, TokenKind::RBracket);

    return true;
}

TAMARIN_TEST(Lexer, OOPKeywords) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn", "class interface public private protected virtual override this ~");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    Lexer lex(sm, fileId, diag);

    EXPECT_EQ(lex.nextToken().kind, TokenKind::Class);
    EXPECT_EQ(lex.nextToken().kind, TokenKind::Interface);
    EXPECT_EQ(lex.nextToken().kind, TokenKind::Public);
    EXPECT_EQ(lex.nextToken().kind, TokenKind::Private);
    EXPECT_EQ(lex.nextToken().kind, TokenKind::Protected);
    EXPECT_EQ(lex.nextToken().kind, TokenKind::Virtual);
    EXPECT_EQ(lex.nextToken().kind, TokenKind::Override);
    EXPECT_EQ(lex.nextToken().kind, TokenKind::This);
    EXPECT_EQ(lex.nextToken().kind, TokenKind::Tilde);
    EXPECT_EQ(lex.nextToken().kind, TokenKind::Eof);

    return true;
}

TAMARIN_TEST(Lexer, PatternMatchingTokens) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn", "match enum => ? _");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    Lexer lex(sm, fileId, diag);

    EXPECT_EQ(lex.nextToken().kind, TokenKind::Match);
    EXPECT_EQ(lex.nextToken().kind, TokenKind::Enum);
    EXPECT_EQ(lex.nextToken().kind, TokenKind::FatArrow);
    EXPECT_EQ(lex.nextToken().kind, TokenKind::Question);
    auto wild = lex.nextToken();
    EXPECT_EQ(wild.kind, TokenKind::Identifier);
    EXPECT_EQ(wild.text, "_");
    EXPECT_EQ(lex.nextToken().kind, TokenKind::Eof);

    return true;
}


