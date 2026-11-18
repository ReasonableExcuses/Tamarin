// ============================================================================
// monomorphizer.h - Zero-Cost Monomorphizer & AST Type Substitutor
// ============================================================================
#pragma once

#include "tamarin/ast/ast.h"
#include "tamarin/sema/types.h"
#include <unordered_map>
#include <string>
#include <memory>
#include <vector>

namespace tamarin::sema {

/// Mapping from type parameter name (e.g. "T") to concrete AST TypeNode.
using TypeSubstMap = std::unordered_map<std::string, const ast::TypeNode*>;

class Monomorphizer {
public:
    Monomorphizer() = default;

    /// Substitute type parameters in a TypeNode, returning a newly cloned TypeNode.
    static ast::TypePtr substituteType(const ast::TypeNode* node, const TypeSubstMap& subst);

    /// Substitute type parameters in an expression, returning a deep clone.
    static ast::ExprPtr substituteExpr(const ast::Expr* expr, const TypeSubstMap& subst);

    /// Substitute type parameters in a statement, returning a deep clone.
    static ast::StmtPtr substituteStmt(const ast::Stmt* stmt, const TypeSubstMap& subst);

    /// Monomorphize a generic function declaration into a specialized concrete function.
    static std::unique_ptr<ast::FunctionDecl> monomorphizeFunction(
        const ast::FunctionDecl& templateFn,
        const std::string& specializedName,
        const TypeSubstMap& subst);

    /// Monomorphize a generic struct declaration into a specialized concrete struct.
    static std::unique_ptr<ast::StructDecl> monomorphizeStruct(
        const ast::StructDecl& templateStruct,
        const std::string& specializedName,
        const TypeSubstMap& subst);

    /// Monomorphize a generic enum declaration into a specialized concrete enum.
    static std::unique_ptr<ast::EnumDecl> monomorphizeEnum(
        const ast::EnumDecl& templateEnum,
        const std::string& specializedName,
        const TypeSubstMap& subst);

    /// Generate a sanitized mangled name from a base template name and concrete type arguments.
    static std::string mangleSpecialization(
        const std::string& baseName,
        const std::vector<const Type*>& typeArgs);

    /// Generate a mangled string from an AST TypeNode.
    static std::string mangleTypeNode(const ast::TypeNode* node);

    /// Helper to convert a sema::Type back into an AST TypeNode for substitution mapping.
    static ast::TypePtr typeToASTTypeNode(const Type* type, SourceLocation loc);
};

} // namespace tamarin::sema
