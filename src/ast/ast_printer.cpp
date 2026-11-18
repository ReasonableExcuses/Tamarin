// ============================================================================
// ast_printer.cpp - Debug printer for the Tamarin AST
// ============================================================================

#include "tamarin/ast/ast_printer.h"
#include <iostream>

namespace tamarin::ast {

void ASTPrinter::pad(int indent) {
    for (int i = 0; i < indent; ++i) os_ << "  ";
}

void ASTPrinter::print(const Program& prog) {
    os_ << "Program\n";
    for (auto& imp : prog.imports) {
        pad(1);
        os_ << "Import " << imp.modulePath;
        if (!imp.alias.empty()) os_ << " as " << imp.alias;
        os_ << "\n";
    }
    for (auto& d : prog.decls) printDecl(*d, 1);
}

void ASTPrinter::printType(const TypeNode& type) {
    switch (type.kind) {
        case TypeNodeKind::Primitive: {
            auto& pt = static_cast<const PrimitiveTypeNode&>(type);
            static const char* names[] = {
                "void","bool","char","byte","short","int","long",
                "unsigned char","unsigned short","unsigned int","unsigned long",
                "float","double","size_t","ptrdiff_t",
                "u8","u16","u32","u64","u128","usize",
                "i8","i16","i32","i64","i128","isize",
                "float4","float8","int4","int8","int16",
            };
            os_ << names[static_cast<int>(pt.primKind)];
            break;
        }
        case TypeNodeKind::Array: {
            auto& at = static_cast<const ArrayTypeNode&>(type);
            printType(*at.elementType);
            os_ << "[]";
            break;
        }
        case TypeNodeKind::Named: {
            auto& nt = static_cast<const NamedTypeNode&>(type);
            os_ << nt.name;
            if (!nt.typeArgs.empty()) {
                os_ << "<";
                for (size_t i = 0; i < nt.typeArgs.size(); ++i) {
                    if (i > 0) os_ << ", ";
                    printType(*nt.typeArgs[i]);
                }
                os_ << ">";
            }
            break;
        }
        case TypeNodeKind::Reference: {
            auto& rt = static_cast<const ReferenceTypeNode&>(type);
            if (rt.isConst) os_ << "const ";
            printType(*rt.referentType);
            os_ << "&";
            break;
        }
        case TypeNodeKind::Handle: {
            auto& ht = static_cast<const HandleTypeNode&>(type);
            os_ << "Handle<";
            if (ht.innerType) printType(*ht.innerType);
            os_ << ">";
            break;
        }
        case TypeNodeKind::Slab: {
            auto& st = static_cast<const SlabTypeNode&>(type);
            os_ << "Slab<";
            if (st.elementType) printType(*st.elementType);
            os_ << ">";
            break;
        }
        case TypeNodeKind::Auto: {
            auto& at = static_cast<const AutoTypeNode&>(type);
            if (at.isConst) os_ << "const ";
            os_ << "auto";
            break;
        }
        case TypeNodeKind::Tuple: {
            auto& tt = static_cast<const TupleTypeNode&>(type);
            os_ << "(";
            for (size_t i = 0; i < tt.elements.size(); ++i) {
                if (i > 0) os_ << ", ";
                printType(*tt.elements[i]);
            }
            os_ << ")";
            break;
        }
        case TypeNodeKind::Function: {
            auto& ft = static_cast<const FunctionTypeNode&>(type);
            os_ << "fn(";
            for (size_t i = 0; i < ft.paramTypes.size(); ++i) {
                if (i > 0) os_ << ", ";
                printType(*ft.paramTypes[i]);
            }
            os_ << ") -> ";
            printType(*ft.returnType);
            break;
        }
        case TypeNodeKind::Task: {
            auto& tt = static_cast<const TaskTypeNode&>(type);
            os_ << "Task<";
            if (tt.elementType) printType(*tt.elementType);
            else os_ << "void";
            os_ << ">";
            break;
        }
    }
}

void ASTPrinter::printDecl(const Decl& decl, int indent) {
    pad(indent);
    switch (decl.kind) {
        case DeclKind::Function: {
            auto& fn = static_cast<const FunctionDecl&>(decl);
            os_ << "FunctionDecl: ";
            printType(*fn.returnType);
            os_ << " " << fn.name;
            if (!fn.typeParams.empty()) {
                os_ << "<";
                for (size_t i = 0; i < fn.typeParams.size(); ++i) {
                    if (i > 0) os_ << ", ";
                    os_ << fn.typeParams[i];
                }
                os_ << ">";
            }
            os_ << "(";
            for (size_t i = 0; i < fn.params.size(); ++i) {
                if (i > 0) os_ << ", ";
                auto& p = fn.params[i];
                if (p.isConst) os_ << "const ";
                printType(*p.type);
                if (p.isArray) os_ << "[]";
                os_ << " " << p.name;
            }
            os_ << ")\n";
            if (fn.body) printStmt(*fn.body, indent + 1);
            break;
        }
        case DeclKind::Const: {
            auto& cd = static_cast<const ConstDecl&>(decl);
            os_ << "ConstDecl: ";
            printType(*cd.type);
            os_ << " " << cd.name << "\n";
            if (cd.value) printExpr(*cd.value, indent + 1);
            break;
        }
        case DeclKind::Struct: {
            auto& sd = static_cast<const StructDecl&>(decl);
            os_ << "StructDecl: " << sd.name;
            if (!sd.typeParams.empty()) {
                os_ << "<";
                for (size_t i = 0; i < sd.typeParams.size(); ++i) {
                    if (i > 0) os_ << ", ";
                    os_ << sd.typeParams[i];
                }
                os_ << ">";
            }
            os_ << "\n";
            for (auto& f : sd.fields) {
                pad(indent + 1);
                printType(*f.type);
                os_ << " " << f.name << "\n";
            }
            break;
        }
        case DeclKind::Trait: {
            auto& td = static_cast<const TraitDecl&>(decl);
            os_ << "TraitDecl: " << td.name << "\n";
            for (auto& m : td.methods) {
                pad(indent + 1);
                printType(*m.returnType);
                os_ << " " << m.name << "()\n";
            }
            break;
        }
        case DeclKind::Impl: {
            auto& id = static_cast<const ImplDecl&>(decl);
            os_ << "ImplDecl: " << id.traitName << " for ";
            printType(*id.targetType);
            os_ << "\n";
            break;
        }
        case DeclKind::Enum: {
            auto& ed = static_cast<const EnumDecl&>(decl);
            os_ << "EnumDecl: " << ed.name;
            if (!ed.typeParams.empty()) {
                os_ << "<";
                for (size_t i = 0; i < ed.typeParams.size(); ++i) {
                    if (i > 0) os_ << ", ";
                    os_ << ed.typeParams[i];
                }
                os_ << ">";
            }
            os_ << "\n";
            for (const auto& v : ed.variants) {
                pad(indent + 1);
                os_ << v.name;
                if (!v.payloadTypes.empty()) {
                    os_ << "(";
                    for (size_t i = 0; i < v.payloadTypes.size(); ++i) {
                        if (i > 0) os_ << ", ";
                        printType(*v.payloadTypes[i]);
                    }
                    os_ << ")";
                }
                os_ << "\n";
            }
            break;
        }
        case DeclKind::Var: {
            auto& vd = static_cast<const GlobalVarDecl&>(decl);
            os_ << "GlobalVarDecl: ";
            printType(*vd.type);
            os_ << " " << vd.name << "\n";
            break;
        }
    }
}

void ASTPrinter::printStmt(const Stmt& stmt, int indent) {
    pad(indent);
    switch (stmt.kind) {
        case StmtKind::Block: {
            auto& b = static_cast<const BlockStmt&>(stmt);
            os_ << "Block\n";
            for (auto& s : b.stmts) printStmt(*s, indent + 1);
            break;
        }
        case StmtKind::VarDecl: {
            auto& v = static_cast<const VarDeclStmt&>(stmt);
            os_ << "VarDecl: ";
            if (v.isConst) os_ << "const ";
            printType(*v.type);
            if (v.isArray) os_ << "[]";
            os_ << " " << v.name << "\n";
            if (v.initializer) printExpr(*v.initializer, indent + 1);
            break;
        }
        case StmtKind::DestructureVarDecl: {
            auto& d = static_cast<const DestructureVarDeclStmt&>(stmt);
            os_ << "DestructureVarDecl: ";
            if (d.isConst) os_ << "const ";
            os_ << "(";
            for (size_t i = 0; i < d.names.size(); ++i) {
                if (i > 0) os_ << ", ";
                os_ << d.names[i];
            }
            os_ << ")\n";
            if (d.initializer) printExpr(*d.initializer, indent + 1);
            break;
        }
        case StmtKind::Expr: {
            auto& e = static_cast<const ExprStmt&>(stmt);
            os_ << "ExprStmt\n";
            if (e.expr) printExpr(*e.expr, indent + 1);
            break;
        }
        case StmtKind::Return: {
            auto& r = static_cast<const ReturnStmt&>(stmt);
            os_ << "Return\n";
            if (r.value) printExpr(*r.value, indent + 1);
            break;
        }
        case StmtKind::If: {
            auto& i = static_cast<const IfStmt&>(stmt);
            os_ << "If\n";
            pad(indent + 1); os_ << "Cond:\n"; printExpr(*i.condition, indent + 2);
            pad(indent + 1); os_ << "Then:\n"; printStmt(*i.thenBlock, indent + 2);
            if (i.elseBlock) { pad(indent + 1); os_ << "Else:\n"; printStmt(*i.elseBlock, indent + 2); }
            break;
        }
        case StmtKind::While: {
            auto& w = static_cast<const WhileStmt&>(stmt);
            os_ << "While\n";
            printExpr(*w.condition, indent + 1);
            printStmt(*w.body, indent + 1);
            break;
        }
        case StmtKind::For: {
            auto& f = static_cast<const ForStmt&>(stmt);
            os_ << "For\n";
            if (f.init) printStmt(*f.init, indent + 1);
            if (f.condition) { pad(indent + 1); os_ << "Cond:\n"; printExpr(*f.condition, indent + 2); }
            if (f.increment) { pad(indent + 1); os_ << "Inc:\n"; printExpr(*f.increment, indent + 2); }
            printStmt(*f.body, indent + 1);
            break;
        }
        case StmtKind::Break: os_ << "Break\n"; break;
        case StmtKind::Continue: os_ << "Continue\n"; break;
        case StmtKind::Switch: os_ << "Switch\n"; break;
        case StmtKind::Match: {
            auto& m = static_cast<const MatchStmt&>(stmt);
            os_ << "MatchStmt\n";
            if (m.expr) {
                pad(indent + 1); os_ << "Target:\n";
                printExpr(*m.expr, indent + 2);
            }
            for (const auto& c : m.cases) {
                pad(indent + 1); os_ << "Case:\n";
                if (c.body) printStmt(*c.body, indent + 2);
            }
            break;
        }
        case StmtKind::Region: {
            auto& r = static_cast<const RegionStmt&>(stmt);
            os_ << "Region " << r.name << "\n";
            if (r.capacityExpr) {
                pad(indent + 1);
                os_ << "Capacity:\n";
                printExpr(*r.capacityExpr, indent + 2);
            }
            if (r.body) printStmt(*r.body, indent + 1);
            break;
        }
        case StmtKind::ParallelFor: {
            auto& pf = static_cast<const ParallelForStmt&>(stmt);
            os_ << "ParallelFor\n";
            if (pf.init) printStmt(*pf.init, indent + 1);
            if (pf.condition) printExpr(*pf.condition, indent + 1);
            if (pf.increment) printExpr(*pf.increment, indent + 1);
            if (pf.body) printStmt(*pf.body, indent + 1);
            break;
        }
        case StmtKind::TaskScope: {
            auto& ts = static_cast<const TaskScopeStmt&>(stmt);
            os_ << "TaskScope\n";
            if (ts.body) printStmt(*ts.body, indent + 1);
            break;
        }
        case StmtKind::Spawn: {
            auto& sp = static_cast<const SpawnStmt&>(stmt);
            os_ << "Spawn\n";
            if (sp.body) printStmt(*sp.body, indent + 1);
            break;
        }
    }
}

void ASTPrinter::printExpr(const Expr& expr, int indent) {
    pad(indent);
    switch (expr.kind) {
        case ExprKind::IntLiteral:
            os_ << "Int(" << static_cast<const IntLiteralExpr&>(expr).value << ")\n"; break;
        case ExprKind::FloatLiteral:
            os_ << "Float(" << static_cast<const FloatLiteralExpr&>(expr).value << ")\n"; break;
        case ExprKind::BoolLiteral:
            os_ << "Bool(" << (static_cast<const BoolLiteralExpr&>(expr).value ? "true" : "false") << ")\n"; break;
        case ExprKind::StringLiteral:
            os_ << "String(\"" << static_cast<const StringLiteralExpr&>(expr).value << "\")\n"; break;
        case ExprKind::Identifier:
            os_ << "Id(" << static_cast<const IdentifierExpr&>(expr).name << ")\n"; break;
        case ExprKind::Binary: {
            auto& b = static_cast<const BinaryExpr&>(expr);
            static const char* ops[] = {"+","-","*","/","%","<",">","<=",">=","==","!=","&&","||","&","|","^","<<",">>"};
            os_ << "BinOp(" << ops[static_cast<int>(b.op)] << ")\n";
            printExpr(*b.lhs, indent + 1);
            printExpr(*b.rhs, indent + 1);
            break;
        }
        case ExprKind::Unary: {
            auto& u = static_cast<const UnaryExpr&>(expr);
            os_ << "UnaryOp\n";
            printExpr(*u.operand, indent + 1);
            break;
        }
        case ExprKind::Assign: {
            auto& a = static_cast<const AssignExpr&>(expr);
            os_ << "Assign\n";
            printExpr(*a.target, indent + 1);
            printExpr(*a.value, indent + 1);
            break;
        }
        case ExprKind::Call: {
            auto& c = static_cast<const CallExpr&>(expr);
            os_ << "Call(" << c.callee << ")\n";
            for (auto& a : c.args) printExpr(*a, indent + 1);
            break;
        }
        case ExprKind::MethodCall: {
            auto& mc = static_cast<const MethodCallExpr&>(expr);
            os_ << "MethodCall(" << mc.methodName << ")\n";
            printExpr(*mc.object, indent + 1);
            for (auto& a : mc.args) printExpr(*a, indent + 1);
            break;
        }
        case ExprKind::Index: {
            auto& i = static_cast<const IndexExpr&>(expr);
            os_ << "Index\n";
            printExpr(*i.array, indent + 1);
            printExpr(*i.index, indent + 1);
            break;
        }
        case ExprKind::Field: {
            auto& f = static_cast<const FieldExpr&>(expr);
            os_ << "Field(" << f.fieldName << ")\n";
            printExpr(*f.object, indent + 1);
            break;
        }
        case ExprKind::Cast: {
            os_ << "Cast\n"; break;
        }
        case ExprKind::ArrayInit: {
            auto& a = static_cast<const ArrayInitExpr&>(expr);
            os_ << "ArrayInit\n";
            for (auto& e : a.elements) printExpr(*e, indent + 1);
            break;
        }
        case ExprKind::CompoundInit: {
            auto& ci = static_cast<const CompoundInitExpr&>(expr);
            os_ << "CompoundInit";
            if (ci.type) { os_ << " "; printType(*ci.type); }
            os_ << "\n";
            for (auto& f : ci.fields) {
                pad(indent + 1);
                if (!f.fieldName.empty()) os_ << "." << f.fieldName << " = ";
                os_ << "\n";
                if (f.value) printExpr(*f.value, indent + 2);
            }
            break;
        }
        case ExprKind::This: {
            os_ << "This\n";
            break;
        }
        case ExprKind::Construct: {
            auto& c = static_cast<const ConstructExpr&>(expr);
            os_ << "Construct(" << c.typeName << ")\n";
            for (auto& a : c.args) printExpr(*a, indent + 1);
            break;
        }
        case ExprKind::Match: {
            auto& m = static_cast<const MatchExpr&>(expr);
            os_ << "MatchExpr\n";
            if (m.expr) {
                pad(indent + 1); os_ << "Target:\n";
                printExpr(*m.expr, indent + 2);
            }
            break;
        }
        case ExprKind::Try: {
            auto& t = static_cast<const TryExpr&>(expr);
            os_ << "TryExpr\n";
            if (t.operand) printExpr(*t.operand, indent + 1);
            break;
        }
        case ExprKind::Ternary: {
            auto& t = static_cast<const TernaryExpr&>(expr);
            os_ << "TernaryExpr\n";
            if (t.condition) {
                pad(indent + 1); os_ << "Cond:\n";
                printExpr(*t.condition, indent + 2);
            }
            if (t.thenExpr) {
                pad(indent + 1); os_ << "Then:\n";
                printExpr(*t.thenExpr, indent + 2);
            }
            if (t.elseExpr) {
                pad(indent + 1); os_ << "Else:\n";
                printExpr(*t.elseExpr, indent + 2);
            }
            break;
        }
        case ExprKind::Tuple: {
            auto& te = static_cast<const TupleExpr&>(expr);
            os_ << "TupleExpr\n";
            for (auto& el : te.elements) printExpr(*el, indent + 1);
            break;
        }
        case ExprKind::TupleIndex: {
            auto& tie = static_cast<const TupleIndexExpr&>(expr);
            os_ << "TupleIndex(" << tie.index << ")\n";
            printExpr(*tie.tuple, indent + 1);
            break;
        }
        case ExprKind::Lambda: {
            auto& le = static_cast<const LambdaExpr&>(expr);
            os_ << "Lambda(";
            for (size_t i = 0; i < le.params.size(); ++i) {
                if (i > 0) os_ << ", ";
                os_ << le.params[i].name;
            }
            os_ << ")\n";
            if (le.exprBody) printExpr(*le.exprBody, indent + 1);
            if (le.blockBody) printStmt(*le.blockBody, indent + 1);
            break;
        }
        case ExprKind::Range: {
            auto& re = static_cast<const RangeExpr&>(expr);
            os_ << "Range\n";
            if (re.start) {
                pad(indent + 1); os_ << "Start:\n";
                printExpr(*re.start, indent + 2);
            }
            if (re.end) {
                pad(indent + 1); os_ << "End:\n";
                printExpr(*re.end, indent + 2);
            }
            break;
        }
        case ExprKind::Await: {
            auto& aw = static_cast<const AwaitExpr&>(expr);
            os_ << "Await\n";
            if (aw.operand) printExpr(*aw.operand, indent + 1);
            break;
        }
    }
}

} // namespace tamarin::ast

