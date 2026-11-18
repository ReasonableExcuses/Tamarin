// ============================================================================
// ast_printer.h - Debug printer for the Tamarin AST
// ============================================================================
#pragma once

#include "tamarin/ast/ast.h"
#include <ostream>
#include <string>

namespace tamarin::ast {

class ASTPrinter {
public:
    explicit ASTPrinter(std::ostream& os) : os_(os) {}
    void print(const Program& prog);
private:
    void printDecl(const Decl& decl, int indent);
    void printStmt(const Stmt& stmt, int indent);
    void printExpr(const Expr& expr, int indent);
    void printType(const TypeNode& type);
    void pad(int indent);
    std::ostream& os_;
};

} // namespace tamarin::ast

