// ============================================================================
// ast.h - Tamarin Abstract Syntax Tree node definitions
// ============================================================================
#pragma once

#include "tamarin/source/source_location.h"
#include "tamarin/target/target_triple.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace tamarin::ast {

// ── Forward declarations ────────────────────────────────────────────────────
struct Expr;
struct Stmt;
struct TypeNode;

using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;
using TypePtr = std::unique_ptr<TypeNode>;

// ═══════════════════════════════════════════════════════════════════════════
//  TYPE NODES
// ═══════════════════════════════════════════════════════════════════════════

enum class PrimitiveKind {
  Void,
  Bool,
  Char,
  Byte,
  Short,
  Int,
  Long,
  UnsignedChar,
  UnsignedShort,
  UnsignedInt,
  UnsignedLong,
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
  Float4,
  Float8,
  Int4,
  Int8,
  Int16,
};

enum class TypeNodeKind {
  Primitive,
  Array,
  Named,
  Reference,
  Handle,
  Slab,
  Auto,
  Tuple,
  Function,
  Task
};

struct TypeNode {
  TypeNodeKind kind;
  SourceLocation loc;
  virtual ~TypeNode() = default;

protected:
  TypeNode(TypeNodeKind k, SourceLocation l) : kind(k), loc(l) {}
};

struct PrimitiveTypeNode : TypeNode {
  PrimitiveKind primKind;
  PrimitiveTypeNode(PrimitiveKind pk, SourceLocation l)
      : TypeNode(TypeNodeKind::Primitive, l), primKind(pk) {}
};

struct ArrayTypeNode : TypeNode {
  TypePtr elementType;
  ExprPtr sizeExpr; // nullptr = unsized (view in param, inferred in local)
  bool isConst = false;
  ArrayTypeNode(TypePtr elem, ExprPtr size, SourceLocation l, bool isC = false)
      : TypeNode(TypeNodeKind::Array, l), elementType(std::move(elem)),
        sizeExpr(std::move(size)), isConst(isC) {}
};

struct NamedTypeNode : TypeNode {
  std::string name;
  std::vector<TypePtr> typeArgs;
  NamedTypeNode(std::string n, SourceLocation l)
      : TypeNode(TypeNodeKind::Named, l), name(std::move(n)) {}
  NamedTypeNode(std::string n, std::vector<TypePtr> args, SourceLocation l)
      : TypeNode(TypeNodeKind::Named, l), name(std::move(n)),
        typeArgs(std::move(args)) {}
};

struct ReferenceTypeNode : TypeNode {
  TypePtr referentType;
  bool isConst = false;
  bool isPointer = false;
  ReferenceTypeNode(TypePtr ref, bool isC, SourceLocation l, bool isPtr = false)
      : TypeNode(TypeNodeKind::Reference, l), referentType(std::move(ref)),
        isConst(isC), isPointer(isPtr) {}
};

struct HandleTypeNode : TypeNode {
  TypePtr innerType;
  HandleTypeNode(TypePtr inner, SourceLocation l)
      : TypeNode(TypeNodeKind::Handle, l), innerType(std::move(inner)) {}
};

struct TaskTypeNode : TypeNode {
  TypePtr elementType;
  TaskTypeNode(TypePtr elem, SourceLocation l)
      : TypeNode(TypeNodeKind::Task, l), elementType(std::move(elem)) {}
};

struct SlabTypeNode : TypeNode {
  TypePtr elementType;
  ExprPtr capacityExpr; // nullptr for default / dynamic
  SlabTypeNode(TypePtr elem, ExprPtr cap, SourceLocation l)
      : TypeNode(TypeNodeKind::Slab, l), elementType(std::move(elem)),
        capacityExpr(std::move(cap)) {}
};

struct AutoTypeNode : TypeNode {
  bool isConst = false;
  AutoTypeNode(SourceLocation l, bool isC = false)
      : TypeNode(TypeNodeKind::Auto, l), isConst(isC) {}
};

struct TupleTypeNode : TypeNode {
  std::vector<TypePtr> elements;
  TupleTypeNode(std::vector<TypePtr> elems, SourceLocation l)
      : TypeNode(TypeNodeKind::Tuple, l), elements(std::move(elems)) {}
};

struct FunctionTypeNode : TypeNode {
  std::vector<TypePtr> paramTypes;
  TypePtr returnType;
  FunctionTypeNode(std::vector<TypePtr> params, TypePtr ret, SourceLocation l)
      : TypeNode(TypeNodeKind::Function, l), paramTypes(std::move(params)),
        returnType(std::move(ret)) {}
};

// ═══════════════════════════════════════════════════════════════════════════
//  EXPRESSIONS
// ═══════════════════════════════════════════════════════════════════════════

enum class ExprKind {
  IntLiteral,
  FloatLiteral,
  BoolLiteral,
  StringLiteral,
  Identifier,
  Binary,
  Unary,
  Assign,
  Call,
  MethodCall,
  Index,
  Field,
  Cast,
  ArrayInit,
  CompoundInit,
  This,
  Construct,
  Match,
  Try,
  Ternary,
  Tuple,
  TupleIndex,
  Lambda,
  Range,
  Await,
};

enum class BinaryOp {
  Add,
  Sub,
  Mul,
  Div,
  Mod,
  Lt,
  Gt,
  LtEq,
  GtEq,
  EqEq,
  NotEq,
  And,
  Or,
  BitAnd,
  BitOr,
  BitXor,
  Shl,
  Shr,
};

enum class UnaryOp {
  Neg,
  Not,
  BitNot,
  PreInc,
  PreDec,
  PostInc,
  PostDec,
  AddrOf,
  Deref,
};

enum class AssignOp {
  Assign,
  AddAssign,
  SubAssign,
  MulAssign,
  DivAssign,
  ModAssign,
  AndAssign,
  OrAssign,
  XorAssign,
  ShlAssign,
  ShrAssign,
};

struct Expr {
  ExprKind kind;
  SourceLocation loc;
  virtual ~Expr() = default;

protected:
  Expr(ExprKind k, SourceLocation l) : kind(k), loc(l) {}
};

struct IntLiteralExpr : Expr {
  int64_t value;
  IntLiteralExpr(int64_t v, SourceLocation l)
      : Expr(ExprKind::IntLiteral, l), value(v) {}
};

struct FloatLiteralExpr : Expr {
  double value;
  bool isSinglePrecision = false;
  FloatLiteralExpr(double v, SourceLocation l, bool isSingle = false)
      : Expr(ExprKind::FloatLiteral, l), value(v), isSinglePrecision(isSingle) {
  }
};

struct BoolLiteralExpr : Expr {
  bool value;
  BoolLiteralExpr(bool v, SourceLocation l)
      : Expr(ExprKind::BoolLiteral, l), value(v) {}
};

struct StringLiteralExpr : Expr {
  std::string value;
  StringLiteralExpr(std::string val, SourceLocation l)
      : Expr(ExprKind::StringLiteral, l), value(std::move(val)) {}
};

struct IdentifierExpr : Expr {
  std::string name;
  IdentifierExpr(std::string n, SourceLocation l)
      : Expr(ExprKind::Identifier, l), name(std::move(n)) {}
};

struct BinaryExpr : Expr {
  BinaryOp op;
  ExprPtr lhs, rhs;
  std::string overloadedMethod;
  BinaryExpr(BinaryOp o, ExprPtr l, ExprPtr r, SourceLocation loc)
      : Expr(ExprKind::Binary, loc), op(o), lhs(std::move(l)),
        rhs(std::move(r)) {}
};

struct UnaryExpr : Expr {
  UnaryOp op;
  ExprPtr operand;
  std::string overloadedMethod;
  UnaryExpr(UnaryOp o, ExprPtr e, SourceLocation l)
      : Expr(ExprKind::Unary, l), op(o), operand(std::move(e)) {}
};

struct AssignExpr : Expr {
  AssignOp op;
  ExprPtr target, value;
  AssignExpr(AssignOp o, ExprPtr t, ExprPtr v, SourceLocation l)
      : Expr(ExprKind::Assign, l), op(o), target(std::move(t)),
        value(std::move(v)) {}
};

struct CallExpr : Expr {
  std::string callee;
  std::vector<TypePtr> typeArgs;
  std::vector<ExprPtr> args;
  ExprPtr calleeExpr = nullptr;
  CallExpr(std::string name, std::vector<ExprPtr> a, SourceLocation l)
      : Expr(ExprKind::Call, l), callee(std::move(name)), args(std::move(a)) {}
  CallExpr(std::string name, std::vector<TypePtr> tArgs, std::vector<ExprPtr> a,
           SourceLocation l)
      : Expr(ExprKind::Call, l), callee(std::move(name)),
        typeArgs(std::move(tArgs)), args(std::move(a)) {}
  CallExpr(ExprPtr fn, std::vector<ExprPtr> a, SourceLocation l)
      : Expr(ExprKind::Call, l), callee("<indirect>"), args(std::move(a)),
        calleeExpr(std::move(fn)) {}
};

struct MethodCallExpr : Expr {
  ExprPtr object;
  std::string methodName;
  std::vector<TypePtr> typeArgs;
  std::vector<ExprPtr> args;
  bool isArrow; // -> vs .
  MethodCallExpr(ExprPtr obj, std::string method, std::vector<ExprPtr> a,
                 bool arrow, SourceLocation l)
      : Expr(ExprKind::MethodCall, l), object(std::move(obj)),
        methodName(std::move(method)), args(std::move(a)), isArrow(arrow) {}
  MethodCallExpr(ExprPtr obj, std::string method, std::vector<TypePtr> tArgs,
                 std::vector<ExprPtr> a, bool arrow, SourceLocation l)
      : Expr(ExprKind::MethodCall, l), object(std::move(obj)),
        methodName(std::move(method)), typeArgs(std::move(tArgs)),
        args(std::move(a)), isArrow(arrow) {}
};

struct IndexExpr : Expr {
  ExprPtr array;
  ExprPtr index;
  std::string overloadedMethod;
  IndexExpr(ExprPtr arr, ExprPtr idx, SourceLocation l)
      : Expr(ExprKind::Index, l), array(std::move(arr)), index(std::move(idx)) {
  }
};

struct FieldExpr : Expr {
  ExprPtr object;
  std::string fieldName;
  bool isArrow; // -> vs .
  FieldExpr(ExprPtr obj, std::string field, bool arrow, SourceLocation l)
      : Expr(ExprKind::Field, l), object(std::move(obj)),
        fieldName(std::move(field)), isArrow(arrow) {}
};

struct CastExpr : Expr {
  TypePtr targetType;
  ExprPtr operand;
  CastExpr(TypePtr ty, ExprPtr e, SourceLocation l)
      : Expr(ExprKind::Cast, l), targetType(std::move(ty)),
        operand(std::move(e)) {}
};

struct ArrayInitExpr : Expr {
  std::vector<ExprPtr> elements;
  ArrayInitExpr(std::vector<ExprPtr> elems, SourceLocation l)
      : Expr(ExprKind::ArrayInit, l), elements(std::move(elems)) {}
};

struct StructInitField {
  std::string fieldName; // empty if positional
  ExprPtr value;
  SourceLocation loc;
};

struct CompoundInitExpr : Expr {
  TypePtr type; // optional, e.g. (Vec2){ ... } or nullptr for bare { ... }
  std::vector<StructInitField> fields;
  CompoundInitExpr(TypePtr ty, std::vector<StructInitField> f, SourceLocation l)
      : Expr(ExprKind::CompoundInit, l), type(std::move(ty)),
        fields(std::move(f)) {}
};

struct ThisExpr : Expr {
  ThisExpr(SourceLocation l) : Expr(ExprKind::This, l) {}
};

struct ConstructExpr : Expr {
  std::string typeName;
  std::vector<ExprPtr> args;
  ConstructExpr(std::string name, std::vector<ExprPtr> a, SourceLocation l)
      : Expr(ExprKind::Construct, l), typeName(std::move(name)),
        args(std::move(a)) {}
};

struct TupleExpr : Expr {
  std::vector<ExprPtr> elements;
  TupleExpr(std::vector<ExprPtr> elems, SourceLocation l)
      : Expr(ExprKind::Tuple, l), elements(std::move(elems)) {}
};

struct TupleIndexExpr : Expr {
  ExprPtr tuple;
  size_t index;
  TupleIndexExpr(ExprPtr t, size_t idx, SourceLocation l)
      : Expr(ExprKind::TupleIndex, l), tuple(std::move(t)), index(idx) {}
};

// ═══════════════════════════════════════════════════════════════════════════
//  STATEMENTS
// ═══════════════════════════════════════════════════════════════════════════

enum class StmtKind {
  VarDecl,
  Expr,
  Return,
  Block,
  If,
  While,
  For,
  Break,
  Continue,
  Switch,
  Region,
  Match,
  ParallelFor,
  TaskScope,
  Spawn,
  Select,
  DestructureVarDecl,
};

struct Stmt {
  StmtKind kind;
  SourceLocation loc;
  virtual ~Stmt() = default;

protected:
  Stmt(StmtKind k, SourceLocation l) : kind(k), loc(l) {}
};

struct VarDeclStmt : Stmt {
  TypePtr type;
  std::string name;
  ExprPtr initializer; // may be nullptr
  bool isConst;
  bool isArray;                    // true if declared with [] suffix
  ExprPtr arraySize;               // for fixed [N], nullptr for inferred []
  int64_t evaluatedArraySize = -1; // compile-time evaluated constant size
  VarDeclStmt(TypePtr ty, std::string n, ExprPtr init, bool cnst, bool arr,
              ExprPtr arrSize, SourceLocation l)
      : Stmt(StmtKind::VarDecl, l), type(std::move(ty)), name(std::move(n)),
        initializer(std::move(init)), isConst(cnst), isArray(arr),
        arraySize(std::move(arrSize)), evaluatedArraySize(-1) {}
};

struct DestructureVarDeclStmt : Stmt {
  std::vector<TypePtr> types;
  std::vector<std::string> names;
  ExprPtr initializer;
  bool isConst;
  DestructureVarDeclStmt(std::vector<TypePtr> tys, std::vector<std::string> nms,
                         ExprPtr init, bool cnst, SourceLocation l)
      : Stmt(StmtKind::DestructureVarDecl, l), types(std::move(tys)),
        names(std::move(nms)), initializer(std::move(init)), isConst(cnst) {}
  DestructureVarDeclStmt(std::vector<std::string> nms, ExprPtr init, bool cnst,
                         SourceLocation l)
      : Stmt(StmtKind::DestructureVarDecl, l), types(), names(std::move(nms)),
        initializer(std::move(init)), isConst(cnst) {}
};

struct ExprStmt : Stmt {
  ExprPtr expr;
  ExprStmt(ExprPtr e, SourceLocation l)
      : Stmt(StmtKind::Expr, l), expr(std::move(e)) {}
};

struct ReturnStmt : Stmt {
  ExprPtr value; // may be nullptr
  ReturnStmt(ExprPtr v, SourceLocation l)
      : Stmt(StmtKind::Return, l), value(std::move(v)) {}
};

struct BlockStmt : Stmt {
  std::vector<StmtPtr> stmts;
  BlockStmt(std::vector<StmtPtr> s, SourceLocation l)
      : Stmt(StmtKind::Block, l), stmts(std::move(s)) {}
};

struct IfStmt : Stmt {
  ExprPtr condition;
  StmtPtr thenBlock;
  StmtPtr elseBlock; // may be nullptr
  IfStmt(ExprPtr cond, StmtPtr then, StmtPtr els, SourceLocation l)
      : Stmt(StmtKind::If, l), condition(std::move(cond)),
        thenBlock(std::move(then)), elseBlock(std::move(els)) {}
};

struct WhileStmt : Stmt {
  ExprPtr condition;
  StmtPtr body;
  int unrollCount = 0;
  int vectorizeWidth = 0;
  int interleaveCount = 0;
  int vectorizeEnable = 0;
  WhileStmt(ExprPtr cond, StmtPtr b, SourceLocation l, int unroll = 0)
      : Stmt(StmtKind::While, l), condition(std::move(cond)),
        body(std::move(b)), unrollCount(unroll) {}
};

struct ForStmt : Stmt {
  StmtPtr init;      // VarDeclStmt or ExprStmt, or nullptr
  ExprPtr condition; // may be nullptr
  ExprPtr increment; // may be nullptr
  StmtPtr body;
  int unrollCount = 0;
  int vectorizeWidth = 0;
  int interleaveCount = 0;
  int vectorizeEnable = 0;
  ForStmt(StmtPtr i, ExprPtr c, ExprPtr inc, StmtPtr b, SourceLocation l,
          int unroll = 0)
      : Stmt(StmtKind::For, l), init(std::move(i)), condition(std::move(c)),
        increment(std::move(inc)), body(std::move(b)), unrollCount(unroll) {}
};

struct BreakStmt : Stmt {
  BreakStmt(SourceLocation l) : Stmt(StmtKind::Break, l) {}
};

struct ContinueStmt : Stmt {
  ContinueStmt(SourceLocation l) : Stmt(StmtKind::Continue, l) {}
};

struct CaseClause {
  ExprPtr value; // nullptr for default
  std::vector<StmtPtr> body;
};

struct SwitchStmt : Stmt {
  ExprPtr condition;
  std::vector<CaseClause> cases;
  SwitchStmt(ExprPtr cond, std::vector<CaseClause> c, SourceLocation l)
      : Stmt(StmtKind::Switch, l), condition(std::move(cond)),
        cases(std::move(c)) {}
};

struct RegionStmt : Stmt {
  std::string name;
  ExprPtr capacityExpr; // nullptr for default capacity (65536)
  std::unique_ptr<BlockStmt> body;
  RegionStmt(std::string n, ExprPtr cap, std::unique_ptr<BlockStmt> b,
             SourceLocation l)
      : Stmt(StmtKind::Region, l), name(std::move(n)),
        capacityExpr(std::move(cap)), body(std::move(b)) {}
};

enum class ReductionOp {
  Add,    // +
  Mul,    // *
  Min,    // min
  Max,    // max
  BitAnd, // &
  BitOr,  // |
  BitXor  // ^
};

struct ReductionClause {
  ReductionOp op = ReductionOp::Add;
  std::string varName;
  SourceLocation loc;
};

struct ParallelForStmt : Stmt {
  StmtPtr init;      // VarDeclStmt or ExprStmt
  ExprPtr condition; // induction boundary condition
  ExprPtr increment; // step increment
  StmtPtr body;      // loop body
  ExprPtr grainSize; // optional grain size expression, nullptr for auto
  std::vector<ReductionClause> reductions;
  int unrollCount = 0;
  int vectorizeWidth = 0;
  int interleaveCount = 0;
  int vectorizeEnable = 0;

  ParallelForStmt(StmtPtr i, ExprPtr c, ExprPtr inc, StmtPtr b, ExprPtr grain,
                  std::vector<ReductionClause> reds, SourceLocation l)
      : Stmt(StmtKind::ParallelFor, l), init(std::move(i)),
        condition(std::move(c)), increment(std::move(inc)), body(std::move(b)),
        grainSize(std::move(grain)), reductions(std::move(reds)) {}

  ParallelForStmt(StmtPtr i, ExprPtr c, ExprPtr inc, StmtPtr b, ExprPtr grain,
                  SourceLocation l)
      : Stmt(StmtKind::ParallelFor, l), init(std::move(i)),
        condition(std::move(c)), increment(std::move(inc)), body(std::move(b)),
        grainSize(std::move(grain)), reductions() {}
};

struct TaskScopeStmt : Stmt {
  std::unique_ptr<BlockStmt> body;
  TaskScopeStmt(std::unique_ptr<BlockStmt> b, SourceLocation l)
      : Stmt(StmtKind::TaskScope, l), body(std::move(b)) {}
};

struct SpawnStmt : Stmt {
  StmtPtr body; // CallExpr, ExprStmt, or BlockStmt
  SpawnStmt(StmtPtr b, SourceLocation l)
      : Stmt(StmtKind::Spawn, l), body(std::move(b)) {}
};

struct SelectCase {
  ExprPtr condition; // condition expression, e.g. ch.try_recv(val)
  std::vector<StmtPtr> body;
  SourceLocation loc;
};

struct SelectStmt : Stmt {
  std::vector<SelectCase> cases;
  std::vector<StmtPtr> defaultBody;
  bool hasDefault = false;
  SelectStmt(std::vector<SelectCase> c, std::vector<StmtPtr> def, bool hasDef,
             SourceLocation l)
      : Stmt(StmtKind::Select, l), cases(std::move(c)),
        defaultBody(std::move(def)), hasDefault(hasDef) {}
};

enum class MatchPatternKind {
  Wildcard, // _
  Literal,  // 42, 3.14, true, "hello"
  Variant,  // Option::Some(x) or Color::Red
  Binding,  // x
};

struct MatchPattern {
  MatchPatternKind kind = MatchPatternKind::Wildcard;
  SourceLocation loc;
  std::string enumName;    // e.g. "Option" or "Shape"
  std::string variantName; // e.g. "Some" or "Circle"
  std::vector<std::string>
      bindings;        // bound variables, e.g. ["x"] or ["w", "h"]
  ExprPtr literalExpr; // for Literal pattern
};

struct MatchCase {
  MatchPattern pattern;
  ExprPtr guard; // optional: if (cond)
  StmtPtr body;  // statement/block body or expression statement
  SourceLocation loc;
};

struct MatchStmt : Stmt {
  ExprPtr expr;
  std::vector<MatchCase> cases;
  MatchStmt(ExprPtr e, std::vector<MatchCase> c, SourceLocation l)
      : Stmt(StmtKind::Match, l), expr(std::move(e)), cases(std::move(c)) {}
};

struct MatchExpr : Expr {
  ExprPtr expr;
  std::vector<MatchCase> cases;
  MatchExpr(ExprPtr e, std::vector<MatchCase> c, SourceLocation l)
      : Expr(ExprKind::Match, l), expr(std::move(e)), cases(std::move(c)) {}
};

struct TryExpr : Expr {
  ExprPtr operand;
  TryExpr(ExprPtr op, SourceLocation l)
      : Expr(ExprKind::Try, l), operand(std::move(op)) {}
};

struct TernaryExpr : Expr {
  ExprPtr condition;
  ExprPtr thenExpr;
  ExprPtr elseExpr;
  TernaryExpr(ExprPtr cond, ExprPtr thenE, ExprPtr elseE, SourceLocation l)
      : Expr(ExprKind::Ternary, l), condition(std::move(cond)),
        thenExpr(std::move(thenE)), elseExpr(std::move(elseE)) {}
};

// ═══════════════════════════════════════════════════════════════════════════
//  TOP-LEVEL DECLARATIONS
// ═══════════════════════════════════════════════════════════════════════════

enum class DeclKind { Function, Struct, Enum, Const, Var, Trait, Impl };

struct ParamDecl {
  TypePtr type;
  std::string name;
  bool isConst;
  bool isArray;      // declared with []
  ExprPtr arraySize; // for [N], nullptr for []
  SourceLocation loc;
  ExprPtr defaultValue = nullptr;
};

struct LambdaExpr : Expr {
  std::vector<ParamDecl> params;
  TypePtr returnType;                   // optional, may be nullptr if inferred
  ExprPtr exprBody;                     // if => expr
  std::unique_ptr<BlockStmt> blockBody; // if => { ... }
  std::vector<std::string> captures;
  LambdaExpr(std::vector<ParamDecl> p, TypePtr ret, ExprPtr eBody,
             std::unique_ptr<BlockStmt> bBody, SourceLocation l)
      : Expr(ExprKind::Lambda, l), params(std::move(p)),
        returnType(std::move(ret)), exprBody(std::move(eBody)),
        blockBody(std::move(bBody)) {}
};

struct RangeExpr : Expr {
  ExprPtr start; // optional: nullptr if `..end`
  ExprPtr end;   // optional: nullptr if `start..`
  RangeExpr(ExprPtr s, ExprPtr e, SourceLocation l)
      : Expr(ExprKind::Range, l), start(std::move(s)), end(std::move(e)) {}
};

struct AwaitExpr : Expr {
  ExprPtr operand;
  AwaitExpr(ExprPtr op, SourceLocation l)
      : Expr(ExprKind::Await, l), operand(std::move(op)) {}
};

// ═══════════════════════════════════════════════════════════════════════════
//  CONDITIONAL COMPILATION (@cfg)
// ═══════════════════════════════════════════════════════════════════════════

enum class CfgOp {
  TargetOS,
  TargetArch,
  All,
  Any,
  Not
};

struct CfgExpr {
  CfgOp op;
  std::string value; // e.g. "windows", "linux", "macos", "x86_64", "aarch64"
  std::vector<std::shared_ptr<CfgExpr>> children;
  SourceLocation loc;

  CfgExpr(CfgOp o, std::string v, std::vector<std::shared_ptr<CfgExpr>> ch, SourceLocation l)
      : op(o), value(std::move(v)), children(std::move(ch)), loc(l) {}

  bool evaluate(const target::TargetTriple& triple) const {
    switch (op) {
      case CfgOp::TargetOS: {
        if (value == "windows") return triple.isWindows();
        if (value == "linux") return triple.isLinux();
        if (value == "macos" || value == "darwin") return triple.isDarwin();
        return false;
      }
      case CfgOp::TargetArch: {
        if (value == "x86_64" || value == "amd64" || value == "x64") return triple.isX86_64();
        if (value == "aarch64" || value == "arm64") return triple.isAArch64();
        return false;
      }
      case CfgOp::All: {
        for (const auto& child : children) {
          if (!child || !child->evaluate(triple)) return false;
        }
        return true;
      }
      case CfgOp::Any: {
        for (const auto& child : children) {
          if (child && child->evaluate(triple)) return true;
        }
        return false;
      }
      case CfgOp::Not: {
        if (children.empty() || !children[0]) return true;
        return !children[0]->evaluate(triple);
      }
    }
    return false;
  }
};

struct ImportDecl {
  std::string modulePath; // e.g. "math.vector"
  std::string alias;      // e.g. "vector" or "vec"
  SourceLocation loc;
  std::shared_ptr<CfgExpr> cfg = nullptr;
};

struct Decl {
  DeclKind kind;
  SourceLocation loc;
  bool isPublic = false;
  std::shared_ptr<CfgExpr> cfg = nullptr;
  virtual ~Decl() = default;

protected:
  Decl(DeclKind k, SourceLocation l, bool pub = false, std::shared_ptr<CfgExpr> c = nullptr)
      : kind(k), loc(l), isPublic(pub), cfg(std::move(c)) {}
};

using DeclPtr = std::unique_ptr<Decl>;

struct FunctionDecl : Decl {
  TypePtr returnType;
  std::string name;
  std::vector<std::string> typeParams;
  std::vector<std::pair<std::string, std::string>> traitBounds;
  std::vector<ParamDecl> params;
  std::unique_ptr<BlockStmt> body;
  bool isNoInline = false;
  bool isAlwaysInline = false;
  bool isExtern = false;
  std::string linkage = "";
  bool isExported = false;
  bool isAsync = false;
  FunctionDecl(TypePtr ret, std::string n, std::vector<ParamDecl> p,
               std::unique_ptr<BlockStmt> b, SourceLocation l, bool pub = false,
               bool noInline = false, bool alwaysInline = false)
      : Decl(DeclKind::Function, l, pub), returnType(std::move(ret)),
        name(std::move(n)), params(std::move(p)), body(std::move(b)),
        isNoInline(noInline), isAlwaysInline(alwaysInline) {}
  FunctionDecl(TypePtr ret, std::string n, std::vector<std::string> tParams,
               std::vector<std::pair<std::string, std::string>> bounds,
               std::vector<ParamDecl> p, std::unique_ptr<BlockStmt> b,
               SourceLocation l, bool pub = false, bool noInline = false,
               bool alwaysInline = false)
      : Decl(DeclKind::Function, l, pub), returnType(std::move(ret)),
        name(std::move(n)), typeParams(std::move(tParams)),
        traitBounds(std::move(bounds)), params(std::move(p)),
        body(std::move(b)), isNoInline(noInline), isAlwaysInline(alwaysInline) {
  }
};

enum class LayoutDirective {
  Default, // default AoS / point layout
  Point,   // @layout(point)
  Scan,    // @layout(scan)
  Grouped, // @layout(grouped)
  CLayout, // @c_layout
};

enum class AccessSpecifier {
  Public,
  Private,
  Protected,
};

struct StructField {
  TypePtr type;
  std::string name;
  bool isConst = false;
  bool isCold = false;
  AccessSpecifier access = AccessSpecifier::Public;
  SourceLocation loc;
};

struct MethodDecl {
  TypePtr returnType;
  std::string name;
  std::vector<ParamDecl> params;
  std::unique_ptr<BlockStmt> body; // nullptr if pure virtual (= 0)
  bool isConst = false;
  bool isVirtual = false;
  bool isOverride = false;
  bool isPureVirtual = false;
  bool isOperator = false;
  AccessSpecifier access = AccessSpecifier::Public;
  SourceLocation loc;
  bool isAsync = false;
};

struct ConstructorDecl {
  std::string name;
  std::vector<ParamDecl> params;
  std::unique_ptr<BlockStmt> body;
  AccessSpecifier access = AccessSpecifier::Public;
  SourceLocation loc;
};

struct DestructorDecl {
  std::string name;
  std::unique_ptr<BlockStmt> body;
  bool isVirtual = false;
  AccessSpecifier access = AccessSpecifier::Public;
  SourceLocation loc;
};

struct BaseClassSpec {
  std::string name;
  AccessSpecifier access = AccessSpecifier::Public;
  SourceLocation loc;
};

struct StructDecl : Decl {
  std::string name;
  std::vector<std::string> typeParams;
  std::vector<std::pair<std::string, std::string>> traitBounds;
  std::vector<StructField> fields;
  std::vector<MethodDecl> methods;
  std::vector<ConstructorDecl> constructors;
  std::unique_ptr<DestructorDecl> destructor;
  std::vector<BaseClassSpec> baseClasses;
  LayoutDirective layout = LayoutDirective::Default;
  bool isClass = false;
  bool isInterface = false;

  StructDecl(std::string n, std::vector<StructField> f, LayoutDirective lay,
             SourceLocation l, bool isCls = false, bool isIf = false,
             bool pub = false)
      : Decl(DeclKind::Struct, l, pub), name(std::move(n)),
        fields(std::move(f)), layout(lay), isClass(isCls), isInterface(isIf) {}
  StructDecl(std::string n, std::vector<std::string> tParams,
             std::vector<std::pair<std::string, std::string>> bounds,
             std::vector<StructField> f, LayoutDirective lay, SourceLocation l,
             bool isCls = false, bool isIf = false, bool pub = false)
      : Decl(DeclKind::Struct, l, pub), name(std::move(n)),
        typeParams(std::move(tParams)), traitBounds(std::move(bounds)),
        fields(std::move(f)), layout(lay), isClass(isCls), isInterface(isIf) {}
};

struct TraitMethod {
  TypePtr returnType;
  std::string name;
  std::vector<ParamDecl> params;
  std::unique_ptr<BlockStmt> defaultBody;
  SourceLocation loc;
};

struct TraitDecl : Decl {
  std::string name;
  std::vector<std::string> typeParams;
  std::vector<TraitMethod> methods;
  TraitDecl(std::string n, std::vector<TraitMethod> m, SourceLocation l,
            bool pub = false)
      : Decl(DeclKind::Trait, l, pub), name(std::move(n)),
        methods(std::move(m)) {}
  TraitDecl(std::string n, std::vector<std::string> tParams,
            std::vector<TraitMethod> m, SourceLocation l, bool pub = false)
      : Decl(DeclKind::Trait, l, pub), name(std::move(n)),
        typeParams(std::move(tParams)), methods(std::move(m)) {}
};

struct ImplDecl : Decl {
  std::string traitName;
  TypePtr targetType;
  std::vector<std::string> typeParams;
  std::vector<MethodDecl> methods;
  ImplDecl(std::string trName, TypePtr target, std::vector<MethodDecl> m,
           SourceLocation l)
      : Decl(DeclKind::Impl, l, false), traitName(std::move(trName)),
        targetType(std::move(target)), methods(std::move(m)) {}
  ImplDecl(std::string trName, TypePtr target, std::vector<std::string> tParams,
           std::vector<MethodDecl> m, SourceLocation l)
      : Decl(DeclKind::Impl, l, false), traitName(std::move(trName)),
        targetType(std::move(target)), typeParams(std::move(tParams)),
        methods(std::move(m)) {}
};

struct EnumVariant {
  std::string name;
  std::vector<TypePtr> payloadTypes; // empty if unit variant
  SourceLocation loc;
  EnumVariant() = default;
  EnumVariant(std::string n, SourceLocation l) : name(std::move(n)), loc(l) {}
  EnumVariant(std::string n, std::vector<TypePtr> payloads, SourceLocation l)
      : name(std::move(n)), payloadTypes(std::move(payloads)), loc(l) {}
};

struct EnumDecl : Decl {
  std::string name;
  std::vector<std::string> typeParams; // e.g. <T> or <T, E>
  std::vector<EnumVariant> variants;
  EnumDecl(std::string n, std::vector<EnumVariant> v, SourceLocation l,
           bool pub = false)
      : Decl(DeclKind::Enum, l, pub), name(std::move(n)),
        variants(std::move(v)) {}
  EnumDecl(std::string n, std::vector<std::string> params,
           std::vector<EnumVariant> v, SourceLocation l, bool pub = false)
      : Decl(DeclKind::Enum, l, pub), name(std::move(n)),
        typeParams(std::move(params)), variants(std::move(v)) {}
};

struct ConstDecl : Decl {
  TypePtr type;
  std::string name;
  ExprPtr value;
  ConstDecl(TypePtr ty, std::string n, ExprPtr v, SourceLocation l,
            bool pub = false)
      : Decl(DeclKind::Const, l, pub), type(std::move(ty)), name(std::move(n)),
        value(std::move(v)) {}
};

struct GlobalVarDecl : Decl {
  TypePtr type;
  std::string name;
  ExprPtr initializer;
  bool isConst = false;
  bool isArray = false;
  ExprPtr arraySize;               // nullptr for inferred or unsized
  int64_t evaluatedArraySize = -1; // compile-time evaluated constant size
  GlobalVarDecl(TypePtr ty, std::string n, ExprPtr init, bool cnst, bool arr,
                ExprPtr arrSize, SourceLocation l, bool pub = false)
      : Decl(DeclKind::Var, l, pub), type(std::move(ty)), name(std::move(n)),
        initializer(std::move(init)), isConst(cnst), isArray(arr),
        arraySize(std::move(arrSize)), evaluatedArraySize(-1) {}
};

// ── Program ─────────────────────────────────────────────────────────────────

struct Program {
  std::vector<ImportDecl> imports;
  std::vector<DeclPtr> decls;
};

} // namespace tamarin::ast
