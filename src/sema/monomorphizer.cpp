// ============================================================================
// monomorphizer.cpp - Zero-Cost Monomorphizer & AST Type Substitutor
// ============================================================================

#include "tamarin/sema/monomorphizer.h"
#include <algorithm>

namespace tamarin::sema {

using namespace ast;

// ── Mangling Helpers ────────────────────────────────────────────────────────

static std::string sanitizeMangleName(std::string str) {
    for (char& c : str) {
        if (c == '<' || c == '>' || c == ',' || c == ' ' || c == '&' ||
            c == '*' || c == '[' || c == ']' || c == ':' || c == '.' || c == '(' || c == ')') {
            c = '_';
        }
    }
    // Remove consecutive underscores
    std::string clean;
    bool lastUnderscore = false;
    for (char c : str) {
        if (c == '_') {
            if (!lastUnderscore) clean += c;
            lastUnderscore = true;
        } else {
            clean += c;
            lastUnderscore = false;
        }
    }
    return clean;
}

std::string Monomorphizer::mangleSpecialization(
    const std::string& baseName,
    const std::vector<const Type*>& typeArgs) {
    std::string mangled = baseName;
    for (const auto* t : typeArgs) {
        mangled += "__";
        if (t) {
            mangled += sanitizeMangleName(t->toString());
        } else {
            mangled += "unknown";
        }
    }
    return mangled;
}

std::string Monomorphizer::mangleTypeNode(const ast::TypeNode* node) {
    if (!node) return "unknown";
    switch (node->kind) {
        case TypeNodeKind::Primitive: {
            auto& pt = static_cast<const PrimitiveTypeNode&>(*node);
            switch (pt.primKind) {
                case PrimitiveKind::Void: return "void";
                case PrimitiveKind::Bool: return "bool";
                case PrimitiveKind::Char: return "char";
                case PrimitiveKind::Byte: return "byte";
                case PrimitiveKind::Short: return "short";
                case PrimitiveKind::Int: return "int";
                case PrimitiveKind::Long: return "long";
                case PrimitiveKind::UnsignedChar: return "uchar";
                case PrimitiveKind::UnsignedShort: return "ushort";
                case PrimitiveKind::UnsignedInt: return "uint";
                case PrimitiveKind::UnsignedLong: return "ulong";
                case PrimitiveKind::Float: return "float";
                case PrimitiveKind::Double: return "double";
                case PrimitiveKind::SizeT: return "size_t";
                case PrimitiveKind::PtrdiffT: return "ptrdiff_t";
                case PrimitiveKind::U8: return "u8";
                case PrimitiveKind::U16: return "u16";
                case PrimitiveKind::U32: return "u32";
                case PrimitiveKind::U64: return "u64";
                case PrimitiveKind::U128: return "u128";
                case PrimitiveKind::Usize: return "usize";
                case PrimitiveKind::I8: return "i8";
                case PrimitiveKind::I16: return "i16";
                case PrimitiveKind::I32: return "i32";
                case PrimitiveKind::I64: return "i64";
                case PrimitiveKind::I128: return "i128";
                case PrimitiveKind::Isize: return "isize";
                case PrimitiveKind::Float4: return "float4";
                case PrimitiveKind::Float8: return "float8";
                case PrimitiveKind::Int4: return "int4";
                case PrimitiveKind::Int8: return "int8";
                case PrimitiveKind::Int16: return "int16";
            }
            return "prim";
        }
        case TypeNodeKind::Array: {
            auto& at = static_cast<const ArrayTypeNode&>(*node);
            return "array_" + mangleTypeNode(at.elementType.get());
        }
        case TypeNodeKind::Named: {
            auto& nt = static_cast<const NamedTypeNode&>(*node);
            std::string res = nt.name;
            for (const auto& a : nt.typeArgs) {
                res += "_" + mangleTypeNode(a.get());
            }
            return sanitizeMangleName(res);
        }
        case TypeNodeKind::Reference: {
            auto& rt = static_cast<const ReferenceTypeNode&>(*node);
            return "ref_" + mangleTypeNode(rt.referentType.get());
        }
        case TypeNodeKind::Handle: {
            auto& ht = static_cast<const HandleTypeNode&>(*node);
            return "Handle_" + mangleTypeNode(ht.innerType.get());
        }
        case TypeNodeKind::Slab: {
            auto& st = static_cast<const SlabTypeNode&>(*node);
            return "Slab_" + mangleTypeNode(st.elementType.get());
        }
        case TypeNodeKind::Auto: {
            return "auto";
        }
        case TypeNodeKind::Tuple: {
            auto& tt = static_cast<const TupleTypeNode&>(*node);
            std::string s = "tuple_";
            for (const auto& el : tt.elements) s += mangleTypeNode(el.get()) + "_";
            return s;
        }
        case TypeNodeKind::Function: {
            auto& ft = static_cast<const FunctionTypeNode&>(*node);
            std::string s = "fn_";
            for (const auto& p : ft.paramTypes) s += mangleTypeNode(p.get()) + "_";
            s += "ret_" + mangleTypeNode(ft.returnType.get());
            return s;
        }
        case TypeNodeKind::Task: {
            auto& tt = static_cast<const TaskTypeNode&>(*node);
            return "Task_" + (tt.elementType ? mangleTypeNode(tt.elementType.get()) : "void");
        }
    }
    return "unknown";
}

ast::TypePtr Monomorphizer::typeToASTTypeNode(const Type* type, SourceLocation loc) {
    if (!type) return std::make_unique<PrimitiveTypeNode>(PrimitiveKind::Int, loc);

    switch (type->kind) {
        case TypeKind::Void:  return std::make_unique<PrimitiveTypeNode>(PrimitiveKind::Void, loc);
        case TypeKind::Bool:  return std::make_unique<PrimitiveTypeNode>(PrimitiveKind::Bool, loc);
        case TypeKind::I8:    return std::make_unique<PrimitiveTypeNode>(PrimitiveKind::I8, loc);
        case TypeKind::I16:   return std::make_unique<PrimitiveTypeNode>(PrimitiveKind::I16, loc);
        case TypeKind::I32:   return std::make_unique<PrimitiveTypeNode>(PrimitiveKind::I32, loc);
        case TypeKind::I64:   return std::make_unique<PrimitiveTypeNode>(PrimitiveKind::I64, loc);
        case TypeKind::I128:  return std::make_unique<PrimitiveTypeNode>(PrimitiveKind::I128, loc);
        case TypeKind::Isize: return std::make_unique<PrimitiveTypeNode>(PrimitiveKind::Isize, loc);
        case TypeKind::U8:    return std::make_unique<PrimitiveTypeNode>(PrimitiveKind::U8, loc);
        case TypeKind::U16:   return std::make_unique<PrimitiveTypeNode>(PrimitiveKind::U16, loc);
        case TypeKind::U32:   return std::make_unique<PrimitiveTypeNode>(PrimitiveKind::U32, loc);
        case TypeKind::U64:   return std::make_unique<PrimitiveTypeNode>(PrimitiveKind::U64, loc);
        case TypeKind::U128:  return std::make_unique<PrimitiveTypeNode>(PrimitiveKind::U128, loc);
        case TypeKind::Usize: return std::make_unique<PrimitiveTypeNode>(PrimitiveKind::Usize, loc);
        case TypeKind::F32:   return std::make_unique<PrimitiveTypeNode>(PrimitiveKind::Float, loc);
        case TypeKind::F64:   return std::make_unique<PrimitiveTypeNode>(PrimitiveKind::Double, loc);
        case TypeKind::Struct:
        case TypeKind::Class:
        case TypeKind::Interface:
            return std::make_unique<NamedTypeNode>(type->structName, loc);
        case TypeKind::Enum:
            return std::make_unique<NamedTypeNode>(type->enumName, loc);
        case TypeKind::Reference:
            return std::make_unique<ReferenceTypeNode>(typeToASTTypeNode(type->elementType, loc), type->isConstRef, loc, type->isPointer());
        case TypeKind::Handle:
            return std::make_unique<HandleTypeNode>(typeToASTTypeNode(type->elementType, loc), loc);
        case TypeKind::Slab:
            return std::make_unique<SlabTypeNode>(typeToASTTypeNode(type->elementType, loc), nullptr, loc);
        case TypeKind::Array:
            return std::make_unique<ArrayTypeNode>(typeToASTTypeNode(type->elementType, loc),
                                                   std::make_unique<IntLiteralExpr>(type->arraySize, loc), loc, type->isConst);
        case TypeKind::ArrayView:
            return std::make_unique<ArrayTypeNode>(typeToASTTypeNode(type->elementType, loc), nullptr, loc, type->isConst);
        case TypeKind::Tuple: {
            std::vector<TypePtr> elems;
            for (auto el : type->tupleElements) elems.push_back(typeToASTTypeNode(el, loc));
            return std::make_unique<TupleTypeNode>(std::move(elems), loc);
        }
        case TypeKind::Task:
            return std::make_unique<TaskTypeNode>(typeToASTTypeNode(type->elementType, loc), loc);
        case TypeKind::Function: {
            std::vector<TypePtr> params;
            for (auto p : type->paramTypes) params.push_back(typeToASTTypeNode(p, loc));
            return std::make_unique<FunctionTypeNode>(std::move(params), typeToASTTypeNode(type->returnType, loc), loc);
        }
        case TypeKind::SIMDVector: {
            if (type->elementType && type->elementType->isFloat()) {
                if (type->vectorWidth == 4) return std::make_unique<PrimitiveTypeNode>(PrimitiveKind::Float4, loc);
                if (type->vectorWidth == 8) return std::make_unique<PrimitiveTypeNode>(PrimitiveKind::Float8, loc);
            } else if (type->elementType && type->elementType->isInteger()) {
                if (type->vectorWidth == 4) return std::make_unique<PrimitiveTypeNode>(PrimitiveKind::Int4, loc);
                if (type->vectorWidth == 8) return std::make_unique<PrimitiveTypeNode>(PrimitiveKind::Int8, loc);
                if (type->vectorWidth == 16) return std::make_unique<PrimitiveTypeNode>(PrimitiveKind::Int16, loc);
            }
            return std::make_unique<NamedTypeNode>(type->toString(), loc);
        }
        default:
            return std::make_unique<NamedTypeNode>(type->toString(), loc);
    }
}

// ── Type Substitution ───────────────────────────────────────────────────────

ast::TypePtr Monomorphizer::substituteType(const ast::TypeNode* node, const TypeSubstMap& subst) {
    if (!node) return nullptr;

    switch (node->kind) {
        case TypeNodeKind::Primitive: {
            auto& pt = static_cast<const PrimitiveTypeNode&>(*node);
            return std::make_unique<PrimitiveTypeNode>(pt.primKind, pt.loc);
        }
        case TypeNodeKind::Array: {
            auto& at = static_cast<const ArrayTypeNode&>(*node);
            auto elem = substituteType(at.elementType.get(), subst);
            auto sz = substituteExpr(at.sizeExpr.get(), subst);
            return std::make_unique<ArrayTypeNode>(std::move(elem), std::move(sz), at.loc, at.isConst);
        }
        case TypeNodeKind::Named: {
            auto& nt = static_cast<const NamedTypeNode&>(*node);
            // Check if nt.name is a type parameter being replaced
            auto it = subst.find(nt.name);
            if (it != subst.end() && it->second) {
                // Return a clone of the substituted concrete type node
                return substituteType(it->second, {});
            }
            // Otherwise, recursively substitute any nested typeArgs
            std::vector<TypePtr> specializedArgs;
            for (const auto& arg : nt.typeArgs) {
                specializedArgs.push_back(substituteType(arg.get(), subst));
            }
            return std::make_unique<NamedTypeNode>(nt.name, std::move(specializedArgs), nt.loc);
        }
        case TypeNodeKind::Reference: {
            auto& rt = static_cast<const ReferenceTypeNode&>(*node);
            auto elem = substituteType(rt.referentType.get(), subst);
            return std::make_unique<ReferenceTypeNode>(std::move(elem), rt.isConst, rt.loc, rt.isPointer);
        }
        case TypeNodeKind::Handle: {
            auto& ht = static_cast<const HandleTypeNode&>(*node);
            auto inner = substituteType(ht.innerType.get(), subst);
            return std::make_unique<HandleTypeNode>(std::move(inner), ht.loc);
        }
        case TypeNodeKind::Slab: {
            auto& st = static_cast<const SlabTypeNode&>(*node);
            auto elem = substituteType(st.elementType.get(), subst);
            auto cap = substituteExpr(st.capacityExpr.get(), subst);
            return std::make_unique<SlabTypeNode>(std::move(elem), std::move(cap), st.loc);
        }
        case TypeNodeKind::Auto: {
            auto& at = static_cast<const AutoTypeNode&>(*node);
            return std::make_unique<AutoTypeNode>(at.loc, at.isConst);
        }
        case TypeNodeKind::Tuple: {
            auto& tt = static_cast<const TupleTypeNode&>(*node);
            std::vector<TypePtr> elems;
            for (const auto& el : tt.elements) {
                elems.push_back(substituteType(el.get(), subst));
            }
            return std::make_unique<TupleTypeNode>(std::move(elems), tt.loc);
        }
        case TypeNodeKind::Function: {
            auto& ft = static_cast<const FunctionTypeNode&>(*node);
            std::vector<TypePtr> params;
            for (const auto& p : ft.paramTypes) {
                params.push_back(substituteType(p.get(), subst));
            }
            auto ret = substituteType(ft.returnType.get(), subst);
            return std::make_unique<FunctionTypeNode>(std::move(params), std::move(ret), ft.loc);
        }
        case TypeNodeKind::Task: {
            auto& tt = static_cast<const TaskTypeNode&>(*node);
            auto elem = substituteType(tt.elementType.get(), subst);
            return std::make_unique<TaskTypeNode>(std::move(elem), tt.loc);
        }
    }
    return nullptr;
}

// ── Expression Substitution ────────────────────────────────────────────────

ast::ExprPtr Monomorphizer::substituteExpr(const ast::Expr* expr, const TypeSubstMap& subst) {
    if (!expr) return nullptr;

    switch (expr->kind) {
        case ExprKind::IntLiteral: {
            auto& e = static_cast<const IntLiteralExpr&>(*expr);
            return std::make_unique<IntLiteralExpr>(e.value, e.loc);
        }
        case ExprKind::FloatLiteral: {
            auto& e = static_cast<const FloatLiteralExpr&>(*expr);
            return std::make_unique<FloatLiteralExpr>(e.value, e.loc, e.isSinglePrecision);
        }
        case ExprKind::BoolLiteral: {
            auto& e = static_cast<const BoolLiteralExpr&>(*expr);
            return std::make_unique<BoolLiteralExpr>(e.value, e.loc);
        }
        case ExprKind::StringLiteral: {
            auto& e = static_cast<const StringLiteralExpr&>(*expr);
            return std::make_unique<StringLiteralExpr>(e.value, e.loc);
        }
        case ExprKind::Identifier: {
            auto& e = static_cast<const IdentifierExpr&>(*expr);
            return std::make_unique<IdentifierExpr>(e.name, e.loc);
        }
        case ExprKind::Binary: {
            auto& e = static_cast<const BinaryExpr&>(*expr);
            auto lhs = substituteExpr(e.lhs.get(), subst);
            auto rhs = substituteExpr(e.rhs.get(), subst);
            auto res = std::make_unique<BinaryExpr>(e.op, std::move(lhs), std::move(rhs), e.loc);
            res->overloadedMethod = e.overloadedMethod;
            return res;
        }
        case ExprKind::Unary: {
            auto& e = static_cast<const UnaryExpr&>(*expr);
            auto op = substituteExpr(e.operand.get(), subst);
            auto res = std::make_unique<UnaryExpr>(e.op, std::move(op), e.loc);
            res->overloadedMethod = e.overloadedMethod;
            return res;
        }
        case ExprKind::Assign: {
            auto& e = static_cast<const AssignExpr&>(*expr);
            auto target = substituteExpr(e.target.get(), subst);
            auto val = substituteExpr(e.value.get(), subst);
            return std::make_unique<AssignExpr>(e.op, std::move(target), std::move(val), e.loc);
        }
        case ExprKind::Call: {
            auto& e = static_cast<const CallExpr&>(*expr);
            std::vector<TypePtr> typeArgs;
            for (const auto& ta : e.typeArgs) {
                typeArgs.push_back(substituteType(ta.get(), subst));
            }
            std::vector<ExprPtr> args;
            for (const auto& a : e.args) {
                args.push_back(substituteExpr(a.get(), subst));
            }
            if (e.calleeExpr) {
                auto callee = substituteExpr(e.calleeExpr.get(), subst);
                auto res = std::make_unique<CallExpr>(std::move(callee), std::move(args), e.loc);
                res->typeArgs = std::move(typeArgs);
                return res;
            }
            return std::make_unique<CallExpr>(e.callee, std::move(typeArgs), std::move(args), e.loc);
        }
        case ExprKind::MethodCall: {
            auto& e = static_cast<const MethodCallExpr&>(*expr);
            auto obj = substituteExpr(e.object.get(), subst);
            std::vector<TypePtr> typeArgs;
            for (const auto& ta : e.typeArgs) {
                typeArgs.push_back(substituteType(ta.get(), subst));
            }
            std::vector<ExprPtr> args;
            for (const auto& a : e.args) {
                args.push_back(substituteExpr(a.get(), subst));
            }
            return std::make_unique<MethodCallExpr>(std::move(obj), e.methodName, std::move(typeArgs), std::move(args), e.isArrow, e.loc);
        }
        case ExprKind::Index: {
            auto& e = static_cast<const IndexExpr&>(*expr);
            auto arr = substituteExpr(e.array.get(), subst);
            auto idx = substituteExpr(e.index.get(), subst);
            auto res = std::make_unique<IndexExpr>(std::move(arr), std::move(idx), e.loc);
            res->overloadedMethod = e.overloadedMethod;
            return res;
        }
        case ExprKind::Field: {
            auto& e = static_cast<const FieldExpr&>(*expr);
            auto obj = substituteExpr(e.object.get(), subst);
            return std::make_unique<FieldExpr>(std::move(obj), e.fieldName, e.isArrow, e.loc);
        }
        case ExprKind::Cast: {
            auto& e = static_cast<const CastExpr&>(*expr);
            auto ty = substituteType(e.targetType.get(), subst);
            auto op = substituteExpr(e.operand.get(), subst);
            return std::make_unique<CastExpr>(std::move(ty), std::move(op), e.loc);
        }
        case ExprKind::ArrayInit: {
            auto& e = static_cast<const ArrayInitExpr&>(*expr);
            std::vector<ExprPtr> elems;
            for (const auto& el : e.elements) {
                elems.push_back(substituteExpr(el.get(), subst));
            }
            return std::make_unique<ArrayInitExpr>(std::move(elems), e.loc);
        }
        case ExprKind::CompoundInit: {
            auto& e = static_cast<const CompoundInitExpr&>(*expr);
            auto ty = substituteType(e.type.get(), subst);
            std::vector<StructInitField> fields;
            for (const auto& f : e.fields) {
                fields.push_back(StructInitField{
                    f.fieldName,
                    substituteExpr(f.value.get(), subst),
                    f.loc
                });
            }
            return std::make_unique<CompoundInitExpr>(std::move(ty), std::move(fields), e.loc);
        }
        case ExprKind::This: {
            return std::make_unique<ThisExpr>(expr->loc);
        }
        case ExprKind::Construct: {
            auto& e = static_cast<const ConstructExpr&>(*expr);
            std::string typeName = e.typeName;
            auto it = subst.find(typeName);
            if (it != subst.end() && it->second) {
                typeName = mangleTypeNode(it->second);
            }
            std::vector<ExprPtr> args;
            for (const auto& a : e.args) {
                args.push_back(substituteExpr(a.get(), subst));
            }
            return std::make_unique<ConstructExpr>(typeName, std::move(args), e.loc);
        }
        case ExprKind::Match: {
            auto& e = static_cast<const MatchExpr&>(*expr);
            auto matchExpr = substituteExpr(e.expr.get(), subst);
            std::vector<MatchCase> cases;
            for (const auto& c : e.cases) {
                MatchCase mc;
                mc.pattern.kind = c.pattern.kind;
                mc.pattern.loc = c.pattern.loc;
                mc.pattern.enumName = c.pattern.enumName;
                auto it = subst.find(mc.pattern.enumName);
                if (it != subst.end() && it->second) {
                    mc.pattern.enumName = mangleTypeNode(it->second);
                }
                mc.pattern.variantName = c.pattern.variantName;
                mc.pattern.bindings = c.pattern.bindings;
                mc.pattern.literalExpr = substituteExpr(c.pattern.literalExpr.get(), subst);
                mc.guard = substituteExpr(c.guard.get(), subst);
                mc.body = substituteStmt(c.body.get(), subst);
                mc.loc = c.loc;
                cases.push_back(std::move(mc));
            }
            return std::make_unique<MatchExpr>(std::move(matchExpr), std::move(cases), e.loc);
        }
        case ExprKind::Try: {
            auto& e = static_cast<const TryExpr&>(*expr);
            auto op = substituteExpr(e.operand.get(), subst);
            return std::make_unique<TryExpr>(std::move(op), e.loc);
        }
        case ExprKind::Ternary: {
            auto& e = static_cast<const TernaryExpr&>(*expr);
            auto cond = substituteExpr(e.condition.get(), subst);
            auto thenE = substituteExpr(e.thenExpr.get(), subst);
            auto elseE = substituteExpr(e.elseExpr.get(), subst);
            return std::make_unique<TernaryExpr>(std::move(cond), std::move(thenE), std::move(elseE), e.loc);
        }
        case ExprKind::Tuple: {
            auto& e = static_cast<const TupleExpr&>(*expr);
            std::vector<ExprPtr> elems;
            for (const auto& el : e.elements) {
                elems.push_back(substituteExpr(el.get(), subst));
            }
            return std::make_unique<TupleExpr>(std::move(elems), e.loc);
        }
        case ExprKind::TupleIndex: {
            auto& e = static_cast<const TupleIndexExpr&>(*expr);
            auto tup = substituteExpr(e.tuple.get(), subst);
            return std::make_unique<TupleIndexExpr>(std::move(tup), e.index, e.loc);
        }
        case ExprKind::Lambda: {
            auto& e = static_cast<const LambdaExpr&>(*expr);
            std::vector<ParamDecl> params;
            for (const auto& p : e.params) {
                ParamDecl sp;
                sp.name = p.name;
                sp.isConst = p.isConst;
                sp.isArray = p.isArray;
                sp.loc = p.loc;
                if (p.type) sp.type = substituteType(p.type.get(), subst);
                if (p.defaultValue) sp.defaultValue = substituteExpr(p.defaultValue.get(), subst);
                params.push_back(std::move(sp));
            }
            TypePtr ret = nullptr;
            if (e.returnType) ret = substituteType(e.returnType.get(), subst);
            ExprPtr exprB = nullptr;
            if (e.exprBody) exprB = substituteExpr(e.exprBody.get(), subst);
            std::unique_ptr<BlockStmt> blkB = nullptr;
            if (e.blockBody) {
                auto s = substituteStmt(e.blockBody.get(), subst);
                blkB = std::unique_ptr<BlockStmt>(static_cast<BlockStmt*>(s.release()));
            }
            auto res = std::make_unique<LambdaExpr>(std::move(params), std::move(ret), std::move(exprB), std::move(blkB), e.loc);
            res->captures = e.captures;
            return res;
        }
        case ExprKind::Range: {
            auto& e = static_cast<const RangeExpr&>(*expr);
            auto s = substituteExpr(e.start.get(), subst);
            auto end = substituteExpr(e.end.get(), subst);
            return std::make_unique<RangeExpr>(std::move(s), std::move(end), e.loc);
        }
        case ExprKind::Await: {
            auto& e = static_cast<const AwaitExpr&>(*expr);
            auto op = substituteExpr(e.operand.get(), subst);
            return std::make_unique<AwaitExpr>(std::move(op), e.loc);
        }
    }
    return nullptr;
}

// ── Statement Substitution ─────────────────────────────────────────────────

ast::StmtPtr Monomorphizer::substituteStmt(const ast::Stmt* stmt, const TypeSubstMap& subst) {
    if (!stmt) return nullptr;

    switch (stmt->kind) {
        case StmtKind::Expr: {
            auto& s = static_cast<const ExprStmt&>(*stmt);
            auto e = substituteExpr(s.expr.get(), subst);
            return std::make_unique<ExprStmt>(std::move(e), s.loc);
        }
        case StmtKind::VarDecl: {
            auto& s = static_cast<const VarDeclStmt&>(*stmt);
            auto ty = substituteType(s.type.get(), subst);
            auto init = substituteExpr(s.initializer.get(), subst);
            auto arrSz = substituteExpr(s.arraySize.get(), subst);
            return std::make_unique<VarDeclStmt>(std::move(ty), s.name, std::move(init),
                                                 s.isConst, s.isArray, std::move(arrSz), s.loc);
        }
        case StmtKind::Block: {
            auto& s = static_cast<const BlockStmt&>(*stmt);
            std::vector<StmtPtr> stmts;
            for (const auto& child : s.stmts) {
                stmts.push_back(substituteStmt(child.get(), subst));
            }
            return std::make_unique<BlockStmt>(std::move(stmts), s.loc);
        }
        case StmtKind::If: {
            auto& s = static_cast<const IfStmt&>(*stmt);
            auto cond = substituteExpr(s.condition.get(), subst);
            auto thenB = substituteStmt(s.thenBlock.get(), subst);
            StmtPtr elseB = nullptr;
            if (s.elseBlock) {
                elseB = substituteStmt(s.elseBlock.get(), subst);
            }
            return std::make_unique<IfStmt>(std::move(cond), std::move(thenB), std::move(elseB), s.loc);
        }
        case StmtKind::While: {
            auto& s = static_cast<const WhileStmt&>(*stmt);
            auto cond = substituteExpr(s.condition.get(), subst);
            auto body = substituteStmt(s.body.get(), subst);
            auto res = std::make_unique<WhileStmt>(std::move(cond), std::move(body), s.loc, s.unrollCount);
            res->vectorizeWidth = s.vectorizeWidth;
            res->interleaveCount = s.interleaveCount;
            res->vectorizeEnable = s.vectorizeEnable;
            return res;
        }
        case StmtKind::For: {
            auto& s = static_cast<const ForStmt&>(*stmt);
            auto init = substituteStmt(s.init.get(), subst);
            auto cond = substituteExpr(s.condition.get(), subst);
            auto inc = substituteExpr(s.increment.get(), subst);
            auto body = substituteStmt(s.body.get(), subst);
            auto res = std::make_unique<ForStmt>(std::move(init), std::move(cond), std::move(inc), std::move(body), s.loc, s.unrollCount);
            res->vectorizeWidth = s.vectorizeWidth;
            res->interleaveCount = s.interleaveCount;
            res->vectorizeEnable = s.vectorizeEnable;
            return res;
        }
        case StmtKind::Return: {
            auto& s = static_cast<const ReturnStmt&>(*stmt);
            auto val = substituteExpr(s.value.get(), subst);
            return std::make_unique<ReturnStmt>(std::move(val), s.loc);
        }
        case StmtKind::Break:
            return std::make_unique<BreakStmt>(stmt->loc);
        case StmtKind::Continue:
            return std::make_unique<ContinueStmt>(stmt->loc);
        case StmtKind::Region: {
            auto& s = static_cast<const RegionStmt&>(*stmt);
            auto cap = substituteExpr(s.capacityExpr.get(), subst);
            auto body = std::unique_ptr<BlockStmt>(static_cast<BlockStmt*>(substituteStmt(s.body.get(), subst).release()));
            return std::make_unique<RegionStmt>(s.name, std::move(cap), std::move(body), s.loc);
        }
        case StmtKind::Match: {
            auto& s = static_cast<const MatchStmt&>(*stmt);
            auto matchExpr = substituteExpr(s.expr.get(), subst);
            std::vector<MatchCase> cases;
            for (const auto& c : s.cases) {
                MatchCase mc;
                mc.pattern.kind = c.pattern.kind;
                mc.pattern.loc = c.pattern.loc;
                mc.pattern.enumName = c.pattern.enumName;
                auto it = subst.find(mc.pattern.enumName);
                if (it != subst.end() && it->second) {
                    mc.pattern.enumName = mangleTypeNode(it->second);
                }
                mc.pattern.variantName = c.pattern.variantName;
                mc.pattern.bindings = c.pattern.bindings;
                mc.pattern.literalExpr = substituteExpr(c.pattern.literalExpr.get(), subst);
                mc.guard = substituteExpr(c.guard.get(), subst);
                mc.body = substituteStmt(c.body.get(), subst);
                mc.loc = c.loc;
                cases.push_back(std::move(mc));
            }
            return std::make_unique<MatchStmt>(std::move(matchExpr), std::move(cases), s.loc);
        }
        case StmtKind::ParallelFor: {
            auto& s = static_cast<const ParallelForStmt&>(*stmt);
            auto init = substituteStmt(s.init.get(), subst);
            auto cond = substituteExpr(s.condition.get(), subst);
            auto inc = substituteExpr(s.increment.get(), subst);
            auto body = substituteStmt(s.body.get(), subst);
            auto grain = substituteExpr(s.grainSize.get(), subst);
            std::vector<ReductionClause> reds;
            for (const auto& r : s.reductions) {
                reds.push_back(ReductionClause{r.op, r.varName, r.loc});
            }
            auto res = std::make_unique<ParallelForStmt>(std::move(init), std::move(cond),
                                                    std::move(inc), std::move(body),
                                                    std::move(grain), std::move(reds), s.loc);
            res->unrollCount = s.unrollCount;
            res->vectorizeWidth = s.vectorizeWidth;
            res->interleaveCount = s.interleaveCount;
            res->vectorizeEnable = s.vectorizeEnable;
            return res;
        }
        case StmtKind::TaskScope: {
            auto& s = static_cast<const TaskScopeStmt&>(*stmt);
            auto body = std::unique_ptr<BlockStmt>(static_cast<BlockStmt*>(substituteStmt(s.body.get(), subst).release()));
            return std::make_unique<TaskScopeStmt>(std::move(body), s.loc);
        }
        case StmtKind::Spawn: {
            auto& s = static_cast<const SpawnStmt&>(*stmt);
            auto body = substituteStmt(s.body.get(), subst);
            return std::make_unique<SpawnStmt>(std::move(body), s.loc);
        }
        case StmtKind::Select: {
            auto& s = static_cast<const SelectStmt&>(*stmt);
            std::vector<SelectCase> newCases;
            for (const auto& sc : s.cases) {
                SelectCase nc;
                nc.loc = sc.loc;
                if (sc.condition) nc.condition = substituteExpr(sc.condition.get(), subst);
                for (const auto& b : sc.body) {
                    nc.body.push_back(substituteStmt(b.get(), subst));
                }
                newCases.push_back(std::move(nc));
            }
            std::vector<StmtPtr> newDef;
            for (const auto& b : s.defaultBody) {
                newDef.push_back(substituteStmt(b.get(), subst));
            }
            return std::make_unique<SelectStmt>(std::move(newCases), std::move(newDef), s.hasDefault, s.loc);
        }
        case StmtKind::DestructureVarDecl: {
            auto& s = static_cast<const DestructureVarDeclStmt&>(*stmt);
            std::vector<TypePtr> newTypes;
            for (const auto& t : s.types) {
                newTypes.push_back(substituteType(t.get(), subst));
            }
            auto init = substituteExpr(s.initializer.get(), subst);
            return std::make_unique<DestructureVarDeclStmt>(std::move(newTypes), s.names, std::move(init), s.isConst, s.loc);
        }
        case StmtKind::Switch: {
            auto& s = static_cast<const SwitchStmt&>(*stmt);
            auto cond = substituteExpr(s.condition.get(), subst);
            std::vector<CaseClause> newCases;
            for (const auto& c : s.cases) {
                CaseClause nc;
                if (c.value) nc.value = substituteExpr(c.value.get(), subst);
                for (const auto& b : c.body) {
                    nc.body.push_back(substituteStmt(b.get(), subst));
                }
                newCases.push_back(std::move(nc));
            }
            return std::make_unique<SwitchStmt>(std::move(cond), std::move(newCases), s.loc);
        }
    }
    return nullptr;
}

// ── Declaration Monomorphization ───────────────────────────────────────────

std::unique_ptr<ast::FunctionDecl> Monomorphizer::monomorphizeFunction(
    const ast::FunctionDecl& templateFn,
    const std::string& specializedName,
    const TypeSubstMap& subst) {

    auto retTy = substituteType(templateFn.returnType.get(), subst);
    std::vector<ParamDecl> params;
    for (const auto& p : templateFn.params) {
        auto pTy = substituteType(p.type.get(), subst);
        auto sz = substituteExpr(p.arraySize.get(), subst);
        auto defVal = p.defaultValue ? substituteExpr(p.defaultValue.get(), subst) : nullptr;
        params.push_back(ParamDecl{
            std::move(pTy), p.name, p.isConst, p.isArray, std::move(sz), p.loc, std::move(defVal)
        });
    }

    std::unique_ptr<BlockStmt> body;
    if (templateFn.body) {
        body = std::unique_ptr<BlockStmt>(static_cast<BlockStmt*>(substituteStmt(templateFn.body.get(), subst).release()));
    }

    auto fn = std::make_unique<FunctionDecl>(
        std::move(retTy), specializedName, std::move(params), std::move(body),
        templateFn.loc, templateFn.isPublic, templateFn.isNoInline, templateFn.isAlwaysInline);
    fn->isExtern = templateFn.isExtern;
    fn->linkage = templateFn.linkage;
    fn->isExported = templateFn.isExported;
    fn->isAsync = templateFn.isAsync;
    fn->cfg = templateFn.cfg;
    fn->typeParams.clear(); // Monomorphized specialization has no free type params
    fn->traitBounds.clear();
    return fn;
}

std::unique_ptr<ast::StructDecl> Monomorphizer::monomorphizeStruct(
    const ast::StructDecl& templateStruct,
    const std::string& specializedName,
    const TypeSubstMap& subst) {

    std::vector<StructField> fields;
    for (const auto& f : templateStruct.fields) {
        auto fTy = substituteType(f.type.get(), subst);
        fields.push_back(StructField{
            std::move(fTy), f.name, f.isConst, f.isCold, f.access, f.loc
        });
    }

    std::vector<MethodDecl> methods;
    for (const auto& m : templateStruct.methods) {
        auto retTy = substituteType(m.returnType.get(), subst);
        std::vector<ParamDecl> params;
        for (const auto& p : m.params) {
            auto pTy = substituteType(p.type.get(), subst);
            auto sz = substituteExpr(p.arraySize.get(), subst);
            auto defVal = p.defaultValue ? substituteExpr(p.defaultValue.get(), subst) : nullptr;
            params.push_back(ParamDecl{
                std::move(pTy), p.name, p.isConst, p.isArray, std::move(sz), p.loc, std::move(defVal)
            });
        }
        std::unique_ptr<BlockStmt> body;
        if (m.body) {
            body = std::unique_ptr<BlockStmt>(static_cast<BlockStmt*>(substituteStmt(m.body.get(), subst).release()));
        }
        MethodDecl md{
            std::move(retTy), m.name, std::move(params), std::move(body),
            m.isConst, m.isVirtual, m.isOverride, m.isPureVirtual, m.isOperator, m.access, m.loc
        };
        md.isAsync = m.isAsync;
        methods.push_back(std::move(md));
    }

    std::vector<ConstructorDecl> constructors;
    for (const auto& c : templateStruct.constructors) {
        std::vector<ParamDecl> params;
        for (const auto& p : c.params) {
            auto pTy = substituteType(p.type.get(), subst);
            auto sz = substituteExpr(p.arraySize.get(), subst);
            auto defVal = p.defaultValue ? substituteExpr(p.defaultValue.get(), subst) : nullptr;
            params.push_back(ParamDecl{
                std::move(pTy), p.name, p.isConst, p.isArray, std::move(sz), p.loc, std::move(defVal)
            });
        }
        std::unique_ptr<BlockStmt> body;
        if (c.body) {
            body = std::unique_ptr<BlockStmt>(static_cast<BlockStmt*>(substituteStmt(c.body.get(), subst).release()));
        }
        constructors.push_back(ConstructorDecl{
            specializedName, std::move(params), std::move(body), c.access, c.loc
        });
    }

    std::unique_ptr<DestructorDecl> destructor;
    if (templateStruct.destructor) {
        std::unique_ptr<BlockStmt> body;
        if (templateStruct.destructor->body) {
            body = std::unique_ptr<BlockStmt>(static_cast<BlockStmt*>(substituteStmt(templateStruct.destructor->body.get(), subst).release()));
        }
        destructor = std::make_unique<DestructorDecl>(DestructorDecl{
            specializedName, std::move(body), templateStruct.destructor->isVirtual,
            templateStruct.destructor->access, templateStruct.destructor->loc
        });
    }

    std::vector<BaseClassSpec> baseClasses;
    for (const auto& bc : templateStruct.baseClasses) {
        std::string bName = bc.name;
        auto it = subst.find(bName);
        if (it != subst.end() && it->second) {
            bName = mangleTypeNode(it->second);
        }
        baseClasses.push_back(BaseClassSpec{std::move(bName), bc.access, bc.loc});
    }

    auto sd = std::make_unique<StructDecl>(
        specializedName, std::move(fields), templateStruct.layout,
        templateStruct.loc, templateStruct.isClass, templateStruct.isInterface, templateStruct.isPublic);
    sd->typeParams.clear();
    sd->traitBounds.clear();
    sd->methods = std::move(methods);
    sd->constructors = std::move(constructors);
    sd->destructor = std::move(destructor);
    sd->baseClasses = std::move(baseClasses);
    sd->cfg = templateStruct.cfg;
    return sd;
}

std::unique_ptr<ast::EnumDecl> Monomorphizer::monomorphizeEnum(
    const ast::EnumDecl& templateEnum,
    const std::string& specializedName,
    const TypeSubstMap& subst) {

    std::vector<EnumVariant> variants;
    for (const auto& v : templateEnum.variants) {
        std::vector<TypePtr> payloads;
        for (const auto& p : v.payloadTypes) {
            payloads.push_back(substituteType(p.get(), subst));
        }
        variants.push_back(EnumVariant(v.name, std::move(payloads), v.loc));
    }

    auto ed = std::make_unique<EnumDecl>(
        specializedName, std::move(variants), templateEnum.loc, templateEnum.isPublic);
    ed->typeParams.clear();
    ed->cfg = templateEnum.cfg;
    return ed;
}

} // namespace tamarin::sema
