// ============================================================================
// parser.h - Tamarin recursive descent parser
// ============================================================================
#pragma once

#include "tamarin/ast/ast.h"
#include "tamarin/lexer/lexer.h"
#include "tamarin/diagnostics/diagnostic_engine.h"
#include "tamarin/target/target_triple.h"

#include <unordered_set>

namespace tamarin::parser {

struct LoopAttributes {
    int unrollCount = 0;
    int vectorizeWidth = 0;
    int interleaveCount = 0;
    int vectorizeEnable = 0;

    LoopAttributes() = default;
    LoopAttributes(int unroll) : unrollCount(unroll) {}
};

class Parser {
public:
    using LoopAttributes = tamarin::parser::LoopAttributes;
    Parser(lexer::Lexer& lex, DiagnosticEngine& diag);

    /// Parse a complete translation unit.
    [[nodiscard]] ast::Program parseProgram();

    void addKnownType(const std::string& name) { knownTypes_.insert(name); }

    void setTargetTriple(target::TargetTriple triple) { targetTriple_ = std::move(triple); }
    const target::TargetTriple& getTargetTriple() const { return targetTriple_; }

private:
    // ── Token access ──────────────────────────────────────────────────
    const lexer::Token& peek();
    lexer::Token advance();
    bool check(lexer::TokenKind kind);
    bool checkIdentifier();
    std::string parseIdentifier(const std::string& msg);
    std::string parseQualifiedIdentifier(const std::string& msg);
    bool match(lexer::TokenKind kind);
    lexer::Token expect(lexer::TokenKind kind, const std::string& msg);
    bool expectClosingAngle(const std::string& msg);
    [[noreturn]] void error(const std::string& msg);

    // ── Declarations ──────────────────────────────────────────────────
    ast::ImportDecl parseImport();
    ast::DeclPtr parseTopDecl(bool defaultExtern = false, const std::string& defaultLinkage = "",
                              std::shared_ptr<ast::CfgExpr> initialCfg = nullptr);
    ast::DeclPtr parseEnumDecl(bool isPublic = false);
    ast::DeclPtr parseTraitDecl(bool isPublic = false);
    ast::DeclPtr parseImplDecl();
    ast::DeclPtr parseStructOrClassDecl(ast::LayoutDirective layout, SourceLocation loc,
                                        bool isClass = false, bool isInterface = false, bool isPublic = false);
    ast::DeclPtr parseFunctionDecl(ast::TypePtr retType, const std::string& name,
                                   SourceLocation loc, bool isConst, bool isPublic = false,
                                   bool isNoInline = false, bool isAlwaysInline = false,
                                   std::vector<std::string> typeParams = {},
                                   std::vector<std::pair<std::string, std::string>> traitBounds = {},
                                   bool isExtern = false,
                                   const std::string& linkage = "",
                                   bool isExported = false,
                                   bool isAsync = false);
    ast::ParamDecl parseParam();

    // ── Types ─────────────────────────────────────────────────────────
    ast::TypePtr parseType();
    bool isTypeName();
    bool isTypeNameKind(lexer::TokenKind k) const;
    bool isGenericCallLookahead();

    // ── Statements ────────────────────────────────────────────────────
    ast::StmtPtr parseStmt();
    std::unique_ptr<ast::BlockStmt> parseBlock();

    ast::StmtPtr parseStatementOrBlock();
    ast::StmtPtr parseVarDeclOrExprStmt(bool isConst);
    ast::StmtPtr parseDestructureVarDeclStmt(bool isConst, SourceLocation loc);
    ast::StmtPtr parseIfStmt();
    ast::StmtPtr parseWhileStmt(LoopAttributes attrs = {});
    ast::StmtPtr parseForStmt(LoopAttributes attrs = {});
    ast::StmtPtr parseParallelForStmt(LoopAttributes attrs = {});
    ast::StmtPtr parseTaskScopeStmt();
    ast::StmtPtr parseSpawnStmt();
    ast::StmtPtr parseReturnStmt();
    ast::StmtPtr parseBreakStmt();
    ast::StmtPtr parseContinueStmt();
    ast::StmtPtr parseSwitchStmt();
    ast::StmtPtr parseRegionStmt();
    ast::StmtPtr parseMatchStmt();
    ast::StmtPtr parseSelectStmt();

    // ── Expressions (precedence climbing) ─────────────────────────────
    ast::ExprPtr parseExpr();
    ast::ExprPtr parseAssignment();
    ast::ExprPtr parseConditional();
    ast::ExprPtr parseLogicOr();
    ast::ExprPtr parseLogicAnd();
    ast::ExprPtr parseComparison();
    ast::ExprPtr parseRelational();
    ast::ExprPtr parseBitOr();
    ast::ExprPtr parseBitXor();
    ast::ExprPtr parseBitAnd();
    ast::ExprPtr parseShift();
    ast::ExprPtr parseAddSub();
    ast::ExprPtr parseMulDiv();
    ast::ExprPtr parseUnary();
    ast::ExprPtr parsePostfix();
    ast::ExprPtr parsePrimary();
    bool isLambdaAhead();
    ast::ExprPtr parseLambda();
    ast::ExprPtr parseMatchExpr();
    ast::MatchPattern parseMatchPattern();
    ast::MatchCase parseMatchCase();
    ast::ExprPtr parseBracedInitializer(ast::TypePtr explicitType = nullptr);

    // ── Conditional Compilation ────────────────────────────────────────
    std::shared_ptr<ast::CfgExpr> parseCfgExpr();
    std::shared_ptr<ast::CfgExpr> parseCfgItem();

    // ── State ─────────────────────────────────────────────────────────
    lexer::Lexer& lex_;
    DiagnosticEngine& diag_;
    lexer::Token current_;
    bool hadError_ = false;
    std::unordered_set<std::string> knownTypes_;
    target::TargetTriple targetTriple_ = target::TargetTriple::getHost();
    int exprDepth_ = 0;
    int stmtDepth_ = 0;
    static constexpr int MAX_EXPR_DEPTH = 256;
    static constexpr int MAX_STMT_DEPTH = 256;
};

} // namespace tamarin::parser

