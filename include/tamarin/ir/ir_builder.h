// ============================================================================
// ir_builder.h - Lowers typed AST to Tamarin Intermediate Representation
// ============================================================================
#pragma once

#include "tamarin/ast/ast.h"
#include "tamarin/ir/ir.h"
#include "tamarin/sema/type_checker.h"

#include <optional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace tamarin::driver {
    struct Module;
}

namespace tamarin::ir {

struct Binding {
    ValueId ptr = InvalidValue;
    const sema::Type* type = nullptr;
    std::optional<ValueId> arrayLenValueId;
    int64_t constantArrayLen = -1;
    bool isConst = false;
    bool isParameter = false;
};

struct LoopTarget {
    std::string breakLabel;
    std::string continueLabel;
    size_t scopeDepth = 0;
};

class IRBuilder {
public:
    explicit IRBuilder(sema::TypeChecker& checker);

    /// Lower a complete typed Program AST into an IRModule.
    std::unique_ptr<IRModule> buildModule(ast::Program& prog, const std::string& moduleName = "main");

    /// Lower a set of modules (topologically sorted) into a single unified IRModule.
    std::unique_ptr<IRModule> buildProgram(
        const std::vector<std::shared_ptr<driver::Module>>& modules,
        const std::string& programName = "main");

private:
    void lowerFunction(ast::FunctionDecl& fn);
    void lowerMethod(ast::StructDecl& sd, ast::MethodDecl& m);
    void lowerConstructor(ast::StructDecl& sd, ast::ConstructorDecl& c, size_t ctorIdx);
    void lowerDestructor(ast::StructDecl& sd, ast::DestructorDecl* d);
    void lowerStmt(ast::Stmt& stmt);
    void lowerBlock(ast::BlockStmt& block);
    void lowerVarDecl(ast::VarDeclStmt& decl);
    void lowerDestructureVarDecl(ast::DestructureVarDeclStmt& stmt);
    void lowerIf(ast::IfStmt& stmt);
    void lowerWhile(ast::WhileStmt& stmt);
    void lowerFor(ast::ForStmt& stmt);
    void lowerReturn(ast::ReturnStmt& stmt);
    void lowerRegion(ast::RegionStmt& stmt);
    void lowerMatchStmt(ast::MatchStmt& stmt);
    void lowerParallelFor(ast::ParallelForStmt& stmt);
    void lowerTaskScope(ast::TaskScopeStmt& stmt);
    void lowerSpawn(ast::SpawnStmt& stmt);
    void lowerSelect(ast::SelectStmt& stmt);

    ValueId lowerExpr(ast::Expr& expr);
    ValueId lowerMatchExpr(ast::MatchExpr& expr);
    ValueId lowerTryExpr(ast::TryExpr& expr);
    ValueId lowerTernaryExpr(ast::TernaryExpr& expr);
    ValueId lowerLambda(ast::LambdaExpr& expr);
    ValueId emitCastIfNeeded(ValueId val, const sema::Type* fromTy, const sema::Type* toTy);
    ValueId getStructPointer(ast::Expr& objExpr);
    void emitCleanupsUpTo(size_t targetScopeDepth);
    int64_t registerStringLiteral(const std::string& str);
    void ensureDestructor(const sema::Type* ty);
    void lowerEnumDestructor(const sema::Type* eTy);
    void markVariableMoved(const std::string& name);
    void markVariableReassigned(const std::string& name, ValueId ptr, const sema::Type* ty);

    // ── Object construction helpers ──────────────────────────────────
    bool needsDefaultConstruction(const sema::Type* type) const;
    void constructDefaultObject(ValueId objPtr, const sema::Type* type);
    void constructArrayElements(ValueId arrayBasePtr, const sema::Type* elemTy, int64_t startIndex, int64_t count);

    // ── Instruction emission helpers ──────────────────────────────────
    ValueId nextValueId();
    std::string nextLabel(const std::string& prefix = "bb");

    Instruction& emit(Opcode op, const sema::Type* type = nullptr);
    Instruction& emit(Instruction inst);

    void setInsertBlock(BasicBlock* block);
    bool isBlockTerminated(const BasicBlock* block) const noexcept;
    bool isCurrentBlockTerminated() const noexcept;

    // ── Scoped Binding Management ─────────────────────────────────────
    void pushScope();
    void popScope();
    void setBinding(const std::string& name, Binding binding);
    const Binding* lookupBinding(const std::string& name) const;
    ValueId getVar(const std::string& name);
    const sema::Type* getVarType(const std::string& name);
    ValueId emitIntConstant(int64_t val, const sema::Type* ty = nullptr);

    // ── State ─────────────────────────────────────────────────────────
    sema::TypeChecker& checker_;
    IRModule* currentModule_ = nullptr;
    IRFunction* currentFunction_ = nullptr;
    BasicBlock* currentBlock_ = nullptr;

    uint32_t nextValueId_ = 0;
    uint32_t nextLabelId_ = 0;

    // Scoped variable bindings stack
    std::vector<std::unordered_map<std::string, Binding>> scopeStack_;

    // RAII Cleanup Stack
    enum class CleanupKind { Destructor, RegionDestroy, SlabDestroy };
    struct CleanupItem {
        CleanupKind kind = CleanupKind::Destructor;
        ValueId ptr = InvalidValue;
        const sema::Type* type = nullptr;
        std::string varName;
        ValueId dropFlag = InvalidValue;
        bool isMoved = false;
    };
    std::vector<std::vector<CleanupItem>> cleanupScopes_;
    std::vector<ValueId> activeRegionSlots_;
    std::unordered_set<std::string> emittedEnumDestructors_;
    std::unordered_set<std::string> createdThunks_;
    std::unordered_map<std::string, ValueId> varDropFlags_;

    // Current method/this receiver context
    ValueId currentThisValue_ = InvalidValue;
    const sema::Type* currentRecord_ = nullptr;
    bool isCurrentMethodConst_ = false;

    // Loop target stack for break and continue
    std::vector<LoopTarget> loopStack_;

    // Structured concurrency task scope stack
    std::vector<ValueId> taskScopeStack_;

    // Async coroutine state
    bool isCurrentFunctionAsync_ = false;
    ValueId currentAsyncResultPtr_ = InvalidValue;
    const sema::Type* currentAsyncUnderlyingRetTy_ = nullptr;

    // Fallback dead instruction storage
    Instruction deadInst_;
};

} // namespace tamarin::ir
