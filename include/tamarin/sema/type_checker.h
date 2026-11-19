// ============================================================================
// type_checker.h - Type resolution, compatibility checking, and AST annotation
// ============================================================================
#pragma once

#include "tamarin/ast/ast.h"
#include "tamarin/sema/types.h"
#include "tamarin/sema/symbol_table.h"
#include "tamarin/sema/module_scope.h"
#include "tamarin/sema/provenance.h"
#include "tamarin/diagnostics/diagnostic_engine.h"

#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <vector>
#include <iosfwd>

namespace tamarin::sema {

struct MemoryReportEntry {
    std::string name;
    std::string typeName;
    int64_t size = 0;
    int64_t align = 0;
    std::string storageClass;
    std::string storageRegion = "-";
    std::string lifetime;
    std::string dtorKind;
    bool containsReferencesOrViews = false;
    bool hasExternalBorrow = false;
    bool hasNonTrivialDestructor = false;
    bool requiresSidecar = false;
    bool containsBorrows = false;
    int64_t payloadContribution = 0;
};

class TypeChecker {
public:
    TypeChecker(TypeContext& tc, SymbolTable& syms, DiagnosticEngine& diag);

    /// Resolve an AST TypeNode into a sema::Type.
    const Type* resolveType(const ast::TypeNode& node);

    /// Resolve a named type (e.g. "Point" or "vector::Vec2").
    const Type* resolveNamedType(const std::string& name, SourceLocation loc);

    /// Look up a symbol (qualified or unqualified) with visibility checks.
    const Symbol* findSymbol(const std::string& name, SourceLocation loc);

    /// Check and type an expression. Returns the resolved type, or nullptr on error.
    const Type* checkExpr(ast::Expr& expr);

    /// Check a statement.
    void checkStmt(ast::Stmt& stmt, const Type* currentFuncReturnType);

    /// Get the resolved type for an expression node.
    [[nodiscard]] const Type* getExprType(const ast::Expr* expr) const;

    /// Scoped definite assignment tracking
    void pushScope();
    void popScope();
    void markAssigned(const std::string& name);
    bool isAssigned(const std::string& name) const;
    void markMoved(const std::string& name);
    bool isMoved(const std::string& name) const;
    bool checkBorrowMove(const std::string& targetOrRoot, SourceLocation loc);
    static bool isRValue(const ast::Expr* expr);
    const std::unordered_set<std::string>& getConditionallyMovedVars() const noexcept { return conditionallyMovedVars_; }
    void markFieldAssigned(const std::string& varName, const std::string& fieldName);
    void markAllFieldsAssigned(const std::string& varName);
    bool isFieldUninitialized(const std::string& varName, const std::string& fieldName) const;
    void markArrayElementAssigned(const std::string& arrName, int64_t idx);
    void markAllArrayElementsAssigned(const std::string& arrName);
    bool isArrayElementUninitialized(const std::string& arrName, int64_t idx) const;
    bool isArrayPartiallyInitialized(const std::string& arrName, int64_t totalSize) const;

    /// Current module scope for cross-module name resolution
    void setCurrentModuleScope(const ModuleScope* scope) { currentModuleScope_ = scope; }
    const ModuleScope* getCurrentModuleScope() const noexcept { return currentModuleScope_; }

    void registerModuleScope(const std::string& logicalName, const ModuleScope* scope) {
        moduleScopesByName_[logicalName] = scope;
    }
    const ModuleScope* getModuleScope(const std::string& logicalName) const {
        auto it = moduleScopesByName_.find(logicalName);
        return it != moduleScopesByName_.end() ? it->second : nullptr;
    }

    void setVarDeclType(const ast::VarDeclStmt* decl, const Type* type) {
        varDeclTypes_[decl] = type;
    }
    const Type* getVarDeclType(const ast::VarDeclStmt* decl) const {
        auto it = varDeclTypes_.find(decl);
        return it != varDeclTypes_.end() ? it->second : nullptr;
    }

    const std::vector<const Type*>* getDestructureTypes(const ast::DestructureVarDeclStmt* stmt) const {
        auto it = destructureTypes_.find(stmt);
        return it != destructureTypes_.end() ? &it->second : nullptr;
    }

    struct LambdaCapture {
        std::string name;
        const Type* type = nullptr;
        bool isMutable = false;
        size_t scopeDepth = 0;
        SourceLocation loc;
    };
    [[nodiscard]] const std::vector<LambdaCapture>* getLambdaCaptures(const ast::LambdaExpr* expr) const {
        auto it = lambdaCaptures_.find(expr);
        return it != lambdaCaptures_.end() ? &it->second : nullptr;
    }

    /// C ABI validation for functions and structs
    void checkCAbiType(const Type* ty, SourceLocation loc);
    void checkFunctionCAbi(const ast::FunctionDecl& fn);
    void checkStructCAbi(const ast::StructDecl& sd);

    /// Current method context for OOP checks
    void setCurrentRecord(const Type* rec, bool isConstMethod = false) {
        currentRecord_ = rec;
        isCurrentMethodConst_ = isConstMethod;
    }
    const Type* getCurrentRecord() const noexcept { return currentRecord_; }
    bool isCurrentMethodConst() const noexcept { return isCurrentMethodConst_; }

    /// Get the type context.
    TypeContext& context() noexcept { return tc_; }

    /// Get the symbol table.
    SymbolTable& symbols() noexcept { return syms_; }
    const SymbolTable& symbols() const noexcept { return syms_; }

    /// Register/lookup global function signatures for cross-module IR lowering
    void registerGlobalFunction(const std::string& name, const Type* fnTy) {
        globalFunctions_[name] = fnTy;
    }
    const Type* findGlobalFunction(const std::string& name) const {
        auto it = globalFunctions_.find(name);
        return it != globalFunctions_.end() ? it->second : nullptr;
    }

    /// Active AST Program for attaching monomorphized declarations
    void setActiveProgram(ast::Program* prog) { activeProgram_ = prog; }
    ast::Program* getActiveProgram() const noexcept { return activeProgram_; }
    void flushMonomorphizedDecls(ast::Program& prog);

    /// Template and trait registrations
    void registerGenericFunction(const std::string& name, const ast::FunctionDecl* decl);
    const ast::FunctionDecl* findGenericFunction(const std::string& name) const;

    void registerGenericStruct(const std::string& name, const ast::StructDecl* decl);
    const ast::StructDecl* findGenericStruct(const std::string& name) const;

    void registerGenericEnum(const std::string& name, const ast::EnumDecl* decl);
    const ast::EnumDecl* findGenericEnum(const std::string& name) const;

    void registerTrait(const std::string& name, const ast::TraitDecl* decl);
    const ast::TraitDecl* findTrait(const std::string& name) const;

    void registerTraitImpl(const std::string& targetTypeName, const std::string& traitName);
    bool typeImplementsTrait(const std::string& targetTypeName, const std::string& traitName) const;

    /// Instantiation / Monomorphization triggers
    const Type* instantiateGenericStruct(
        const ast::StructDecl& tmpl,
        const std::vector<const Type*>& typeArgs,
        SourceLocation loc);

    const Type* instantiateGenericFunction(
        const ast::FunctionDecl& tmpl,
        const std::vector<const Type*>& typeArgs,
        SourceLocation loc,
        std::string& outMangledName);

    const Type* instantiateGenericEnum(
        const ast::EnumDecl& tmpl,
        const std::vector<const Type*>& typeArgs,
        SourceLocation loc);

    /// Memory report collection and formatting
    void addMemoryReportEntry(MemoryReportEntry entry) { memoryReport_.push_back(std::move(entry)); }
    [[nodiscard]] const std::vector<MemoryReportEntry>& memoryReport() const noexcept { return memoryReport_; }
    void printMemoryReport(std::ostream& os) const;

    /// Region tracking & lifetime hierarchy
    uint64_t currentRegionId() const { return activeRegionIds_.empty() ? 0 : activeRegionIds_.back(); }
    uint64_t getExprRegionProvenance(const ast::Expr* expr) const;
    void setExprRegionProvenance(const ast::Expr* expr, uint64_t regionId);

    ProvenanceSet getExprProvenance(const ast::Expr* expr) const;
    void setExprProvenance(const ast::Expr* expr, ProvenanceSet prov);

    void setVarProvenance(const std::string& name, ProvenanceSet prov);
    void setVarProvenance(uint64_t bindingId, ProvenanceSet prov);
    ProvenanceSet getVarProvenance(const std::string& name) const;
    ProvenanceSet getVarProvenance(uint64_t bindingId) const;

    void setFieldProvenance(uint64_t bindingId, const std::string& fieldPath, ProvenanceSet prov);
    void setFieldProvenance(const std::string& varName, const std::string& fieldName, ProvenanceSet prov);
    ProvenanceSet getFieldProvenance(uint64_t bindingId, const std::string& fieldPath) const;
    ProvenanceSet getFieldProvenance(const std::string& varName, const std::string& fieldName) const;

    struct LValuePath {
        const Symbol* rootSymbol = nullptr;
        uint64_t bindingId = 0;
        std::string path;
        bool isValid = false;
        bool isGlobal = false;
    };
    LValuePath resolveLValuePath(const ast::Expr* expr) const;

    bool isRegionAncestor(uint64_t ancestor, uint64_t descendant) const;
    bool doesRegionOutlive(uint64_t srcRegion, uint64_t destRegion) const;

    /// Function and Method Effect Summaries
    void registerFunctionSummary(const std::string& name, FunctionBorrowSummary summary);
    const FunctionBorrowSummary* getFunctionSummary(const std::string& name) const;
    void setCurrentFunction(const std::string& name) { currentFunctionName_ = name; }
    const std::string& currentFunction() const noexcept { return currentFunctionName_; }

    /// Constant Expression Evaluation
    struct ConstEvalResult {
        bool isConst = false;
        bool overflow = false;
        bool divByZero = false;
        int64_t value = 0;
    };
    ConstEvalResult evalIntConstExpr(const ast::Expr& expr) const;

    /// Lifetime validation helper
    bool canAssignProvenance(const ProvenanceSet& srcProv, uint64_t destRegionId,
                             bool isDestGlobal, std::string& outReason) const;

private:
    const Type* checkIntLiteral(ast::IntLiteralExpr& expr);
    const Type* checkFloatLiteral(ast::FloatLiteralExpr& expr);
    const Type* checkBoolLiteral(ast::BoolLiteralExpr& expr);
    const Type* checkStringLiteral(ast::StringLiteralExpr& expr);
    const Type* checkIdentifier(ast::IdentifierExpr& expr);
    const Type* checkBinary(ast::BinaryExpr& expr);
    const Type* checkBinaryOpTypes(ast::BinaryExpr& expr, const Type* lhsTy, const Type* rhsTy);
    const Type* checkUnary(ast::UnaryExpr& expr);
    const Type* checkAssign(ast::AssignExpr& expr);
    const Type* checkCall(ast::CallExpr& expr);
    const Type* checkMethodCall(ast::MethodCallExpr& expr);
    const Type* checkIndex(ast::IndexExpr& expr);
    const Type* checkField(ast::FieldExpr& expr);
    const Type* checkCast(ast::CastExpr& expr);
    const Type* checkArrayInit(ast::ArrayInitExpr& expr);
    const Type* checkCompoundInit(ast::CompoundInitExpr& expr);
    const Type* checkThis(ast::ThisExpr& expr);
    const Type* checkConstruct(ast::ConstructExpr& expr);
    const Type* checkMatchExpr(ast::MatchExpr& expr);
    const Type* checkTryExpr(ast::TryExpr& expr);
    const Type* checkTernary(ast::TernaryExpr& expr);
    const Type* checkTuple(ast::TupleExpr& expr);
    const Type* checkTupleIndex(ast::TupleIndexExpr& expr);
    const Type* checkLambda(ast::LambdaExpr& expr);
    const Type* checkRange(ast::RangeExpr& expr);
    const Type* checkAwait(ast::AwaitExpr& expr);
    void checkMatchStmt(ast::MatchStmt& stmt, const Type* currentFuncReturnType);

    void setExprType(const ast::Expr* expr, const Type* type);

    TypeContext& tc_;
    SymbolTable& syms_;
    DiagnosticEngine& diag_;
    std::unordered_map<const ast::Expr*, const Type*> exprTypes_;
    std::unordered_map<const ast::VarDeclStmt*, const Type*> varDeclTypes_;
    std::unordered_map<const ast::DestructureVarDeclStmt*, std::vector<const Type*>> destructureTypes_;
    std::vector<std::unordered_map<std::string, bool>> assignedStack_;
    std::vector<std::unordered_map<std::string, bool>> movedStack_;
    std::unordered_set<std::string> conditionallyMovedVars_;
    std::vector<std::unordered_map<std::string, std::unordered_set<std::string>>> uninitFieldsStack_;
    std::vector<std::unordered_map<std::string, std::unordered_set<int64_t>>> uninitArrayElementsStack_;
    struct ActiveBorrow {
        std::string borrower;
        std::string target;
        std::string rootTarget;
        bool isMutable = true;
        size_t scopeDepth = 0;
        SourceLocation loc;
    };
    std::vector<std::vector<ActiveBorrow>> activeBorrowsStack_;
    std::unordered_set<std::string> parallelLoopWrittenArrays_;

    struct LambdaCheckContext {
        ast::LambdaExpr* expr = nullptr;
        size_t scopeDepth = 0;
        std::unordered_map<std::string, LambdaCapture> captures;
    };
    std::vector<LambdaCheckContext> activeLambdaStack_;
    std::unordered_map<const ast::LambdaExpr*, std::vector<LambdaCapture>> lambdaCaptures_;

    bool registerBorrow(const std::string& borrower, const std::string& target, const std::string& rootTarget,
                        bool isMutable, size_t scopeDepth, SourceLocation loc);
    bool checkBorrowMutation(const std::string& targetOrRoot, const std::string& targetExprStr, SourceLocation loc);
    void checkCallSiteBorrowSafety(const std::string& calleeName,
                                   const std::vector<ast::ExprPtr>& args,
                                   const std::vector<const Type*>& paramTypes,
                                   SourceLocation loc);
    int loopDepth_ = 0;
    size_t parallelLoopScopeDepth_ = 0;
    int taskScopeDepth_ = 0;
    std::unordered_set<std::string> parallelLoopInductionVars_;
    std::unordered_set<std::string> activeReductionVars_;
    const Type* currentRecord_ = nullptr;
    bool isCurrentMethodConst_ = false;
    const ModuleScope* currentModuleScope_ = nullptr;
    std::unordered_map<std::string, const ModuleScope*> moduleScopesByName_;
    std::unordered_map<std::string, const Type*> globalFunctions_;
    std::vector<uint64_t> activeRegionIds_;
    std::unordered_map<uint64_t, uint64_t> regionParent_;
    std::unordered_map<uint64_t, size_t> regionDepth_;
    uint64_t nextRegionId_ = 1;
    std::unordered_map<const ast::Expr*, ProvenanceSet> exprProvenance_;
    std::unordered_map<uint64_t, ProvenanceSet> bindingProvenance_;
    std::unordered_map<uint64_t, std::unordered_map<std::string, ProvenanceSet>> bindingFieldProvenance_;
    std::unordered_map<std::string, ProvenanceSet> varProvenanceFallback_;
    std::unordered_map<std::string, std::unordered_map<std::string, ProvenanceSet>> varFieldProvenanceFallback_;
    std::unordered_map<std::string, FunctionBorrowSummary> functionSummaries_;
    std::string currentFunctionName_;
    const Type* currentFuncReturnType_ = nullptr;
    std::vector<MemoryReportEntry> memoryReport_;

    ast::Program* activeProgram_ = nullptr;
    std::unordered_map<std::string, const ast::FunctionDecl*> genericFunctions_;
    std::unordered_map<std::string, const ast::StructDecl*> genericStructs_;
    std::unordered_map<std::string, const ast::EnumDecl*> genericEnums_;
    std::unordered_map<std::string, const ast::TraitDecl*> traits_;
    std::unordered_map<std::string, std::unordered_set<std::string>> implementedTraits_;
    std::unordered_map<std::string, const Type*> instantiatedFunctionTypes_;
    std::unordered_map<std::string, const Type*> instantiatedStructTypes_;
    std::unordered_map<std::string, const Type*> instantiatedEnumTypes_;
    std::vector<std::unique_ptr<ast::FunctionDecl>> ownedMonomorphizedFunctions_;
    std::vector<std::unique_ptr<ast::StructDecl>> ownedMonomorphizedStructs_;
    std::vector<std::unique_ptr<ast::EnumDecl>> ownedMonomorphizedEnums_;
    int exprDepth_ = 0;
    int stmtDepth_ = 0;
    static constexpr int MAX_EXPR_DEPTH = 256;
    static constexpr int MAX_STMT_DEPTH = 256;
};

} // namespace tamarin::sema

