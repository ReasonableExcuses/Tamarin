// ============================================================================
// type_checker.cpp - Type resolution, compatibility checking implementation
// ============================================================================

#include "tamarin/sema/type_checker.h"
#include "tamarin/sema/monomorphizer.h"
#include <iostream>
#include <iomanip>
#include <algorithm>

namespace tamarin::sema {

using namespace ast;

void TypeChecker::registerGenericFunction(const std::string& name, const ast::FunctionDecl* decl) {
    genericFunctions_[name] = decl;
}

const ast::FunctionDecl* TypeChecker::findGenericFunction(const std::string& name) const {
    if (name.find("::") != std::string::npos) {
        size_t colonPos = name.find("::");
        std::string prefix = name.substr(0, colonPos);
        std::string member = name.substr(colonPos + 2);
        if (currentModuleScope_) {
            auto it = currentModuleScope_->aliasToModule.find(prefix);
            if (it != currentModuleScope_->aliasToModule.end()) {
                const ModuleScope* targetMod = it->second;
                auto fnIt = targetMod->exportedGenericFunctions.find(member);
                if (fnIt != targetMod->exportedGenericFunctions.end()) {
                    return fnIt->second;
                }
            }
        }
    }
    if (currentModuleScope_) {
        auto fnIt = currentModuleScope_->genericFunctions.find(name);
        if (fnIt != currentModuleScope_->genericFunctions.end()) {
            return fnIt->second;
        }
    }
    auto it = genericFunctions_.find(name);
    return it != genericFunctions_.end() ? it->second : nullptr;
}

void TypeChecker::registerGenericStruct(const std::string& name, const ast::StructDecl* decl) {
    genericStructs_[name] = decl;
}

const ast::StructDecl* TypeChecker::findGenericStruct(const std::string& name) const {
    if (name.find("::") != std::string::npos) {
        size_t colonPos = name.find("::");
        std::string prefix = name.substr(0, colonPos);
        std::string member = name.substr(colonPos + 2);
        if (currentModuleScope_) {
            auto it = currentModuleScope_->aliasToModule.find(prefix);
            if (it != currentModuleScope_->aliasToModule.end()) {
                const ModuleScope* targetMod = it->second;
                auto sIt = targetMod->exportedGenericStructs.find(member);
                if (sIt != targetMod->exportedGenericStructs.end()) {
                    return sIt->second;
                }
            }
        }
    }
    if (currentModuleScope_) {
        auto sIt = currentModuleScope_->genericStructs.find(name);
        if (sIt != currentModuleScope_->genericStructs.end()) {
            return sIt->second;
        }
    }
    auto it = genericStructs_.find(name);
    return it != genericStructs_.end() ? it->second : nullptr;
}

void TypeChecker::registerGenericEnum(const std::string& name, const ast::EnumDecl* decl) {
    genericEnums_[name] = decl;
}

const ast::EnumDecl* TypeChecker::findGenericEnum(const std::string& name) const {
    if (name.find("::") != std::string::npos) {
        size_t colonPos = name.find("::");
        std::string prefix = name.substr(0, colonPos);
        std::string member = name.substr(colonPos + 2);
        if (currentModuleScope_) {
            auto it = currentModuleScope_->aliasToModule.find(prefix);
            if (it != currentModuleScope_->aliasToModule.end()) {
                const ModuleScope* targetMod = it->second;
                auto eIt = targetMod->exportedGenericEnums.find(member);
                if (eIt != targetMod->exportedGenericEnums.end()) {
                    return eIt->second;
                }
            }
        }
    }
    if (currentModuleScope_) {
        auto it = currentModuleScope_->genericEnums.find(name);
        if (it != currentModuleScope_->genericEnums.end()) {
            return it->second;
        }
    }
    auto it = genericEnums_.find(name);
    return it != genericEnums_.end() ? it->second : nullptr;
}

void TypeChecker::registerTrait(const std::string& name, const ast::TraitDecl* decl) {
    traits_[name] = decl;
}

const ast::TraitDecl* TypeChecker::findTrait(const std::string& name) const {
    auto it = traits_.find(name);
    return it != traits_.end() ? it->second : nullptr;
}

void TypeChecker::registerTraitImpl(const std::string& targetTypeName, const std::string& traitName) {
    implementedTraits_[targetTypeName].insert(traitName);
}

bool TypeChecker::typeImplementsTrait(const std::string& targetTypeName, const std::string& traitName) const {
    auto it = implementedTraits_.find(targetTypeName);
    if (it != implementedTraits_.end() && it->second.count(traitName)) return true;
    return false;
}

const Type* TypeChecker::instantiateGenericStruct(
    const ast::StructDecl& tmpl,
    const std::vector<const Type*>& typeArgs,
    SourceLocation loc) {

    if (typeArgs.size() != tmpl.typeParams.size()) {
        diag_.report(loc, Severity::Error,
            "generic struct '" + tmpl.name + "' expects " +
            std::to_string(tmpl.typeParams.size()) + " type arguments, got " +
            std::to_string(typeArgs.size()));
        return tc_.getI32();
    }

    // Validate trait bounds on type parameters
    for (const auto& bound : tmpl.traitBounds) {
        auto it = std::find(tmpl.typeParams.begin(), tmpl.typeParams.end(), bound.first);
        if (it != tmpl.typeParams.end()) {
            size_t idx = std::distance(tmpl.typeParams.begin(), it);
            const Type* argTy = typeArgs[idx];
            std::string argName = argTy ? (argTy->isRecord() ? argTy->structName : argTy->toString()) : "";
            if (!typeImplementsTrait(argName, bound.second)) {
                diag_.report(loc, Severity::Error,
                    "type '" + argName + "' does not satisfy trait bound '" + bound.second +
                    "' required by struct '" + tmpl.name + "'");
            }
        }
    }

    std::string mangledName = Monomorphizer::mangleSpecialization(tmpl.name, typeArgs);
    auto existing = tc_.findRecord(mangledName);
    if (existing) return existing;

    TypeSubstMap subst;
    std::vector<TypePtr> ownedNodes;
    for (size_t i = 0; i < tmpl.typeParams.size(); ++i) {
        auto node = Monomorphizer::typeToASTTypeNode(typeArgs[i], loc);
        subst[tmpl.typeParams[i]] = node.get();
        ownedNodes.push_back(std::move(node));
    }

    auto specializedStruct = Monomorphizer::monomorphizeStruct(tmpl, mangledName, subst);

    // Forward declare the record in TypeContext
    tc_.getClass(mangledName, {}, static_cast<int>(specializedStruct->layout),
                 "", specializedStruct->isClass, specializedStruct->isInterface);

    // Resolve field types
    std::vector<StructFieldType> fieldTypes;
    for (const auto& f : specializedStruct->fields) {
        auto fTy = resolveType(*f.type);
        fieldTypes.push_back(StructFieldType{
            .name = f.name,
            .type = fTy,
            .isConst = f.isConst,
            .isCold = f.isCold,
            .access = static_cast<int>(f.access),
        });
    }

    // Register full struct in TypeContext
    auto recTy = tc_.getClass(mangledName, std::move(fieldTypes),
                              static_cast<int>(specializedStruct->layout),
                              "", specializedStruct->isClass, specializedStruct->isInterface);

    auto mutRecTy = const_cast<Type*>(recTy);

    // Register methods
    for (auto& m : specializedStruct->methods) {
        auto retTy = resolveType(*m.returnType);
        std::vector<const Type*> paramTypes;
        std::vector<std::string> paramNames;
        for (auto& p : m.params) {
            auto pTy = resolveType(*p.type);
            if (p.isArray) pTy = tc_.getArrayView(pTy, p.isConst);
            paramTypes.push_back(pTy);
            paramNames.push_back(p.name);
        }
        mutRecTy->methods.push_back(MethodType{
            .name = m.name,
            .definingClass = mangledName,
            .returnType = retTy,
            .paramTypes = std::move(paramTypes),
            .paramNames = std::move(paramNames),
            .isConst = m.isConst,
            .access = static_cast<int>(m.access),
        });
    }

    // Register constructors
    for (auto& c : specializedStruct->constructors) {
        std::vector<const Type*> cParamTypes;
        std::vector<std::string> cParamNames;
        for (auto& p : c.params) {
            auto pTy = resolveType(*p.type);
            if (p.isArray) pTy = tc_.getArrayView(pTy);
            cParamTypes.push_back(pTy);
            cParamNames.push_back(p.name);
        }
        mutRecTy->constructors.push_back(ConstructorType{
            .paramTypes = std::move(cParamTypes),
            .paramNames = std::move(cParamNames),
            .access = static_cast<int>(c.access),
        });
    }

    if (specializedStruct->destructor) {
        mutRecTy->destructor = std::make_unique<DestructorType>(DestructorType{
            .isVirtual = specializedStruct->destructor->isVirtual,
            .access = static_cast<int>(specializedStruct->destructor->access),
        });
    }

    if (activeProgram_) {
        activeProgram_->decls.push_back(std::move(specializedStruct));
    } else {
        ownedMonomorphizedStructs_.push_back(std::move(specializedStruct));
    }

    instantiatedStructTypes_[mangledName] = recTy;
    return recTy;
}

const Type* TypeChecker::instantiateGenericFunction(
    const ast::FunctionDecl& tmpl,
    const std::vector<const Type*>& typeArgs,
    SourceLocation loc,
    std::string& outMangledName) {

    if (typeArgs.size() != tmpl.typeParams.size()) {
        diag_.report(loc, Severity::Error,
            "generic function '" + tmpl.name + "' expects " +
            std::to_string(tmpl.typeParams.size()) + " type arguments, got " +
            std::to_string(typeArgs.size()));
        return tc_.getI32();
    }

    // Validate trait bounds
    for (const auto& bound : tmpl.traitBounds) {
        auto it = std::find(tmpl.typeParams.begin(), tmpl.typeParams.end(), bound.first);
        if (it != tmpl.typeParams.end()) {
            size_t idx = std::distance(tmpl.typeParams.begin(), it);
            const Type* argTy = typeArgs[idx];
            std::string argName = argTy ? (argTy->isRecord() ? argTy->structName : argTy->toString()) : "";
            if (!typeImplementsTrait(argName, bound.second)) {
                diag_.report(loc, Severity::Error,
                    "type '" + argName + "' does not satisfy trait bound '" + bound.second +
                    "' required by function '" + tmpl.name + "'");
            }
        }
    }

    std::string mangledName = Monomorphizer::mangleSpecialization(tmpl.name, typeArgs);
    outMangledName = mangledName;

    auto it = instantiatedFunctionTypes_.find(mangledName);
    if (it != instantiatedFunctionTypes_.end()) {
        return it->second;
    }

    TypeSubstMap subst;
    std::vector<TypePtr> ownedNodes;
    for (size_t i = 0; i < tmpl.typeParams.size(); ++i) {
        auto node = Monomorphizer::typeToASTTypeNode(typeArgs[i], loc);
        subst[tmpl.typeParams[i]] = node.get();
        ownedNodes.push_back(std::move(node));
    }

    auto specializedFn = Monomorphizer::monomorphizeFunction(tmpl, mangledName, subst);

    auto retTy = resolveType(*specializedFn->returnType);
    std::vector<const Type*> paramTys;
    for (auto& p : specializedFn->params) {
        auto pTy = resolveType(*p.type);
        if (p.isArray) pTy = tc_.getArrayView(pTy, p.isConst);
        paramTys.push_back(pTy);
    }
    auto fnTy = tc_.getFunction(retTy, std::move(paramTys));

    Symbol sym{
        .name = mangledName,
        .mangledName = mangledName,
        .type = fnTy,
        .isConst = true,
        .isFunction = true,
        .isParameter = false,
        .isPublic = specializedFn->isPublic,
        .loc = loc,
    };
    syms_.declare(mangledName, sym);
    registerGlobalFunction(mangledName, fnTy);
    instantiatedFunctionTypes_[mangledName] = fnTy;

    FunctionBorrowSummary summary;
    summary.functionName = mangledName;
    summary.hasBody = (specializedFn->body != nullptr);
    summary.isExternal = false;
    summary.isBuiltin = false;
    summary.parameterEffects.resize(specializedFn->params.size(), BorrowEffect::NoEscape);
    summary.receiverEffect = BorrowEffect::NoEscape;
    registerFunctionSummary(mangledName, summary);

    // Type check function body
    if (specializedFn->body) {
        syms_.pushScope();
        pushScope();
        auto savedFunc = currentFunctionName_;
        auto savedRet = currentFuncReturnType_;
        currentFunctionName_ = mangledName;
        currentFuncReturnType_ = retTy;

        for (size_t i = 0; i < specializedFn->params.size(); ++i) {
            const auto& p = specializedFn->params[i];
            const auto* pTy = fnTy->paramTypes[i];
            ProvenanceSet paramProv(ProvenanceKind::ParameterBorrow, 0, p.name);
            Symbol pSym{
                .name = p.name,
                .mangledName = p.name,
                .type = pTy,
                .isConst = p.isConst,
                .isFunction = false,
                .isParameter = true,
                .isPublic = false,
                .regionId = 0,
                .provenance = paramProv,
                .loc = p.loc,
            };
            syms_.declare(p.name, pSym);
            setVarProvenance(p.name, paramProv);
            markAssigned(p.name);
        }

        checkStmt(*specializedFn->body, retTy);

        currentFunctionName_ = savedFunc;
        currentFuncReturnType_ = savedRet;
        popScope();
        syms_.popScope();
    }

    if (activeProgram_) {
        activeProgram_->decls.push_back(std::move(specializedFn));
    } else {
        ownedMonomorphizedFunctions_.push_back(std::move(specializedFn));
    }

    return fnTy;
}

const Type* TypeChecker::instantiateGenericEnum(
    const ast::EnumDecl& tmpl,
    const std::vector<const Type*>& typeArgs,
    SourceLocation loc) {

    if (typeArgs.size() != tmpl.typeParams.size()) {
        diag_.report(loc, Severity::Error,
            "generic enum '" + tmpl.name + "' expects " +
            std::to_string(tmpl.typeParams.size()) + " type arguments, got " +
            std::to_string(typeArgs.size()));
        return tc_.getI32();
    }

    std::string mangledName = Monomorphizer::mangleSpecialization(tmpl.name, typeArgs);
    auto existing = tc_.findEnum(mangledName);
    if (existing) return existing;

    TypeSubstMap subst;
    std::vector<TypePtr> ownedNodes;
    for (size_t i = 0; i < tmpl.typeParams.size(); ++i) {
        auto node = Monomorphizer::typeToASTTypeNode(typeArgs[i], loc);
        subst[tmpl.typeParams[i]] = node.get();
        ownedNodes.push_back(std::move(node));
    }

    auto specializedEnum = Monomorphizer::monomorphizeEnum(tmpl, mangledName, subst);

    std::vector<EnumVariantType> variants;
    bool isNiche = false;
    for (int32_t i = 0; i < static_cast<int32_t>(specializedEnum->variants.size()); ++i) {
        const auto& v = specializedEnum->variants[i];
        std::vector<const Type*> payloadTys;
        for (const auto& pTyNode : v.payloadTypes) {
            auto pTy = resolveType(*pTyNode);
            if (pTy && pTy->isHandle()) isNiche = true;
            payloadTys.push_back(pTy);
        }
        variants.push_back(EnumVariantType{v.name, i, std::move(payloadTys)});
    }

    auto enumTy = tc_.getEnum(mangledName, std::move(variants), isNiche);
    instantiatedEnumTypes_[mangledName] = enumTy;

    if (activeProgram_) {
        activeProgram_->decls.push_back(std::move(specializedEnum));
    } else {
        ownedMonomorphizedEnums_.push_back(std::move(specializedEnum));
    }

    return enumTy;
}

void TypeChecker::flushMonomorphizedDecls(ast::Program& prog) {
    for (auto& s : ownedMonomorphizedStructs_) {
        prog.decls.push_back(std::move(s));
    }
    ownedMonomorphizedStructs_.clear();
    for (auto& f : ownedMonomorphizedFunctions_) {
        prog.decls.push_back(std::move(f));
    }
    ownedMonomorphizedFunctions_.clear();
    for (auto& e : ownedMonomorphizedEnums_) {
        prog.decls.push_back(std::move(e));
    }
    ownedMonomorphizedEnums_.clear();
}

TypeChecker::TypeChecker(TypeContext& tc, SymbolTable& syms, DiagnosticEngine& diag)
    : tc_(tc), syms_(syms), diag_(diag) {}

const Type* TypeChecker::getExprType(const ast::Expr* expr) const {
    auto it = exprTypes_.find(expr);
    return it != exprTypes_.end() ? it->second : nullptr;
}

void TypeChecker::setExprType(const ast::Expr* expr, const Type* type) {
    exprTypes_[expr] = type;
}

uint64_t TypeChecker::getExprRegionProvenance(const ast::Expr* expr) const {
    if (!expr) return 0;
    auto it = exprProvenance_.find(expr);
    if (it != exprProvenance_.end()) {
        return it->second.primaryRegionId();
    }
    return 0;
}

void TypeChecker::setExprRegionProvenance(const ast::Expr* expr, uint64_t regionId) {
    if (expr && regionId > 0) {
        exprProvenance_[expr].add(ProvenanceOrigin{ProvenanceKind::LocalRegion, regionId, ""});
    }
}

ProvenanceSet TypeChecker::getExprProvenance(const ast::Expr* expr) const {
    if (!expr) return {};
    auto it = exprProvenance_.find(expr);
    if (it != exprProvenance_.end()) {
        return it->second;
    }
    return {};
}

void TypeChecker::setExprProvenance(const ast::Expr* expr, ProvenanceSet prov) {
    if (expr) {
        exprProvenance_[expr] = std::move(prov);
    }
}

void TypeChecker::setVarProvenance(uint64_t bindingId, ProvenanceSet prov) {
    bindingProvenance_[bindingId] = std::move(prov);
}

void TypeChecker::setVarProvenance(const std::string& name, ProvenanceSet prov) {
    auto sym = syms_.lookup(name);
    if (sym) {
        bindingProvenance_[sym->bindingId] = std::move(prov);
    } else {
        varProvenanceFallback_[name] = std::move(prov);
    }
}

ProvenanceSet TypeChecker::getVarProvenance(uint64_t bindingId) const {
    auto it = bindingProvenance_.find(bindingId);
    if (it != bindingProvenance_.end()) return it->second;
    auto sym = syms_.lookupBinding(bindingId);
    if (sym) {
        if (!sym->provenance.empty()) return sym->provenance;
        if (sym->regionId > 0) return ProvenanceSet(ProvenanceKind::LocalRegion, sym->regionId);
    }
    return {};
}

ProvenanceSet TypeChecker::getVarProvenance(const std::string& name) const {
    auto sym = syms_.lookup(name);
    if (sym) {
        auto it = bindingProvenance_.find(sym->bindingId);
        if (it != bindingProvenance_.end()) return it->second;
        if (!sym->provenance.empty()) return sym->provenance;
        if (sym->regionId > 0) return ProvenanceSet(ProvenanceKind::LocalRegion, sym->regionId);
    }
    auto it = varProvenanceFallback_.find(name);
    if (it != varProvenanceFallback_.end()) return it->second;
    return {};
}

void TypeChecker::setFieldProvenance(uint64_t bindingId, const std::string& fieldPath, ProvenanceSet prov) {
    bindingFieldProvenance_[bindingId][fieldPath] = prov;
    bindingProvenance_[bindingId].merge(prov);
}

void TypeChecker::setFieldProvenance(const std::string& varName, const std::string& fieldName, ProvenanceSet prov) {
    auto sym = syms_.lookup(varName);
    if (sym) {
        setFieldProvenance(sym->bindingId, fieldName, std::move(prov));
    } else {
        varFieldProvenanceFallback_[varName][fieldName] = std::move(prov);
    }
}

ProvenanceSet TypeChecker::getFieldProvenance(uint64_t bindingId, const std::string& fieldPath) const {
    auto it = bindingFieldProvenance_.find(bindingId);
    if (it != bindingFieldProvenance_.end()) {
        auto fit = it->second.find(fieldPath);
        if (fit != it->second.end()) return fit->second;
        auto dotPos = fieldPath.rfind('.');
        if (dotPos != std::string::npos) {
            std::string prefix = fieldPath.substr(0, dotPos);
            auto pit = it->second.find(prefix);
            if (pit != it->second.end()) return pit->second;
        }
    }
    return getVarProvenance(bindingId);
}

ProvenanceSet TypeChecker::getFieldProvenance(const std::string& varName, const std::string& fieldName) const {
    auto sym = syms_.lookup(varName);
    if (sym) {
        return getFieldProvenance(sym->bindingId, fieldName);
    }
    auto it = varFieldProvenanceFallback_.find(varName);
    if (it != varFieldProvenanceFallback_.end()) {
        auto fit = it->second.find(fieldName);
        if (fit != it->second.end()) return fit->second;
    }
    return getVarProvenance(varName);
}

TypeChecker::LValuePath TypeChecker::resolveLValuePath(const ast::Expr* expr) const {
    if (!expr) return {};

    if (expr->kind == ExprKind::Identifier) {
        auto& id = static_cast<const IdentifierExpr&>(*expr);
        auto sym = syms_.lookup(id.name);
        if (sym) {
            bool isGlob = sym->isGlobal || sym->provenance.hasStaticStorage();
            if (currentModuleScope_ && currentModuleScope_->symbols.count(id.name) > 0) {
                isGlob = true;
            }
            return LValuePath{
                .rootSymbol = sym,
                .bindingId = sym->bindingId,
                .path = "",
                .isValid = true,
                .isGlobal = isGlob
            };
        }
        return {};
    }

    if (expr->kind == ExprKind::Field) {
        auto& fe = static_cast<const FieldExpr&>(*expr);
        auto objPath = resolveLValuePath(fe.object.get());
        if (objPath.isValid) {
            std::string sub = objPath.path.empty() ? fe.fieldName : (objPath.path + "." + fe.fieldName);
            return LValuePath{
                .rootSymbol = objPath.rootSymbol,
                .bindingId = objPath.bindingId,
                .path = std::move(sub),
                .isValid = true,
                .isGlobal = objPath.isGlobal
            };
        }
        return {};
    }

    if (expr->kind == ExprKind::Index) {
        auto& idx = static_cast<const IndexExpr&>(*expr);
        auto arrPath = resolveLValuePath(idx.array.get());
        if (arrPath.isValid) {
            std::string sub = arrPath.path + "[]";
            return LValuePath{
                .rootSymbol = arrPath.rootSymbol,
                .bindingId = arrPath.bindingId,
                .path = std::move(sub),
                .isValid = true,
                .isGlobal = arrPath.isGlobal
            };
        }
        return {};
    }

    return {};
}

bool TypeChecker::isRegionAncestor(uint64_t ancestor, uint64_t descendant) const {
    if (ancestor == 0 || descendant == 0) return false;
    if (ancestor == descendant) return true;
    uint64_t cur = descendant;
    while (cur > 0) {
        auto it = regionParent_.find(cur);
        if (it == regionParent_.end()) break;
        cur = it->second;
        if (cur == ancestor) return true;
    }
    return false;
}

bool TypeChecker::doesRegionOutlive(uint64_t srcRegion, uint64_t destRegion) const {
    if (srcRegion == 0) return true;
    if (destRegion == 0) return false;
    return isRegionAncestor(srcRegion, destRegion);
}

void TypeChecker::registerFunctionSummary(const std::string& name, FunctionBorrowSummary summary) {
    functionSummaries_[name] = std::move(summary);
}

const FunctionBorrowSummary* TypeChecker::getFunctionSummary(const std::string& name) const {
    auto it = functionSummaries_.find(name);
    return it != functionSummaries_.end() ? &it->second : nullptr;
}

bool TypeChecker::canAssignProvenance(const ProvenanceSet& srcProv, uint64_t destRegionId,
                                      bool isDestGlobal, std::string& outReason) const {
    if (srcProv.empty()) return true;

    for (const auto& o : srcProv.origins) {
        if (o.kind == ProvenanceKind::None || o.kind == ProvenanceKind::StaticStorage) {
            continue;
        }
        if (isDestGlobal) {
            if (o.kind == ProvenanceKind::LocalRegion) {
                outReason = "cannot assign region-backed reference or view to global storage";
                return false;
            }
            if (o.kind == ProvenanceKind::ParameterBorrow) {
                outReason = "cannot assign borrowed parameter" + (o.detail.empty() ? "" : (" '" + o.detail + "'")) + " to static or global storage";
                return false;
            }
            if (o.kind == ProvenanceKind::ThisBorrow) {
                outReason = "cannot assign borrowed 'this' receiver to static or global storage";
                return false;
            }
            if (o.kind == ProvenanceKind::Unknown) {
                outReason = "cannot assign unknown provenance to static or global storage";
                return false;
            }
        }
        if (o.kind == ProvenanceKind::LocalRegion) {
            if (!doesRegionOutlive(o.identity, destRegionId)) {
                outReason = "cannot assign region-backed reference or view to an outer lifetime";
                return false;
            }
        }
        if (o.kind == ProvenanceKind::Unknown) {
            outReason = "cannot escape indeterminate provenance to outer lifetime";
            return false;
        }
    }
    return true;
}

TypeChecker::ConstEvalResult TypeChecker::evalIntConstExpr(const ast::Expr& expr) const {
    ConstEvalResult res;
    switch (expr.kind) {
        case ExprKind::IntLiteral: {
            auto& lit = static_cast<const IntLiteralExpr&>(expr);
            res.isConst = true;
            res.value = lit.value;
            return res;
        }
        case ExprKind::BoolLiteral: {
            auto& lit = static_cast<const BoolLiteralExpr&>(expr);
            res.isConst = true;
            res.value = lit.value ? 1 : 0;
            return res;
        }
        case ExprKind::Ternary: {
            auto& t = static_cast<const TernaryExpr&>(expr);
            auto cond = evalIntConstExpr(*t.condition);
            if (!cond.isConst) return res;
            if (cond.overflow || cond.divByZero) return cond;
            if (cond.value != 0) {
                return evalIntConstExpr(*t.thenExpr);
            } else {
                return evalIntConstExpr(*t.elseExpr);
            }
        }
        case ExprKind::Unary: {
            auto& u = static_cast<const UnaryExpr&>(expr);
            auto opRes = evalIntConstExpr(*u.operand);
            if (!opRes.isConst) return res;
            if (opRes.overflow || opRes.divByZero) return opRes;
            res.isConst = true;
            if (u.op == UnaryOp::Neg) {
                if (opRes.value == INT64_MIN) {
                    res.overflow = true;
                } else {
                    res.value = -opRes.value;
                }
            } else if (u.op == UnaryOp::BitNot) {
                res.value = ~opRes.value;
            } else if (u.op == UnaryOp::Not) {
                res.value = (opRes.value == 0) ? 1 : 0;
            } else {
                res.isConst = false;
            }
            return res;
        }
        case ExprKind::Binary: {
            auto& b = static_cast<const BinaryExpr&>(expr);
            auto left = evalIntConstExpr(*b.lhs);
            if (!left.isConst) return res;
            if (left.overflow || left.divByZero) return left;
            if (b.op == BinaryOp::And) {
                if (left.value == 0) {
                    res.isConst = true;
                    res.value = 0;
                    return res;
                }
                auto right = evalIntConstExpr(*b.rhs);
                if (!right.isConst) return res;
                if (right.overflow || right.divByZero) return right;
                res.isConst = true;
                res.value = (right.value != 0) ? 1 : 0;
                return res;
            }
            if (b.op == BinaryOp::Or) {
                if (left.value != 0) {
                    res.isConst = true;
                    res.value = 1;
                    return res;
                }
                auto right = evalIntConstExpr(*b.rhs);
                if (!right.isConst) return res;
                if (right.overflow || right.divByZero) return right;
                res.isConst = true;
                res.value = (right.value != 0) ? 1 : 0;
                return res;
            }
            auto right = evalIntConstExpr(*b.rhs);
            if (!right.isConst) return res;
            if (right.overflow || right.divByZero) return right;
            if (left.divByZero || right.divByZero) {
                res.isConst = true;
                res.divByZero = true;
                return res;
            }
            res.isConst = true;
            int64_t lhs = left.value;
            int64_t rhs = right.value;
            switch (b.op) {
                case BinaryOp::Add: {
                    if ((rhs > 0 && lhs > INT64_MAX - rhs) || (rhs < 0 && lhs < INT64_MIN - rhs)) {
                        res.overflow = true;
                    } else {
                        res.value = lhs + rhs;
                    }
                    break;
                }
                case BinaryOp::Sub: {
                    if ((rhs < 0 && lhs > INT64_MAX + rhs) || (rhs > 0 && lhs < INT64_MIN + rhs)) {
                        res.overflow = true;
                    } else {
                        res.value = lhs - rhs;
                    }
                    break;
                }
                case BinaryOp::Mul: {
                    if (lhs > 0) {
                        if (rhs > 0) {
                            if (lhs > INT64_MAX / rhs) res.overflow = true;
                            else res.value = lhs * rhs;
                        } else if (rhs < 0) {
                            if (rhs < INT64_MIN / lhs) res.overflow = true;
                            else res.value = lhs * rhs;
                        } else {
                            res.value = 0;
                        }
                    } else if (lhs < 0) {
                        if (rhs > 0) {
                            if (lhs < INT64_MIN / rhs) res.overflow = true;
                            else res.value = lhs * rhs;
                        } else if (rhs < 0) {
                            if (lhs <= INT64_MIN || rhs <= INT64_MIN || -lhs > INT64_MAX / (-rhs)) res.overflow = true;
                            else res.value = lhs * rhs;
                        } else {
                            res.value = 0;
                        }
                    } else {
                        res.value = 0;
                    }
                    break;
                }
                case BinaryOp::Div: {
                    if (rhs == 0) {
                        res.divByZero = true;
                    } else if (lhs == INT64_MIN && rhs == -1) {
                        res.overflow = true;
                    } else {
                        res.value = lhs / rhs;
                    }
                    break;
                }
                case BinaryOp::Mod: {
                    if (rhs == 0) {
                        res.divByZero = true;
                    } else if (lhs == INT64_MIN && rhs == -1) {
                        res.value = 0;
                    } else {
                        res.value = lhs % rhs;
                    }
                    break;
                }
                case BinaryOp::Shl: {
                    if (rhs < 0 || rhs >= 64) {
                        res.overflow = true;
                    } else if (lhs < 0 || (lhs > (INT64_MAX >> rhs))) {
                        res.overflow = true;
                    } else {
                        res.value = lhs << rhs;
                    }
                    break;
                }
                case BinaryOp::Shr: {
                    if (rhs < 0 || rhs >= 64) {
                        res.overflow = true;
                    } else {
                        res.value = lhs >> rhs;
                    }
                    break;
                }
                case BinaryOp::BitAnd: {
                    res.value = lhs & rhs;
                    break;
                }
                case BinaryOp::BitOr: {
                    res.value = lhs | rhs;
                    break;
                }
                case BinaryOp::BitXor: {
                    res.value = lhs ^ rhs;
                    break;
                }
                case BinaryOp::EqEq: {
                    res.value = (lhs == rhs) ? 1 : 0;
                    break;
                }
                case BinaryOp::NotEq: {
                    res.value = (lhs != rhs) ? 1 : 0;
                    break;
                }
                case BinaryOp::Lt: {
                    res.value = (lhs < rhs) ? 1 : 0;
                    break;
                }
                case BinaryOp::LtEq: {
                    res.value = (lhs <= rhs) ? 1 : 0;
                    break;
                }
                case BinaryOp::Gt: {
                    res.value = (lhs > rhs) ? 1 : 0;
                    break;
                }
                case BinaryOp::GtEq: {
                    res.value = (lhs >= rhs) ? 1 : 0;
                    break;
                }
                default:
                    res.isConst = false;
                    break;
            }
            return res;
        }
        default:
            return res;
    }
}

void TypeChecker::pushScope() {
    assignedStack_.emplace_back();
    movedStack_.emplace_back();
    uninitFieldsStack_.emplace_back();
    uninitArrayElementsStack_.emplace_back();
    activeBorrowsStack_.emplace_back();
}

void TypeChecker::popScope() {
    if (!assignedStack_.empty()) {
        assignedStack_.pop_back();
    }
    if (!movedStack_.empty()) {
        movedStack_.pop_back();
    }
    if (!uninitFieldsStack_.empty()) {
        uninitFieldsStack_.pop_back();
    }
    if (!uninitArrayElementsStack_.empty()) {
        uninitArrayElementsStack_.pop_back();
    }
    if (!activeBorrowsStack_.empty()) {
        activeBorrowsStack_.pop_back();
    }
}

static std::pair<std::string, std::string> extractBorrowTarget(const ast::Expr* expr) {
    if (!expr) return {"", ""};
    if (expr->kind == ast::ExprKind::Identifier) {
        std::string n = static_cast<const ast::IdentifierExpr*>(expr)->name;
        return {n, n};
    }
    if (expr->kind == ast::ExprKind::Field) {
        auto& fe = static_cast<const ast::FieldExpr&>(*expr);
        auto [innerTarget, root] = extractBorrowTarget(fe.object.get());
        std::string t = innerTarget.empty() ? fe.fieldName : (innerTarget + "." + fe.fieldName);
        return {t, root.empty() ? innerTarget : root};
    }
    if (expr->kind == ast::ExprKind::Index) {
        auto& idx = static_cast<const ast::IndexExpr&>(*expr);
        auto [innerTarget, root] = extractBorrowTarget(idx.array.get());
        std::string idxStr = "?";
        if (idx.index->kind == ast::ExprKind::IntLiteral) {
            idxStr = std::to_string(static_cast<const ast::IntLiteralExpr&>(*idx.index).value);
        } else if (idx.index->kind == ast::ExprKind::Identifier) {
            idxStr = static_cast<const ast::IdentifierExpr&>(*idx.index).name;
        }
        std::string t = (innerTarget.empty() ? "arr" : innerTarget) + "[" + idxStr + "]";
        return {t, root.empty() ? innerTarget : root};
    }
    return {"", ""};
}

bool TypeChecker::registerBorrow(const std::string& borrower, const std::string& target, const std::string& rootTarget,
                                 bool isMutable, size_t scopeDepth, SourceLocation loc) {
    if (activeBorrowsStack_.empty()) return true;

    // Check if target is already actively borrowed
    for (const auto& frame : activeBorrowsStack_) {
        for (const auto& b : frame) {
            if (b.borrower == borrower) continue;
            bool targetMatch = (b.target == target || (!b.target.empty() && b.target == rootTarget) ||
                                (!target.empty() && b.rootTarget == target) ||
                                (!b.rootTarget.empty() && !rootTarget.empty() && b.rootTarget == rootTarget && (b.target == target || target.find('[') != std::string::npos)));
            if (targetMatch) {
                if (isMutable && b.isMutable) {
                    diag_.report(loc, Severity::Error,
                                 "cannot borrow '" + target + "' as mutable more than once at a time; already borrowed by '" + b.borrower + "'");
                    return false;
                }
            }
        }
    }

    if (!borrower.empty()) {
        for (auto& frame : activeBorrowsStack_) {
            for (auto& b : frame) {
                if (b.borrower == borrower) {
                    b.target = target;
                    b.rootTarget = rootTarget;
                    b.isMutable = isMutable;
                    b.scopeDepth = scopeDepth;
                    b.loc = loc;
                    return true;
                }
            }
        }
    }

    activeBorrowsStack_.back().push_back(ActiveBorrow{
        .borrower = borrower,
        .target = target,
        .rootTarget = rootTarget,
        .isMutable = isMutable,
        .scopeDepth = scopeDepth,
        .loc = loc
    });
    return true;
}

void TypeChecker::checkCallSiteBorrowSafety(const std::string& calleeName,
                                            const std::vector<ast::ExprPtr>& args,
                                            const std::vector<const Type*>& paramTypes,
                                            SourceLocation loc) {
    (void)loc;
    std::vector<ActiveBorrow> tempBorrows;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i]->kind == ExprKind::Lambda) {
            auto* caps = getLambdaCaptures(static_cast<const LambdaExpr*>(args[i].get()));
            if (caps) {
                for (const auto& cap : *caps) {
                    if (cap.isMutable) {
                        tempBorrows.push_back(ActiveBorrow{
                            .borrower = "closure_arg",
                            .target = cap.name,
                            .rootTarget = cap.name,
                            .isMutable = true,
                            .scopeDepth = syms_.depth(),
                            .loc = args[i]->loc
                        });
                    }
                }
            }
        }
    }
    if (!activeBorrowsStack_.empty()) {
        for (const auto& tb : tempBorrows) {
            activeBorrowsStack_.back().push_back(tb);
        }
    }

    std::vector<std::pair<std::string, std::string>> callMutBorrows;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i < paramTypes.size() && paramTypes[i]) {
            auto paramTy = paramTypes[i];
            if (paramTy->isReference() && !paramTy->isConstRef) {
                std::string tStr, rTarget;
                if (args[i]->kind == ExprKind::Unary &&
                    static_cast<UnaryExpr&>(*args[i]).op == UnaryOp::AddrOf) {
                    auto& un = static_cast<UnaryExpr&>(*args[i]);
                    std::tie(tStr, rTarget) = extractBorrowTarget(un.operand.get());
                } else if (args[i]->kind == ExprKind::Identifier) {
                    auto idName = static_cast<IdentifierExpr&>(*args[i]).name;
                    for (const auto& frame : activeBorrowsStack_) {
                        for (const auto& b : frame) {
                            if (b.borrower == idName) {
                                tStr = b.target;
                                rTarget = b.rootTarget;
                                break;
                            }
                        }
                        if (!tStr.empty()) break;
                    }
                    if (tStr.empty()) {
                        tStr = idName;
                        rTarget = idName;
                    }
                }

                if (!rTarget.empty()) {
                    // 1. Data-race check inside parallel loop
                    if (parallelLoopScopeDepth_ > 0) {
                        size_t targetDepth = syms_.lookupScopeDepth(rTarget);
                        if (targetDepth > 0 && targetDepth < parallelLoopScopeDepth_) {
                            diag_.report(args[i]->loc, Severity::Error,
                                         "data-race safety violation: passing mutable pointer or reference to outer variable '" +
                                         rTarget + "' inside parallel loop is disallowed");
                        }
                    }

                    // 2. Call-site mutable aliasing check against other args in same call
                    for (const auto& [prevT, prevR] : callMutBorrows) {
                        if (prevR == rTarget) {
                            diag_.report(args[i]->loc, Severity::Error,
                                         "cannot borrow '" + rTarget + "' as mutable more than once at a time in call to '" +
                                         calleeName + "'");
                            break;
                        }
                    }

                    // 3. Conflict with existing active borrows
                    for (const auto& frame : activeBorrowsStack_) {
                        for (const auto& b : frame) {
                            if (b.isMutable && (b.rootTarget == rTarget || b.target == tStr)) {
                                diag_.report(args[i]->loc, Severity::Error,
                                             "cannot borrow '" + rTarget + "' as mutable more than once at a time; already borrowed by '" +
                                             b.borrower + "'");
                                break;
                            }
                        }
                    }

                    callMutBorrows.push_back({tStr, rTarget});
                }
            }
        }

        // Check closure arguments
        if (args[i]->kind == ExprKind::Lambda) {
            auto* caps = getLambdaCaptures(static_cast<const LambdaExpr*>(args[i].get()));
            if (caps) {
                for (const auto& cap : *caps) {
                    if (cap.isMutable) {
                        if (parallelLoopScopeDepth_ > 0 && cap.scopeDepth < parallelLoopScopeDepth_) {
                            diag_.report(args[i]->loc, Severity::Error,
                                         "data-race safety violation: closure with mutable captures cannot be used inside parallel loop; outer variable '" +
                                         cap.name + "' is captured as mutable");
                        }
                        for (const auto& [prevT, prevR] : callMutBorrows) {
                            if (prevR == cap.name) {
                                diag_.report(args[i]->loc, Severity::Error,
                                             "cannot borrow '" + cap.name + "' as mutable more than once at a time in call to '" +
                                             calleeName + "'");
                                break;
                            }
                        }
                        for (const auto& frame : activeBorrowsStack_) {
                            for (const auto& b : frame) {
                                if (b.borrower == "closure_arg") continue;
                                if (b.isMutable && (b.rootTarget == cap.name || b.target == cap.name)) {
                                    diag_.report(args[i]->loc, Severity::Error,
                                                 "cannot borrow '" + cap.name + "' as mutable more than once at a time; already borrowed by '" +
                                                 b.borrower + "'");
                                    break;
                                }
                            }
                        }
                        callMutBorrows.push_back({cap.name, cap.name});
                    }
                }
            }
        } else if (args[i]->kind == ExprKind::Identifier) {
            auto idName = static_cast<IdentifierExpr&>(*args[i]).name;
            for (const auto& frame : activeBorrowsStack_) {
                for (const auto& b : frame) {
                    if (b.borrower == idName && b.isMutable) {
                        if (parallelLoopScopeDepth_ > 0) {
                            size_t targetDepth = syms_.lookupScopeDepth(b.rootTarget);
                            if (targetDepth > 0 && targetDepth < parallelLoopScopeDepth_) {
                                diag_.report(args[i]->loc, Severity::Error,
                                             "data-race safety violation: closure with mutable captures cannot be used inside parallel loop; outer variable '" +
                                             b.rootTarget + "' is captured as mutable");
                            }
                        }
                        for (const auto& [prevT, prevR] : callMutBorrows) {
                            if (prevR == b.rootTarget) {
                                diag_.report(args[i]->loc, Severity::Error,
                                             "cannot borrow '" + b.rootTarget + "' as mutable more than once at a time in call to '" +
                                             calleeName + "'");
                                break;
                            }
                        }
                        callMutBorrows.push_back({b.target, b.rootTarget});
                    }
                }
            }
        }
    }

    // Clean up temporary borrows
    if (!activeBorrowsStack_.empty() && !tempBorrows.empty()) {
        auto& topFrame = activeBorrowsStack_.back();
        size_t count = tempBorrows.size();
        while (count > 0 && !topFrame.empty()) {
            topFrame.pop_back();
            --count;
        }
    }
}

bool TypeChecker::checkBorrowMutation(const std::string& targetOrRoot, const std::string& targetExprStr, SourceLocation loc) {
    for (const auto& frame : activeBorrowsStack_) {
        for (const auto& b : frame) {
            if (b.borrower == targetOrRoot) continue;
            if (b.rootTarget == targetOrRoot || b.target == targetOrRoot || (!targetExprStr.empty() && b.target == targetExprStr)) {
                diag_.report(loc, Severity::Error,
                             "cannot mutate '" + targetOrRoot + "' while active pointer '" + b.borrower + "' borrows it");
                return false;
            }
        }
    }
    return true;
}

bool TypeChecker::checkBorrowMove(const std::string& targetOrRoot, SourceLocation loc) {
    for (const auto& frame : activeBorrowsStack_) {
        for (const auto& b : frame) {
            if (b.borrower == targetOrRoot) continue;
            if (b.rootTarget == targetOrRoot || b.target == targetOrRoot) {
                diag_.report(loc, Severity::Error,
                             "cannot move '" + targetOrRoot + "' while active pointer '" + b.borrower + "' borrows it");
                return false;
            }
        }
    }
    return true;
}

bool TypeChecker::isRValue(const ast::Expr* expr) {
    if (!expr) return false;
    switch (expr->kind) {
        case ast::ExprKind::Call:
        case ast::ExprKind::MethodCall:
        case ast::ExprKind::Construct:
        case ast::ExprKind::CompoundInit:
        case ast::ExprKind::ArrayInit:
        case ast::ExprKind::Match:
        case ast::ExprKind::Ternary:
        case ast::ExprKind::IntLiteral:
        case ast::ExprKind::FloatLiteral:
        case ast::ExprKind::StringLiteral:
        case ast::ExprKind::BoolLiteral:
            return true;
        default:
            return false;
    }
}

void TypeChecker::markAssigned(const std::string& name) {
    for (auto it = assignedStack_.rbegin(); it != assignedStack_.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) {
            found->second = true;
            return;
            break;
        }
    }
    if (!assignedStack_.empty()) {
        assignedStack_.back()[name] = true;
    }
    // Clear moved state upon reassignment
    for (auto it = movedStack_.rbegin(); it != movedStack_.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) {
            found->second = false;
            break;
        }
    }
}

void TypeChecker::markMoved(const std::string& name) {
    for (auto it = movedStack_.rbegin(); it != movedStack_.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) {
            found->second = true;
            break;
        }
    }
    if (!movedStack_.empty()) {
        movedStack_.back()[name] = true;
    }
    // Also mark as unassigned
    for (auto it = assignedStack_.rbegin(); it != assignedStack_.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) {
            found->second = false;
            return;
        }
    }
    if (!assignedStack_.empty()) {
        assignedStack_.back()[name] = false;
    }
}

bool TypeChecker::isMoved(const std::string& name) const {
    for (auto it = movedStack_.rbegin(); it != movedStack_.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) {
            return found->second;
        }
    }
    return false;
}

bool TypeChecker::isAssigned(const std::string& name) const {
    for (auto it = assignedStack_.rbegin(); it != assignedStack_.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) {
            return found->second;
        }
    }
    auto sym = syms_.lookup(name);
    if (sym && (sym->isParameter || sym->isFunction || sym->isConst)) {
        return true;
    }
    return true;
}

void TypeChecker::markFieldAssigned(const std::string& varName, const std::string& fieldName) {
    for (auto it = uninitFieldsStack_.rbegin(); it != uninitFieldsStack_.rend(); ++it) {
        auto found = it->find(varName);
        if (found != it->end()) {
            found->second.erase(fieldName);
            return;
        }
    }
}

void TypeChecker::markAllFieldsAssigned(const std::string& varName) {
    for (auto it = uninitFieldsStack_.rbegin(); it != uninitFieldsStack_.rend(); ++it) {
        auto found = it->find(varName);
        if (found != it->end()) {
            found->second.clear();
            return;
        }
    }
}

bool TypeChecker::isFieldUninitialized(const std::string& varName, const std::string& fieldName) const {
    for (auto it = uninitFieldsStack_.rbegin(); it != uninitFieldsStack_.rend(); ++it) {
        auto found = it->find(varName);
        if (found != it->end()) {
            return found->second.count(fieldName) > 0;
        }
    }
    return false;
}

void TypeChecker::markArrayElementAssigned(const std::string& arrName, int64_t idx) {
    for (auto it = uninitArrayElementsStack_.rbegin(); it != uninitArrayElementsStack_.rend(); ++it) {
        auto found = it->find(arrName);
        if (found != it->end()) {
            found->second.erase(idx);
            return;
        }
    }
}

void TypeChecker::markAllArrayElementsAssigned(const std::string& arrName) {
    for (auto it = uninitArrayElementsStack_.rbegin(); it != uninitArrayElementsStack_.rend(); ++it) {
        auto found = it->find(arrName);
        if (found != it->end()) {
            found->second.clear();
            return;
        }
    }
}

bool TypeChecker::isArrayElementUninitialized(const std::string& arrName, int64_t idx) const {
    for (auto it = uninitArrayElementsStack_.rbegin(); it != uninitArrayElementsStack_.rend(); ++it) {
        auto found = it->find(arrName);
        if (found != it->end()) {
            return found->second.count(idx) > 0;
        }
    }
    return false;
}

bool TypeChecker::isArrayPartiallyInitialized(const std::string& arrName, int64_t totalSize) const {
    for (auto it = uninitArrayElementsStack_.rbegin(); it != uninitArrayElementsStack_.rend(); ++it) {
        auto found = it->find(arrName);
        if (found != it->end()) {
            return !found->second.empty() && static_cast<int64_t>(found->second.size()) < totalSize;
        }
    }
    return false;
}

// ── Type Resolution ─────────────────────────────────────────────────────────

const Type* TypeChecker::resolveNamedType(const std::string& name, SourceLocation loc) {
    if (name.find("::") != std::string::npos) {
        size_t colonPos = name.find("::");
        std::string prefix = name.substr(0, colonPos);
        std::string member = name.substr(colonPos + 2);

        if (!currentModuleScope_) {
            diag_.report(loc, Severity::Error, "unknown module namespace '" + prefix + "'");
            return tc_.getI32();
        }

        auto it = currentModuleScope_->aliasToModule.find(prefix);
        if (it == currentModuleScope_->aliasToModule.end()) {
            diag_.report(loc, Severity::Error, "unknown module alias or namespace '" + prefix + "'");
            return tc_.getI32();
        }

        const ModuleScope* targetMod = it->second;

        // Check exported types
        auto tyIt = targetMod->exportedTypes.find(member);
        if (tyIt != targetMod->exportedTypes.end()) {
            return tyIt->second;
        }

        // Check if it was private in targetMod
        auto privIt = targetMod->types.find(member);
        if (privIt != targetMod->types.end()) {
            diag_.report(loc, Severity::Error, "'" + member + "' is private to module '" + targetMod->logicalName + "'");
            return tc_.getI32();
        }

        diag_.report(loc, Severity::Error, "'" + member + "' is not declared in module '" + targetMod->logicalName + "'");
        return tc_.getI32();
    }

    // Unqualified name: check current module's local types first
    if (currentModuleScope_) {
        auto it = currentModuleScope_->types.find(name);
        if (it != currentModuleScope_->types.end()) {
            return it->second;
        }
    }

    // Check TypeContext global types
    auto sTy = tc_.findRecord(name);
    if (sTy) return sTy;

    auto eTy = tc_.findEnum(name);
    if (eTy) return eTy;

    diag_.report(loc, Severity::Error, "unresolved type: " + name);
    return tc_.getI32();
}

const Symbol* TypeChecker::findSymbol(const std::string& name, SourceLocation loc) {
    if (name.find("::") != std::string::npos) {
        size_t colonPos = name.find("::");
        std::string prefix = name.substr(0, colonPos);
        std::string member = name.substr(colonPos + 2);

        if (prefix == "Option" || prefix == "Result") {
            return nullptr; // Handled as enum variant
        }
        const Type* eTy = tc_.findEnum(prefix);
        if (eTy && eTy->findVariant(member)) {
            return nullptr; // Handled as enum variant
        }

        if (!currentModuleScope_) {
            diag_.report(loc, Severity::Error, "unknown module namespace '" + prefix + "'");
            return nullptr;
        }

        auto it = currentModuleScope_->aliasToModule.find(prefix);
        if (it == currentModuleScope_->aliasToModule.end()) {
            diag_.report(loc, Severity::Error, "unknown module alias or namespace '" + prefix + "'");
            return nullptr;
        }

        const ModuleScope* targetMod = it->second;

        // Check exported symbols
        auto symIt = targetMod->exportedSymbols.find(member);
        if (symIt != targetMod->exportedSymbols.end()) {
            return &symIt->second;
        }

        // Check if it was private in targetMod
        auto privIt = targetMod->symbols.find(member);
        if (privIt != targetMod->symbols.end()) {
            diag_.report(loc, Severity::Error, "'" + member + "' is private to module '" + targetMod->logicalName + "'");
            return nullptr;
        }

        diag_.report(loc, Severity::Error, "'" + member + "' is not declared in module '" + targetMod->logicalName + "'");
        return nullptr;
    }

    return syms_.lookup(name);
}

const Type* TypeChecker::resolveType(const ast::TypeNode& node) {
    switch (node.kind) {
        case TypeNodeKind::Primitive: {
            auto& pt = static_cast<const PrimitiveTypeNode&>(node);
            switch (pt.primKind) {
                case PrimitiveKind::Void:          return tc_.getVoid();
                case PrimitiveKind::Bool:          return tc_.getBool();
                case PrimitiveKind::Char:          return tc_.getU8();   // char is u8
                case PrimitiveKind::Byte:          return tc_.getU8();
                case PrimitiveKind::Short:         return tc_.getI16();
                case PrimitiveKind::Int:           return tc_.getI32();
                case PrimitiveKind::Long:          return tc_.getI64();
                case PrimitiveKind::UnsignedChar:  return tc_.getU8();
                case PrimitiveKind::UnsignedShort: return tc_.getU16();
                case PrimitiveKind::UnsignedInt:   return tc_.getU32();
                case PrimitiveKind::UnsignedLong:  return tc_.getU64();
                case PrimitiveKind::Float:         return tc_.getF32();
                case PrimitiveKind::Double:        return tc_.getF64();
                case PrimitiveKind::SizeT:         return tc_.getUsize();
                case PrimitiveKind::PtrdiffT:      return tc_.getIsize();
                case PrimitiveKind::U8:            return tc_.getU8();
                case PrimitiveKind::U16:           return tc_.getU16();
                case PrimitiveKind::U32:           return tc_.getU32();
                case PrimitiveKind::U64:           return tc_.getU64();
                case PrimitiveKind::U128:
                case PrimitiveKind::I128:
                    diag_.report(pt.loc, Severity::Error,
                                 "128-bit integers are not yet supported in this compiler release");
                    return tc_.getI64();
                case PrimitiveKind::Usize:         return tc_.getUsize();
                case PrimitiveKind::I8:            return tc_.getI8();
                case PrimitiveKind::I16:           return tc_.getI16();
                case PrimitiveKind::I32:           return tc_.getI32();
                case PrimitiveKind::I64:           return tc_.getI64();
                case PrimitiveKind::Isize:         return tc_.getIsize();
                case PrimitiveKind::Float4:        return tc_.getFloat4();
                case PrimitiveKind::Float8:        return tc_.getFloat8();
                case PrimitiveKind::Int4:          return tc_.getInt4();
                case PrimitiveKind::Int8:          return tc_.getInt8();
                case PrimitiveKind::Int16:         return tc_.getInt16();
            }
            break;
        }
        case TypeNodeKind::Array: {
            auto& at = static_cast<const ArrayTypeNode&>(node);
            auto elemTy = resolveType(*at.elementType);
            if (at.sizeExpr) {
                // Fixed size array
                if (at.sizeExpr->kind == ExprKind::IntLiteral) {
                    auto size = static_cast<const IntLiteralExpr*>(at.sizeExpr.get())->value;
                    if (size < 0) {
                        diag_.report(at.sizeExpr->loc, Severity::Error,
                                     "array size cannot be negative: " + std::to_string(size));
                        return tc_.getArray(elemTy, 0, at.isConst);
                    }
                    return tc_.getArray(elemTy, size, at.isConst);
                } else if (at.sizeExpr->kind == ExprKind::Unary &&
                           static_cast<const UnaryExpr*>(at.sizeExpr.get())->op == UnaryOp::Neg &&
                           static_cast<const UnaryExpr*>(at.sizeExpr.get())->operand->kind == ExprKind::IntLiteral) {
                    auto val = static_cast<const IntLiteralExpr*>(static_cast<const UnaryExpr*>(at.sizeExpr.get())->operand.get())->value;
                    int64_t size = -val;
                    diag_.report(at.sizeExpr->loc, Severity::Error,
                                 "array size cannot be negative: " + std::to_string(size));
                    return tc_.getArray(elemTy, 0, at.isConst);
                } else {
                    diag_.report(at.sizeExpr->loc, Severity::Error,
                                 "variable-length arrays are not supported; array size must be a constant integer expression");
                    return tc_.getArray(elemTy, 0, at.isConst);
                }
            }
            // Unsized = view
            return tc_.getArrayView(elemTy, at.isConst);
        }
        case TypeNodeKind::Named: {
            auto& nt = static_cast<const NamedTypeNode&>(node);
            if (!nt.typeArgs.empty()) {
                auto tmplStruct = findGenericStruct(nt.name);
                if (tmplStruct) {
                    std::vector<const Type*> resolvedArgs;
                    for (const auto& ta : nt.typeArgs) resolvedArgs.push_back(resolveType(*ta));
                    return instantiateGenericStruct(*tmplStruct, resolvedArgs, nt.loc);
                }
                auto tmplEnum = findGenericEnum(nt.name);
                if (tmplEnum) {
                    std::vector<const Type*> resolvedArgs;
                    for (const auto& ta : nt.typeArgs) resolvedArgs.push_back(resolveType(*ta));
                    return instantiateGenericEnum(*tmplEnum, resolvedArgs, nt.loc);
                }
                if (nt.name == "Task" && nt.typeArgs.size() == 1) {
                    auto inner = resolveType(*nt.typeArgs[0]);
                    return tc_.getTask(inner);
                }
                if (nt.name == "Option" && nt.typeArgs.size() == 1) {
                    auto inner = resolveType(*nt.typeArgs[0]);
                    bool isNiche = inner && inner->isHandle();
                    std::string optName = "Option<" + inner->toString() + ">";
                    auto existing = tc_.findEnum(optName);
                    if (existing) return existing;
                    std::vector<EnumVariantType> variants;
                    variants.push_back(EnumVariantType{"Some", 0, {inner}});
                    variants.push_back(EnumVariantType{"None", 1, {}});
                    return tc_.getEnum(optName, std::move(variants), isNiche);
                }
                if (nt.name == "Result" && nt.typeArgs.size() == 2) {
                    auto okTy = resolveType(*nt.typeArgs[0]);
                    auto errTy = resolveType(*nt.typeArgs[1]);
                    std::string resName = "Result<" + okTy->toString() + ", " + errTy->toString() + ">";
                    auto existing = tc_.findEnum(resName);
                    if (existing) return existing;
                    std::vector<EnumVariantType> variants;
                    variants.push_back(EnumVariantType{"Ok", 0, {okTy}});
                    variants.push_back(EnumVariantType{"Err", 1, {errTy}});
                    return tc_.getEnum(resName, std::move(variants), false);
                }
            }
            return resolveNamedType(nt.name, nt.loc);
        }
        case TypeNodeKind::Reference: {
            auto& rt = static_cast<const ReferenceTypeNode&>(node);
            auto refTy = resolveType(*rt.referentType);
            return tc_.getReference(refTy, rt.isConst, rt.isPointer);
        }
        case TypeNodeKind::Handle: {
            auto& ht = static_cast<const HandleTypeNode&>(node);
            auto elemTy = resolveType(*ht.innerType);
            return tc_.getHandle(elemTy);
        }
        case TypeNodeKind::Slab: {
            auto& st = static_cast<const SlabTypeNode&>(node);
            auto elemTy = resolveType(*st.elementType);
            int64_t cap = 0;
            if (st.capacityExpr) {
                auto eval = evalIntConstExpr(*st.capacityExpr);
                if (eval.isConst && !eval.overflow && eval.value > 0) {
                    cap = eval.value;
                }
            }
            return tc_.getSlab(elemTy, cap);
        }
        case TypeNodeKind::Auto: {
            return nullptr;
        }
        case TypeNodeKind::Tuple: {
            auto& tt = static_cast<const ast::TupleTypeNode&>(node);
            std::vector<const Type*> elemTypes;
            for (const auto& elem : tt.elements) {
                elemTypes.push_back(resolveType(*elem));
            }
            return tc_.getTuple(std::move(elemTypes));
        }
        case TypeNodeKind::Function: {
            auto& ft = static_cast<const ast::FunctionTypeNode&>(node);
            auto retTy = resolveType(*ft.returnType);
            std::vector<const Type*> paramTypes;
            for (const auto& p : ft.paramTypes) {
                paramTypes.push_back(resolveType(*p));
            }
            return tc_.getFunction(retTy, std::move(paramTypes));
        }
        case TypeNodeKind::Task: {
            auto& tt = static_cast<const ast::TaskTypeNode&>(node);
            auto elemTy = tt.elementType ? resolveType(*tt.elementType) : tc_.getVoid();
            return tc_.getTask(elemTy);
        }
    }
    return tc_.getI32();
}

void TypeChecker::checkCAbiType(const Type* ty, SourceLocation loc) {
    if (!ty) return;
    if (ty->isFunction()) {
        diag_.report(loc, Severity::Error,
                     "closure type '" + ty->toString() + "' has a 16-byte fat-pointer layout and cannot cross C ABI boundaries directly");
        return;
    }
    if (ty->isTask()) {
        diag_.report(loc, Severity::Error,
                     "task type '" + ty->toString() + "' cannot cross C ABI boundaries directly");
        return;
    }
    if (ty->isArrayView()) {
        diag_.report(loc, Severity::Error,
                     "slice type '" + ty->toString() + "' has a 16-byte fat-pointer layout and cannot cross C ABI boundaries directly; use raw pointer (*T) instead");
        return;
    }
    if (ty->isReference() || ty->isArray()) {
        checkCAbiType(ty->elementType, loc);
    }
}

void TypeChecker::checkFunctionCAbi(const ast::FunctionDecl& fn) {
    bool isCAbi = fn.isExported || (fn.isExtern && (fn.linkage == "C" || fn.linkage.empty()));
    if (!isCAbi) return;

    if (fn.isAsync) {
        diag_.report(fn.loc, Severity::Error,
                     "async function '" + fn.name + "' cannot have C linkage or be exported directly across C ABI boundary");
        return;
    }

    if (fn.returnType) {
        auto retTy = resolveType(*fn.returnType);
        checkCAbiType(retTy, fn.returnType->loc);
    }
    for (const auto& p : fn.params) {
        if (p.type) {
            auto pTy = resolveType(*p.type);
            if (p.isArray) {
                pTy = tc_.getArrayView(pTy, p.isConst);
            }
            checkCAbiType(pTy, p.loc);
        }
    }
}

void TypeChecker::checkStructCAbi(const ast::StructDecl& sd) {
    if (sd.layout != ast::LayoutDirective::CLayout) return;

    for (const auto& f : sd.fields) {
        if (f.type) {
            auto fTy = resolveType(*f.type);
            checkCAbiType(fTy, f.loc);
        }
    }
    for (const auto& m : sd.methods) {
        if (m.returnType) {
            auto mRet = resolveType(*m.returnType);
            checkCAbiType(mRet, m.returnType->loc);
        }
        for (const auto& p : m.params) {
            if (p.type) {
                auto pTy = resolveType(*p.type);
                if (p.isArray) {
                    pTy = tc_.getArrayView(pTy, p.isConst);
                }
                checkCAbiType(pTy, p.loc);
            }
        }
    }
}

// ── Expression Checking ─────────────────────────────────────────────────────

const Type* TypeChecker::checkExpr(ast::Expr& expr) {
    if (++exprDepth_ > MAX_EXPR_DEPTH) {
        diag_.report(expr.loc, Severity::Error,
                     "expression nesting depth exceeds maximum supported limit of " +
                     std::to_string(MAX_EXPR_DEPTH));
        --exprDepth_;
        return tc_.getI32();
    }
    struct ExprDepthScope {
        int& d;
        ~ExprDepthScope() { --d; }
    } scope{exprDepth_};

    const Type* ty = nullptr;
    switch (expr.kind) {
        case ExprKind::IntLiteral:   ty = checkIntLiteral(static_cast<IntLiteralExpr&>(expr)); break;
        case ExprKind::FloatLiteral: ty = checkFloatLiteral(static_cast<FloatLiteralExpr&>(expr)); break;
        case ExprKind::BoolLiteral:  ty = checkBoolLiteral(static_cast<BoolLiteralExpr&>(expr)); break;
        case ExprKind::StringLiteral: ty = checkStringLiteral(static_cast<StringLiteralExpr&>(expr)); break;
        case ExprKind::Identifier:   ty = checkIdentifier(static_cast<IdentifierExpr&>(expr)); break;
        case ExprKind::Binary:       ty = checkBinary(static_cast<BinaryExpr&>(expr)); break;
        case ExprKind::Unary:        ty = checkUnary(static_cast<UnaryExpr&>(expr)); break;
        case ExprKind::Assign:       ty = checkAssign(static_cast<AssignExpr&>(expr)); break;
        case ExprKind::Call:         ty = checkCall(static_cast<CallExpr&>(expr)); break;
        case ExprKind::MethodCall:   ty = checkMethodCall(static_cast<MethodCallExpr&>(expr)); break;
        case ExprKind::Index:        ty = checkIndex(static_cast<IndexExpr&>(expr)); break;
        case ExprKind::Field:        ty = checkField(static_cast<FieldExpr&>(expr)); break;
        case ExprKind::Cast:         ty = checkCast(static_cast<CastExpr&>(expr)); break;
        case ExprKind::ArrayInit:    ty = checkArrayInit(static_cast<ArrayInitExpr&>(expr)); break;
        case ExprKind::CompoundInit: ty = checkCompoundInit(static_cast<CompoundInitExpr&>(expr)); break;
        case ExprKind::This:         ty = checkThis(static_cast<ThisExpr&>(expr)); break;
        case ExprKind::Construct:    ty = checkConstruct(static_cast<ConstructExpr&>(expr)); break;
        case ExprKind::Match:        ty = checkMatchExpr(static_cast<MatchExpr&>(expr)); break;
        case ExprKind::Try:          ty = checkTryExpr(static_cast<TryExpr&>(expr)); break;
        case ExprKind::Ternary:      ty = checkTernary(static_cast<TernaryExpr&>(expr)); break;
        case ExprKind::Tuple:        ty = checkTuple(static_cast<TupleExpr&>(expr)); break;
        case ExprKind::TupleIndex:   ty = checkTupleIndex(static_cast<TupleIndexExpr&>(expr)); break;
        case ExprKind::Lambda:       ty = checkLambda(static_cast<LambdaExpr&>(expr)); break;
        case ExprKind::Range:        ty = checkRange(static_cast<RangeExpr&>(expr)); break;
        case ExprKind::Await:        ty = checkAwait(static_cast<AwaitExpr&>(expr)); break;
    }
    setExprType(&expr, ty);
    return ty;
}

const Type* TypeChecker::checkIntLiteral(IntLiteralExpr& expr) {
    if (expr.value > INT32_MAX || expr.value < INT32_MIN) {
        return tc_.getI64();
    }
    return tc_.getI32();
}

const Type* TypeChecker::checkFloatLiteral(FloatLiteralExpr& expr) {
    if (expr.isSinglePrecision) {
        return tc_.getF32();
    }
    return tc_.getF64();
}

const Type* TypeChecker::checkBoolLiteral(BoolLiteralExpr& /*expr*/) {
    return tc_.getBool();
}

const Type* TypeChecker::checkStringLiteral(StringLiteralExpr& /*expr*/) {
    return tc_.getArrayView(tc_.getU8(), /*isConst=*/true);
}

const Type* TypeChecker::checkIdentifier(IdentifierExpr& expr) {
    auto it = exprTypes_.find(&expr);
    if (it != exprTypes_.end()) return it->second;

    if (expr.name.find("::") != std::string::npos) {
        size_t colon = expr.name.find("::");
        std::string eName = expr.name.substr(0, colon);
        std::string vName = expr.name.substr(colon + 2);
        if (eName == "Option" && vName == "None") {
            std::vector<EnumVariantType> variants;
            variants.push_back(EnumVariantType{"Some", 0, {tc_.getI32()}});
            variants.push_back(EnumVariantType{"None", 1, {}});
            return tc_.getEnum("Option<int>", std::move(variants), false);
        }
        const Type* eTy = tc_.findEnum(eName);
        if (eTy) {
            auto v = eTy->findVariant(vName);
            if (v) return eTy;
        }
    }

    auto sym = findSymbol(expr.name, expr.loc);
    if (!sym) {
        if (expr.name.find("::") != std::string::npos) {
            return tc_.getI32(); // already reported in findSymbol
        }
        if (currentRecord_) {
            auto f = currentRecord_->findField(expr.name);
            if (f) {
                ProvenanceSet thisProv(ProvenanceKind::ThisBorrow, 0, "this");
                setExprProvenance(&expr, thisProv);
                return f->type ? f->type : tc_.getI32();
            }
        }
        diag_.report(expr.loc, Severity::Error,
                     "use of undeclared identifier '" + expr.name + "'");
        return tc_.getI32(); // recovery
    }

    if (!sym->mangledName.empty()) {
        expr.name = sym->mangledName;
    }

    if (!sym->isFunction && !sym->isParameter && !sym->isConst && !sym->type->isArray() &&
        !sym->type->isRef() && !isAssigned(expr.name)) {
        if (isMoved(expr.name)) {
            diag_.report(expr.loc, Severity::Error,
                         "use of moved variable '" + expr.name + "'");
        } else {
            diag_.report(expr.loc, Severity::Error,
                         "variable '" + expr.name + "' is used before being initialized");
        }
    }

    ProvenanceSet pSet = getVarProvenance(sym->name);
    if (pSet.empty() && !sym->provenance.empty()) {
        pSet = sym->provenance;
    }
    if (pSet.empty() && sym->regionId > 0) {
        pSet = ProvenanceSet(ProvenanceKind::LocalRegion, sym->regionId);
    }
    if (!pSet.empty()) {
        setExprProvenance(&expr, pSet);
    }

    if (!activeLambdaStack_.empty() && sym && !sym->isFunction && !sym->isGlobal) {
        size_t sDepth = syms_.lookupScopeDepth(expr.name);
        for (auto& lctx : activeLambdaStack_) {
            if (sDepth > 1 && sDepth < lctx.scopeDepth) {
                if (!lctx.captures.count(expr.name)) {
                    lctx.captures[expr.name] = LambdaCapture{expr.name, sym->type, false, sDepth, expr.loc};
                }
            }
        }
    }

    const Type* retTy = sym->type;
    if (sym->isConst && !retTy->isConst) {
        if (retTy->isArray()) {
            retTy = tc_.getArray(retTy->elementType, retTy->arraySize, true);
        } else if (retTy->isArrayView()) {
            retTy = tc_.getArrayView(retTy->elementType, true);
        }
    }
    return retTy;
}

const Type* TypeChecker::checkBinary(BinaryExpr& expr) {
    // Collect the left spine of binary expressions to avoid deep recursion
    std::vector<BinaryExpr*> spine;
    BinaryExpr* curr = &expr;
    while (curr->lhs && curr->lhs->kind == ExprKind::Binary) {
        spine.push_back(curr);
        curr = static_cast<BinaryExpr*>(curr->lhs.get());
    }

    if (!spine.empty()) {
        auto leftTy = checkExpr(*curr->lhs);
        auto rightTy = checkExpr(*curr->rhs);
        auto accTy = checkBinaryOpTypes(*curr, leftTy, rightTy);
        setExprType(curr, accTy);

        for (auto it = spine.rbegin(); it != spine.rend(); ++it) {
            BinaryExpr* b = *it;
            auto rTy = checkExpr(*b->rhs);
            accTy = checkBinaryOpTypes(*b, accTy, rTy);
            setExprType(b, accTy);
        }
        return accTy;
    }

    auto lhsTy = checkExpr(*expr.lhs);
    auto rhsTy = checkExpr(*expr.rhs);
    return checkBinaryOpTypes(expr, lhsTy, rhsTy);
}

const Type* TypeChecker::checkBinaryOpTypes(BinaryExpr& expr, const Type* lhsTy, const Type* rhsTy) {
    if (!lhsTy || !rhsTy) return tc_.getI32();

    // Check for overloaded operator on record (struct/class) types
    const Type* recTy = lhsTy;
    if (recTy && recTy->isReference()) recTy = recTy->elementType;
    if (recTy && recTy->isRecord()) {
        std::string opMethodName;
        switch (expr.op) {
            case BinaryOp::Add:    opMethodName = "operator+"; break;
            case BinaryOp::Sub:    opMethodName = "operator-"; break;
            case BinaryOp::Mul:    opMethodName = "operator*"; break;
            case BinaryOp::Div:    opMethodName = "operator/"; break;
            case BinaryOp::Mod:    opMethodName = "operator%"; break;
            case BinaryOp::EqEq:   opMethodName = "operator=="; break;
            case BinaryOp::NotEq:  opMethodName = "operator!="; break;
            case BinaryOp::Lt:     opMethodName = "operator<"; break;
            case BinaryOp::LtEq:   opMethodName = "operator<="; break;
            case BinaryOp::Gt:     opMethodName = "operator>"; break;
            case BinaryOp::GtEq:   opMethodName = "operator>="; break;
            case BinaryOp::BitAnd: opMethodName = "operator&"; break;
            case BinaryOp::BitOr:  opMethodName = "operator|"; break;
            case BinaryOp::BitXor: opMethodName = "operator^"; break;
            case BinaryOp::Shl:    opMethodName = "operator<<"; break;
            case BinaryOp::Shr:    opMethodName = "operator>>"; break;
            default: break;
        }
        if (!opMethodName.empty()) {
            const MethodType* m = recTy->findMethod(opMethodName, 1);
            if (m && m->paramTypes.size() == 1) {
                const Type* expectedRhs = m->paramTypes[0];
                const Type* actualRhs = rhsTy;
                if (expectedRhs->isReference()) expectedRhs = expectedRhs->elementType;
                if (actualRhs->isReference()) actualRhs = actualRhs->elementType;
                if (*expectedRhs == *actualRhs || actualRhs->isImplicitlyConvertibleTo(*expectedRhs)) {
                    expr.overloadedMethod = opMethodName;
                    setExprType(&expr, m->returnType);
                    return m->returnType;
                }
            }
        }
    }

    if (lhsTy->isSIMDVector() || rhsTy->isSIMDVector()) {
        if (lhsTy->isSIMDVector() && rhsTy->isSIMDVector()) {
            if (*lhsTy == *rhsTy) {
                return lhsTy;
            }
            diag_.report(expr.loc, Severity::Error,
                         "SIMD vector binary operations require identical vector types: " +
                         lhsTy->toString() + " vs " + rhsTy->toString());
            return lhsTy;
        }
        if (lhsTy->isSIMDVector() && rhsTy->isNumeric()) {
            return lhsTy;
        }
        if (lhsTy->isNumeric() && rhsTy->isSIMDVector()) {
            return rhsTy;
        }
        diag_.report(expr.loc, Severity::Error,
                     "invalid binary operation with SIMD vector and " +
                     (lhsTy->isSIMDVector() ? rhsTy->toString() : lhsTy->toString()));
        return lhsTy->isSIMDVector() ? lhsTy : rhsTy;
    }

    // Comparison operators -> bool result
    switch (expr.op) {
        case BinaryOp::Lt:
        case BinaryOp::Gt:
        case BinaryOp::LtEq:
        case BinaryOp::GtEq:
        case BinaryOp::EqEq:
        case BinaryOp::NotEq:
            if (*lhsTy != *rhsTy && !lhsTy->isImplicitlyConvertibleTo(*rhsTy) &&
                !rhsTy->isImplicitlyConvertibleTo(*lhsTy)) {
                if (lhsTy->isNumeric() && rhsTy->isNumeric()) {
                    return tc_.getBool();
                }
                if (lhsTy->isPointer() && rhsTy->isPointer()) {
                    return tc_.getBool();
                }
                if ((expr.op == BinaryOp::EqEq || expr.op == BinaryOp::NotEq) &&
                    ((lhsTy->isPointer() && rhsTy->isInteger()) || (rhsTy->isPointer() && lhsTy->isInteger()))) {
                    return tc_.getBool();
                }
                diag_.report(expr.loc, Severity::Error,
                             "type mismatch in comparison: " + lhsTy->toString() +
                             " vs " + rhsTy->toString());
            }
            return tc_.getBool();

        case BinaryOp::And:
        case BinaryOp::Or:
            if (!lhsTy->isBool() || !rhsTy->isBool()) {
                diag_.report(expr.loc, Severity::Error,
                             "logical operators require bool operands");
            }
            return tc_.getBool();

        default:
            // Pointer arithmetic
            if (lhsTy->isPointer() || rhsTy->isPointer()) {
                if (expr.op == BinaryOp::Add) {
                    if (lhsTy->isPointer() && rhsTy->isInteger()) return lhsTy;
                    if (lhsTy->isInteger() && rhsTy->isPointer()) return rhsTy;
                }
                if (expr.op == BinaryOp::Sub) {
                    if (lhsTy->isPointer() && rhsTy->isInteger()) return lhsTy;
                    if (lhsTy->isPointer() && rhsTy->isPointer()) return tc_.getI64();
                }
                diag_.report(expr.loc, Severity::Error, "invalid binary operation on pointer types");
                return lhsTy->isPointer() ? lhsTy : rhsTy;
            }

            // Shift operations: result type is always the type of the left operand
            if (expr.op == BinaryOp::Shl || expr.op == BinaryOp::Shr) {
                if (!lhsTy->isInteger() || !rhsTy->isInteger()) {
                    diag_.report(expr.loc, Severity::Error, "shift operators require integer operands");
                }
                return lhsTy;
            }

            // Bitwise operations (BitAnd, BitOr, BitXor) and Mod: require integer operands
            if (expr.op == BinaryOp::BitAnd || expr.op == BinaryOp::BitOr ||
                expr.op == BinaryOp::BitXor || expr.op == BinaryOp::Mod) {
                if (!lhsTy->isInteger() || !rhsTy->isInteger()) {
                    std::string opStr = (expr.op == BinaryOp::Mod) ? "%" :
                                        (expr.op == BinaryOp::BitAnd ? "&" :
                                         (expr.op == BinaryOp::BitOr ? "|" : "^"));
                    diag_.report(expr.loc, Severity::Error,
                                 "operator '" + opStr + "' requires integer operands, but got " +
                                 lhsTy->toString() + " and " + rhsTy->toString());
                    return lhsTy;
                }
            }

            // Arithmetic and bitwise: operands are promoted to common arithmetic type
            if (*lhsTy != *rhsTy) {
                if ((lhsTy->isInteger() && rhsTy->isInteger()) ||
                    (lhsTy->isNumeric() && rhsTy->isNumeric())) {
                    return tc_.getCommonArithmeticType(lhsTy, rhsTy);
                }
                if (lhsTy->isImplicitlyConvertibleTo(*rhsTy)) {
                    return rhsTy;
                }
                if (rhsTy->isImplicitlyConvertibleTo(*lhsTy)) {
                    return lhsTy;
                }
                diag_.report(expr.loc, Severity::Error,
                             "type mismatch in binary operation: " +
                             lhsTy->toString() + " vs " + rhsTy->toString());
                return lhsTy;
            }

            if (!lhsTy->isNumeric() && !lhsTy->isSIMDVector()) {
                diag_.report(expr.loc, Severity::Error,
                             "binary operator not supported for type '" + lhsTy->toString() + "'");
                return lhsTy;
            }
            return lhsTy;
    }
}

const Type* TypeChecker::checkUnary(UnaryExpr& expr) {
    auto opTy = checkExpr(*expr.operand);
    if (!opTy) return tc_.getI32();

    const Type* recTy = opTy;
    if (recTy && recTy->isReference()) recTy = recTy->elementType;
    if (recTy && recTy->isRecord()) {
        std::string opMethodName;
        switch (expr.op) {
            case UnaryOp::Neg:    opMethodName = "operator-"; break;
            case UnaryOp::Not:    opMethodName = "operator!"; break;
            case UnaryOp::BitNot: opMethodName = "operator~"; break;
            default: break;
        }
        if (!opMethodName.empty()) {
            const MethodType* m = recTy->findMethod(opMethodName, 0);
            if (m && m->paramTypes.empty()) {
                expr.overloadedMethod = opMethodName;
                setExprType(&expr, m->returnType);
                return m->returnType;
            }
        }
    }

    switch (expr.op) {
        case UnaryOp::Neg:
            if (!opTy->isNumeric()) {
                diag_.report(expr.loc, Severity::Error, "cannot negate non-numeric type");
            }
            return opTy;
        case UnaryOp::Not:
            if (!opTy->isBool()) {
                diag_.report(expr.loc, Severity::Error, "logical not requires bool");
            }
            return tc_.getBool();
        case UnaryOp::BitNot:
            if (!opTy->isInteger()) {
                diag_.report(expr.loc, Severity::Error, "bitwise not requires integer");
            }
            return opTy;
        case UnaryOp::PreInc:
        case UnaryOp::PreDec:
        case UnaryOp::PostInc:
        case UnaryOp::PostDec:
            if (!opTy->isInteger()) {
                diag_.report(expr.loc, Severity::Error, "increment/decrement requires integer");
            }
            if (expr.operand->kind == ExprKind::Identifier) {
                auto& id = static_cast<IdentifierExpr&>(*expr.operand);
                if (parallelLoopScopeDepth_ > 0 && !parallelLoopInductionVars_.count(id.name) && !activeReductionVars_.count(id.name)) {
                    size_t symDepth = syms_.lookupScopeDepth(id.name);
                    if (symDepth > 0 && symDepth < parallelLoopScopeDepth_) {
                        diag_.report(expr.loc, Severity::Error,
                                     "data-race safety violation: mutation of outer scalar variable '" + id.name +
                                     "' is disallowed inside parallel loop; use a parallel reduction or lane-local variable");
                    }
                }
                auto sym = syms_.lookup(id.name);
                if (sym && sym->isConst) {
                    diag_.report(expr.loc, Severity::Error, "cannot modify const variable '" + id.name + "'");
                }
                if (!isAssigned(id.name)) {
                    diag_.report(id.loc, Severity::Error, "variable '" + id.name + "' is used before being initialized");
                    if (isMoved(id.name)) {
                        diag_.report(id.loc, Severity::Error, "use of moved variable '" + id.name + "'");
                    } else {
                        diag_.report(id.loc, Severity::Error, "variable '" + id.name + "' is used before being initialized");
                    }
                }
                if (!activeLambdaStack_.empty() && sym && !sym->isFunction && !sym->isGlobal) {
                    size_t symDepth = syms_.lookupScopeDepth(id.name);
                    for (auto& lctx : activeLambdaStack_) {
                        if (symDepth > 1 && symDepth < lctx.scopeDepth) {
                            lctx.captures[id.name] = LambdaCapture{id.name, sym->type, true, symDepth, id.loc};
                        }
                    }
                }
            }
            return opTy;
        case UnaryOp::AddrOf: {
            if (!activeLambdaStack_.empty()) {
                auto [tStr, rTarget] = extractBorrowTarget(expr.operand.get());
                if (!rTarget.empty()) {
                    size_t symDepth = syms_.lookupScopeDepth(rTarget);
                    auto sym = syms_.lookup(rTarget);
                    if (sym && !sym->isFunction && !sym->isGlobal) {
                        for (auto& lctx : activeLambdaStack_) {
                            if (symDepth > 1 && symDepth < lctx.scopeDepth) {
                                lctx.captures[rTarget] = LambdaCapture{rTarget, sym->type, true, symDepth, expr.loc};
                            }
                        }
                    }
                }
            }
            if (expr.operand->kind == ExprKind::Index) {
                auto& idx = static_cast<IndexExpr&>(*expr.operand);
                auto arrTy = getExprType(idx.array.get());
                if (arrTy && arrTy->isArray() && arrTy->elementType && arrTy->elementType->isRecord()) {
                    if (arrTy->elementType->layoutDirective == 2 /* Scan / SoA */) {
                        diag_.report(expr.loc, Severity::Error,
                                     "cannot take address of element in Structure-of-Arrays (@layout(scan)) storage; logical elements are not stored contiguously");
                    }
                }
            }
            ProvenanceSet opProv = getExprProvenance(expr.operand.get());
            if (opProv.empty()) {
                uint64_t opRid = getExprRegionProvenance(expr.operand.get());
                if (opRid > 0) opProv = ProvenanceSet(ProvenanceKind::LocalRegion, opRid);
            }
            if (opProv.empty() && expr.operand->kind == ExprKind::Index) {
                auto& idx = static_cast<IndexExpr&>(*expr.operand);
                opProv = getExprProvenance(idx.array.get());
                if (opProv.empty()) {
                    uint64_t arrRid = getExprRegionProvenance(idx.array.get());
                    if (arrRid > 0) opProv = ProvenanceSet(ProvenanceKind::LocalRegion, arrRid);
                }
            }
            if (opProv.empty() && expr.operand->kind == ExprKind::Field) {
                auto& fe = static_cast<FieldExpr&>(*expr.operand);
                opProv = getExprProvenance(fe.object.get());
                if (opProv.empty()) {
                    uint64_t objRid = getExprRegionProvenance(fe.object.get());
                    if (objRid > 0) opProv = ProvenanceSet(ProvenanceKind::LocalRegion, objRid);
                }
            }
            if (opProv.empty()) {
                auto [targetStr, rootName] = extractBorrowTarget(expr.operand.get());
                if (!rootName.empty()) {
                    auto sym = syms_.lookup(rootName);
                    if (sym) {
                        size_t sDepth = syms_.lookupScopeDepth(rootName);
                        if (sym->isParameter) {
                            opProv = ProvenanceSet(ProvenanceKind::ParameterBorrow, sDepth, rootName);
                        } else if (sym->regionId > 0) {
                            opProv = ProvenanceSet(ProvenanceKind::LocalRegion, sym->regionId, rootName);
                        } else if (sDepth == 0) {
                            opProv = ProvenanceSet(ProvenanceKind::StaticStorage, 0, rootName);
                        } else {
                            opProv = ProvenanceSet(ProvenanceKind::LocalStack, sDepth, rootName);
                        }
                    }
                }
            }
            if (!opProv.empty()) {
                setExprProvenance(&expr, opProv);
                if (opProv.hasRegion()) {
                    setExprRegionProvenance(&expr, opProv.primaryRegionId());
                }
            }
            return tc_.getReference(opTy, false, /*isPointer=*/true);
        }
        case UnaryOp::Deref: {
            ProvenanceSet opProv = getExprProvenance(expr.operand.get());
            if (opProv.empty()) {
                uint64_t opRid = getExprRegionProvenance(expr.operand.get());
                if (opRid > 0) opProv = ProvenanceSet(ProvenanceKind::LocalRegion, opRid);
            }
            if (!opProv.empty()) {
                setExprProvenance(&expr, opProv);
                if (opProv.hasRegion()) {
                    setExprRegionProvenance(&expr, opProv.primaryRegionId());
                }
            }
            return opTy->isReference() ? opTy->elementType : opTy;
        }
    }
    return opTy;
}

const Type* TypeChecker::checkAssign(AssignExpr& expr) {
    const Type* targetTy = nullptr;
    if (expr.target->kind == ExprKind::Identifier) {
        auto& id = static_cast<IdentifierExpr&>(*expr.target);
        auto sym = syms_.lookup(id.name);
        if (!sym && currentRecord_) {
            auto f = currentRecord_->findField(id.name);
            if (f) {
                if (isCurrentMethodConst_) {
                    diag_.report(expr.loc, Severity::Error,
                                 "cannot modify field '" + id.name + "' in const method");
                }
                if (f->isConst) {
                    diag_.report(expr.loc, Severity::Error,
                                 "cannot assign to const field '" + id.name + "'");
                }
                targetTy = f->type;
                setExprType(expr.target.get(), targetTy);
            }
        }
        if (!sym && !targetTy) {
            diag_.report(expr.loc, Severity::Error,
                         "use of undeclared identifier '" + id.name + "'");
            return tc_.getI32();
        }
        if (sym) {
            if (!activeLambdaStack_.empty() && !sym->isFunction && !sym->isGlobal) {
                size_t symDepth = syms_.lookupScopeDepth(id.name);
                for (auto& lctx : activeLambdaStack_) {
                    if (symDepth > 1 && symDepth < lctx.scopeDepth) {
                        lctx.captures[id.name] = LambdaCapture{id.name, sym->type, true, symDepth, id.loc};
                    }
                }
            }
            if (parallelLoopScopeDepth_ > 0 && !parallelLoopInductionVars_.count(id.name) && !activeReductionVars_.count(id.name)) {
                size_t symDepth = syms_.lookupScopeDepth(id.name);
                if (symDepth > 0 && symDepth < parallelLoopScopeDepth_) {
                    diag_.report(expr.loc, Severity::Error,
                                 "data-race safety violation: mutation of outer scalar variable '" + id.name +
                                 "' is disallowed inside parallel loop; use a parallel reduction or lane-local variable");
                }
            }
            if (sym->isConst) {
                diag_.report(expr.loc, Severity::Error,
                             "cannot assign to const variable '" + id.name + "'");
            }
            if (sym->type->isRef() && sym->type->isConstRef) {
                diag_.report(expr.loc, Severity::Error,
                             "cannot assign to const reference '" + id.name + "'");
            }
            if (expr.op != AssignOp::Assign && !sym->type->isRef() && !isAssigned(id.name)) {
                diag_.report(expr.loc, Severity::Error,
                             "variable '" + id.name + "' is used before being initialized");
                if (isMoved(id.name)) {
                    diag_.report(expr.loc, Severity::Error,
                                 "use of moved variable '" + id.name + "'");
                } else {
                    diag_.report(expr.loc, Severity::Error,
                                 "variable '" + id.name + "' is used before being initialized");
                }
            }
            targetTy = sym->type->isRef() ? sym->type->elementType : sym->type;
            setExprType(expr.target.get(), targetTy);
        }
    } else if (expr.target->kind == ExprKind::Field) {
        auto& fe = static_cast<FieldExpr&>(*expr.target);
        if (expr.op == AssignOp::Assign) {
            if (fe.object->kind == ExprKind::Identifier) {
                auto& bId = static_cast<IdentifierExpr&>(*fe.object);
                markFieldAssigned(bId.name, fe.fieldName);
            } else if (fe.object->kind == ExprKind::Index) {
                auto& innerIdx = static_cast<IndexExpr&>(*fe.object);
                const ast::Expr* rootArr = innerIdx.array.get();
                while (rootArr && rootArr->kind == ExprKind::Index) {
                    rootArr = static_cast<const IndexExpr*>(rootArr)->array.get();
                }
                if (rootArr && rootArr->kind == ExprKind::Identifier) {
                    auto& id = static_cast<const IdentifierExpr&>(*rootArr);
                    if (innerIdx.array.get() == rootArr && innerIdx.index->kind == ExprKind::IntLiteral) {
                        markArrayElementAssigned(id.name, static_cast<const IntLiteralExpr&>(*innerIdx.index).value);
                    } else {
                        markAllArrayElementsAssigned(id.name);
                    }
                }
            }
        }
        targetTy = checkField(fe);
        setExprType(expr.target.get(), targetTy);

        if (fe.object->kind == ExprKind::Identifier) {
            auto& bId = static_cast<IdentifierExpr&>(*fe.object);
            auto sym = syms_.lookup(bId.name);
            if (sym && sym->isConst) {
                diag_.report(expr.loc, Severity::Error,
                             "cannot assign to field '" + fe.fieldName + "' of const struct '" + bId.name + "'");
            }
        }
        auto baseTy = getExprType(fe.object.get());
        if (baseTy && baseTy->isReference()) baseTy = baseTy->elementType;
        if (baseTy && baseTy->isRecord()) {
            auto f = baseTy->findField(fe.fieldName);
            if (f && f->isConst && (!f->type || !f->type->isArrayView())) {
                diag_.report(expr.loc, Severity::Error,
                             "cannot assign to const struct field '" + fe.fieldName + "'");
            }
        }
    } else if (expr.target->kind == ExprKind::Index) {
        auto& idx = static_cast<IndexExpr&>(*expr.target);
        if (expr.op == AssignOp::Assign) {
            const ast::Expr* rootArr = idx.array.get();
            while (rootArr && rootArr->kind == ExprKind::Index) {
                rootArr = static_cast<const IndexExpr*>(rootArr)->array.get();
            }
            if (rootArr && rootArr->kind == ExprKind::Identifier) {
                auto& id = static_cast<const IdentifierExpr&>(*rootArr);
                if (idx.array.get() == rootArr && idx.index->kind == ExprKind::IntLiteral) {
                    markArrayElementAssigned(id.name, static_cast<const IntLiteralExpr&>(*idx.index).value);
                } else {
                    markAllArrayElementsAssigned(id.name);
                }
            }
        }
        if (idx.array->kind == ExprKind::StringLiteral) {
            diag_.report(expr.loc, Severity::Error, "cannot modify string literal");
        }
        auto arrTy = checkExpr(*idx.array);
        if (arrTy && arrTy->isConst) {
            diag_.report(expr.loc, Severity::Error, "cannot modify element of const array or string literal view");
        }
        if (idx.array->kind == ExprKind::Identifier) {
            auto& id = static_cast<IdentifierExpr&>(*idx.array);
            auto sym = syms_.lookup(id.name);
            if (sym && sym->isConst) {
                diag_.report(expr.loc, Severity::Error, "cannot assign to element of const variable '" + id.name + "'");
            }
        }
        targetTy = checkIndex(idx);
        setExprType(expr.target.get(), targetTy);

        if (parallelLoopScopeDepth_ > 0 && idx.array->kind == ExprKind::Identifier) {
            auto arrName = static_cast<IdentifierExpr&>(*idx.array).name;
            size_t arrDepth = syms_.lookupScopeDepth(arrName);
            if (arrDepth > 0 && arrDepth < parallelLoopScopeDepth_) {
                parallelLoopWrittenArrays_.insert(arrName);

                bool isDisjoint = false;
                for (const auto& indVar : parallelLoopInductionVars_) {
                    if (idx.index->kind == ExprKind::Identifier) {
                        if (static_cast<IdentifierExpr&>(*idx.index).name == indVar) {
                            isDisjoint = true;
                            break;
                        }
                    } else if (idx.index->kind == ExprKind::Binary) {
                        auto& bin = static_cast<BinaryExpr&>(*idx.index);
                        if (bin.op == BinaryOp::Add || bin.op == BinaryOp::Sub) {
                            if (bin.lhs->kind == ExprKind::Identifier &&
                                static_cast<IdentifierExpr&>(*bin.lhs).name == indVar &&
                                bin.rhs->kind == ExprKind::IntLiteral) {
                                isDisjoint = true;
                                break;
                            } else if (bin.rhs->kind == ExprKind::Identifier &&
                                       static_cast<IdentifierExpr&>(*bin.rhs).name == indVar &&
                                       bin.lhs->kind == ExprKind::IntLiteral &&
                                       bin.op == BinaryOp::Add) {
                                isDisjoint = true;
                                break;
                            }
                        }
                    }
                }
                if (!isDisjoint) {
                    diag_.report(expr.loc, Severity::Error,
                                 "data-race safety violation: array write index in parallel loop is non-disjoint and may cause concurrent writes to the same element");
                }
            }
        }
    } else if (expr.target->kind == ExprKind::Unary) {
        auto& un = static_cast<UnaryExpr&>(*expr.target);
        if (un.op == UnaryOp::Deref) {
            if (parallelLoopScopeDepth_ > 0) {
                diag_.report(expr.loc, Severity::Error,
                             "data-race safety violation: mutation through pointer dereference inside parallel loop is disallowed");
            }
            auto ptrTy = checkExpr(*un.operand);
            if (ptrTy && ptrTy->isReference()) {
                if (ptrTy->isConstRef) {
                    diag_.report(expr.loc, Severity::Error,
                                 "cannot assign through pointer/reference to const");
                }
                targetTy = ptrTy->elementType;
            } else {
                targetTy = ptrTy;
            }
            setExprType(expr.target.get(), targetTy);
        } else {
            targetTy = checkExpr(*expr.target);
        }
    } else {
        targetTy = checkExpr(*expr.target);
    }

    auto valueTy = checkExpr(*expr.value);
    if (!targetTy || !valueTy) return tc_.getI32();

    if (targetTy->isEnum() && targetTy->enumName.rfind("Result<", 0) == 0 && valueTy->isEnum() && valueTy->enumName.rfind("Result<", 0) == 0) {
        setExprType(expr.value.get(), targetTy);
        valueTy = targetTy;
    } else if (targetTy->isEnum() && targetTy->enumName.rfind("Option<", 0) == 0 && valueTy->isEnum() && valueTy->enumName.rfind("Option<", 0) == 0) {
        setExprType(expr.value.get(), targetTy);
        valueTy = targetTy;
    }

    if (targetTy->hasNonTrivialDestructor() && !isRValue(expr.value.get())) {
        std::string typeName = targetTy->isEnum() ? targetTy->enumName : targetTy->structName;
        std::string srcName = (expr.value->kind == ExprKind::Identifier) ? static_cast<IdentifierExpr&>(*expr.value).name : "value";
        diag_.report(expr.loc, Severity::Error,
                     "cannot copy or assign resource-owning class '" + typeName + "'; use 'move(" + srcName + ")' to transfer ownership");
    }

    bool canAssign = false;
    if (expr.op == AssignOp::Assign) {
        canAssign = (*targetTy == *valueTy || valueTy->isImplicitlyConvertibleTo(*targetTy));
        if (!canAssign && targetTy->isPointer() && expr.value->kind == ExprKind::IntLiteral &&
            static_cast<IntLiteralExpr&>(*expr.value).value == 0) {
            canAssign = true;
        }
    } else if (expr.op == AssignOp::AddAssign || expr.op == AssignOp::SubAssign) {
        if (targetTy->isPointer()) {
            if (valueTy->isInteger()) {
                canAssign = true;
            }
        } else if (targetTy->isNumeric() && valueTy->isNumeric()) {
            canAssign = true;
        } else if (targetTy->isRecord()) {
            std::string opMethod = (expr.op == AssignOp::AddAssign) ? "operator+=" : "operator-=";
            auto m = targetTy->findMethod(opMethod);
            if (!m) m = targetTy->findMethod(expr.op == AssignOp::AddAssign ? "operator+" : "operator-");
            if (m && m->paramTypes.size() == 1) {
                canAssign = true;
            }
        }
    } else if (expr.op == AssignOp::MulAssign || expr.op == AssignOp::DivAssign) {
        if (targetTy->isNumeric() && valueTy->isNumeric()) {
            canAssign = true;
        } else if (targetTy->isRecord()) {
            std::string opMethod = (expr.op == AssignOp::MulAssign) ? "operator*=" : "operator/=";
            auto m = targetTy->findMethod(opMethod);
            if (!m) m = targetTy->findMethod(expr.op == AssignOp::MulAssign ? "operator*" : "operator/");
            if (m && m->paramTypes.size() == 1) {
                canAssign = true;
            }
        }
    } else if (expr.op == AssignOp::ModAssign) {
        if (targetTy->isInteger() && valueTy->isInteger()) {
            canAssign = true;
        } else if (targetTy->isRecord()) {
            auto m = targetTy->findMethod("operator%=");
            if (!m) m = targetTy->findMethod("operator%");
            if (m && m->paramTypes.size() == 1) {
                canAssign = true;
            }
        }
    } else if (expr.op == AssignOp::ShlAssign || expr.op == AssignOp::ShrAssign) {
        if (targetTy->isInteger() && valueTy->isInteger()) {
            canAssign = true;
        }
    } else if (expr.op == AssignOp::AndAssign || expr.op == AssignOp::OrAssign || expr.op == AssignOp::XorAssign) {
        if ((targetTy->isInteger() && valueTy->isInteger()) || (targetTy->isBool() && valueTy->isBool())) {
            canAssign = true;
        }
    }

    if (!canAssign) {
        diag_.report(expr.loc, Severity::Error,
                     "cannot apply assignment operator to " + targetTy->toString() + " and " + valueTy->toString());
    }

    std::string rootTarget;
    std::string targetStr;
    if (expr.target->kind == ExprKind::Identifier) {
        rootTarget = static_cast<IdentifierExpr&>(*expr.target).name;
        targetStr = rootTarget;
    } else if (expr.target->kind == ExprKind::Index || expr.target->kind == ExprKind::Field) {
        auto [t, r] = extractBorrowTarget(expr.target.get());
        targetStr = t;
        rootTarget = r;
    }
    if (!rootTarget.empty()) {
        checkBorrowMutation(rootTarget, targetStr, expr.loc);
        if (!activeLambdaStack_.empty()) {
            size_t symDepth = syms_.lookupScopeDepth(rootTarget);
            auto sym = syms_.lookup(rootTarget);
            if (sym && !sym->isFunction && !sym->isGlobal) {
                for (auto& lctx : activeLambdaStack_) {
                    if (symDepth > 1 && symDepth < lctx.scopeDepth) {
                        lctx.captures[rootTarget] = LambdaCapture{rootTarget, sym->type, true, symDepth, expr.loc};
                    }
                }
            }
        }
    }

    ProvenanceSet valProv = getExprProvenance(expr.value.get());
    if (valProv.empty()) {
        uint64_t valRid = getExprRegionProvenance(expr.value.get());
        if (valRid > 0) valProv = ProvenanceSet(ProvenanceKind::LocalRegion, valRid);
    }

    bool targetIsReference = false;
    if (expr.target->kind == ExprKind::Identifier) {
        auto& id = static_cast<IdentifierExpr&>(*expr.target);
        auto sym = syms_.lookup(id.name);
        if (sym && sym->type->isReference()) {
            targetIsReference = true;
        }
    } else if (targetTy && targetTy->isReference()) {
        targetIsReference = true;
    }

    bool canHoldBorrow = targetIsReference || (targetTy && (targetTy->isArrayView() ||
                                               targetTy->isArray() || targetTy->isFunction() || targetTy->canContainBorrow()));

    ProvenanceSet assignProv = valProv;
    if (targetIsReference && assignProv.empty()) {
        if (expr.value->kind == ExprKind::Index) {
            auto& idx = static_cast<IndexExpr&>(*expr.value);
            assignProv = getExprProvenance(idx.array.get());
            if (assignProv.empty()) {
                uint64_t arrRid = getExprRegionProvenance(idx.array.get());
                if (arrRid > 0) assignProv = ProvenanceSet(ProvenanceKind::LocalRegion, arrRid);
            }
        } else if (expr.value->kind == ExprKind::Field) {
            auto& fe = static_cast<FieldExpr&>(*expr.value);
            assignProv = getExprProvenance(fe.object.get());
            if (assignProv.empty()) {
                uint64_t objRid = getExprRegionProvenance(fe.object.get());
                if (objRid > 0) assignProv = ProvenanceSet(ProvenanceKind::LocalRegion, objRid);
            }
        } else if (expr.value->kind == ExprKind::Identifier) {
            auto& id = static_cast<IdentifierExpr&>(*expr.value);
            assignProv = getVarProvenance(id.name);
            if (assignProv.empty()) {
                auto sym = syms_.lookup(id.name);
                if (sym && sym->regionId > 0) {
                    assignProv = ProvenanceSet(ProvenanceKind::LocalRegion, sym->regionId);
                }
            }
        }
    }

    if (canHoldBorrow && !assignProv.empty()) {
        auto path = resolveLValuePath(expr.target.get());
        bool isGlobal = path.isGlobal;
        uint64_t targetRid = path.rootSymbol ? path.rootSymbol->regionId : 0;
        if (!path.isValid) {
            auto targetProv = getExprProvenance(expr.target.get());
            targetRid = targetProv.primaryRegionId();
            if (targetProv.hasStaticStorage()) isGlobal = true;
        }

        std::string reason;
        if (!canAssignProvenance(assignProv, targetRid, isGlobal, reason)) {
            diag_.report(expr.loc, Severity::Error, reason);
        } else {
            if (path.isValid && path.bindingId > 0) {
                if (path.path.empty()) {
                    setVarProvenance(path.bindingId, assignProv);
                } else {
                    setFieldProvenance(path.bindingId, path.path, assignProv);
                }
            } else if (expr.target->kind == ExprKind::Identifier) {
                auto& id = static_cast<IdentifierExpr&>(*expr.target);
                setVarProvenance(id.name, assignProv);
            }

            // Check if assigning a local stack address to an outer scope variable, parameter, or dereference
            size_t targetDepth = SIZE_MAX;
            bool isOutStorage = false;
            if (expr.target->kind == ExprKind::Identifier) {
                auto& id = static_cast<IdentifierExpr&>(*expr.target);
                targetDepth = syms_.lookupScopeDepth(id.name);
                auto sym = syms_.lookup(id.name);
                if (sym && sym->isParameter) {
                    targetDepth = 0;
                    isOutStorage = true;
                }
            } else if (expr.target->kind == ExprKind::Index || expr.target->kind == ExprKind::Field) {
                auto [tStr, rTarget] = extractBorrowTarget(expr.target.get());
                if (!rTarget.empty()) {
                    targetDepth = syms_.lookupScopeDepth(rTarget);
                    auto sym = syms_.lookup(rTarget);
                    if (sym && sym->isParameter) {
                        targetDepth = 0;
                        isOutStorage = true;
                    }
                }
            } else if (expr.target->kind == ExprKind::Unary &&
                       static_cast<UnaryExpr*>(expr.target.get())->op == UnaryOp::Deref) {
                auto& un = static_cast<UnaryExpr&>(*expr.target);
                auto [tStr, rTarget] = extractBorrowTarget(un.operand.get());
                if (!rTarget.empty()) {
                    targetDepth = syms_.lookupScopeDepth(rTarget);
                    auto sym = syms_.lookup(rTarget);
                    if (sym && sym->isParameter) {
                        targetDepth = 0;
                        isOutStorage = true;
                    }
                } else {
                    targetDepth = 0;
                    isOutStorage = true;
                }
                isOutStorage = true;
            }

            if (targetDepth != SIZE_MAX) {
                for (const auto& o : assignProv.origins) {
                    if (o.kind == ProvenanceKind::LocalStack && (o.identity > targetDepth || (isOutStorage && targetDepth == 0))) {
                        if (isOutStorage && expr.target->kind == ExprKind::Unary) {
                            diag_.report(expr.loc, Severity::Error,
                                         "cannot assign pointer or reference to local stack variable '" + o.detail +
                                         "' to outer storage through dereference");
                        } else {
                            diag_.report(expr.loc, Severity::Error,
                                         "cannot assign pointer or reference to variable '" + o.detail +
                                         "' outliving its lexical block scope");
                        }
                    }
                }
            }
        }
    }

    if (targetIsReference && expr.value) {
        std::string tStr, rTarget;
        if (expr.value->kind == ExprKind::Unary &&
            static_cast<UnaryExpr&>(*expr.value).op == UnaryOp::AddrOf) {
            auto& un = static_cast<UnaryExpr&>(*expr.value);
            std::tie(tStr, rTarget) = extractBorrowTarget(un.operand.get());
        } else if (expr.value->kind == ExprKind::Identifier) {
            auto srcName = static_cast<IdentifierExpr&>(*expr.value).name;
            for (const auto& frame : activeBorrowsStack_) {
                for (const auto& b : frame) {
                    if (b.borrower == srcName) {
                        tStr = b.target;
                        rTarget = b.rootTarget;
                        break;
                    }
                }
                if (!tStr.empty()) break;
            }
        }
        if (tStr.empty() && expr.value) {
            ProvenanceSet rhsValProv = getExprProvenance(expr.value.get());
            for (const auto& o : rhsValProv.origins) {
                if ((o.kind == ProvenanceKind::LocalStack || o.kind == ProvenanceKind::ParameterBorrow) &&
                    !o.detail.empty()) {
                    tStr = o.detail;
                    rTarget = o.detail;
                    break;
                }
            }
        }
        std::string borrower;
        if (expr.target->kind == ExprKind::Identifier) {
            borrower = static_cast<IdentifierExpr&>(*expr.target).name;
        }
        if (!tStr.empty()) {
            registerBorrow(borrower, tStr, rTarget, !targetTy->isConstRef, syms_.depth(), expr.loc);
        }
    }

    if (expr.target->kind == ExprKind::Identifier && expr.op == AssignOp::Assign) {
        auto& id = static_cast<IdentifierExpr&>(*expr.target);
        markAssigned(id.name);
        markAllFieldsAssigned(id.name);
        markAllArrayElementsAssigned(id.name);
    }

    if (targetTy && targetTy->isFunction() && !rootTarget.empty()) {
        if (expr.value && expr.value->kind == ExprKind::Lambda) {
            auto* caps = getLambdaCaptures(static_cast<const LambdaExpr*>(expr.value.get()));
            if (caps) {
                for (const auto& cap : *caps) {
                    registerBorrow(rootTarget, cap.name, cap.name, cap.isMutable, syms_.depth(), expr.loc);
                }
            }
        } else if (!valProv.empty()) {
            for (const auto& o : valProv.origins) {
                if (o.kind == ProvenanceKind::LocalStack && !o.detail.empty()) {
                    registerBorrow(rootTarget, o.detail, o.detail, true, syms_.depth(), expr.loc);
                }
            }
        }
        setVarProvenance(rootTarget, valProv);
    }

    return targetTy;
}

static bool isCharBufferOrView(const Type* ty) {
    if (!ty) return false;
    if (ty->isReference()) ty = ty->elementType;
    if (!ty) return false;
    if (ty->isArray() || ty->isArrayView()) {
        return ty->elementType && (ty->elementType->kind == TypeKind::I8 || ty->elementType->kind == TypeKind::U8);
    }
    return false;
}

const Type* TypeChecker::checkCall(CallExpr& expr) {
    if (expr.calleeExpr) {
        auto calleeTy = checkExpr(*expr.calleeExpr);
        if (calleeTy && calleeTy->isFunction()) {
            if (expr.args.size() != calleeTy->paramTypes.size()) {
                diag_.report(expr.loc, Severity::Error,
                             "function pointer expects " +
                             std::to_string(calleeTy->paramTypes.size()) + " arguments, got " +
                             std::to_string(expr.args.size()));
                return calleeTy->returnType ? calleeTy->returnType : tc_.getVoid();
            }
            for (size_t i = 0; i < expr.args.size(); ++i) {
                auto argTy = checkExpr(*expr.args[i]);
                auto paramTy = calleeTy->paramTypes[i];
                if (argTy && paramTy && !argTy->isImplicitlyConvertibleTo(*paramTy)) {
                    diag_.report(expr.args[i]->loc, Severity::Error,
                                 "argument " + std::to_string(i + 1) + " type mismatch in function pointer call: expected " +
                                 paramTy->toString() + ", got " + argTy->toString());
                }
            }
            if (parallelLoopScopeDepth_ > 0) {
                if (expr.calleeExpr->kind == ExprKind::Lambda) {
                    auto* caps = getLambdaCaptures(static_cast<const LambdaExpr*>(expr.calleeExpr.get()));
                    if (caps) {
                        for (const auto& cap : *caps) {
                            if (cap.isMutable && cap.scopeDepth < parallelLoopScopeDepth_) {
                                diag_.report(expr.loc, Severity::Error,
                                             "data-race safety violation: closure with mutable captures cannot be used inside parallel loop; outer variable '" +
                                             cap.name + "' is captured as mutable");
                            }
                        }
                    }
                } else if (expr.calleeExpr->kind == ExprKind::Identifier) {
                    auto calleeName = static_cast<IdentifierExpr&>(*expr.calleeExpr).name;
                    for (const auto& frame : activeBorrowsStack_) {
                        for (const auto& b : frame) {
                            if (b.borrower == calleeName && b.isMutable) {
                                size_t targetDepth = syms_.lookupScopeDepth(b.rootTarget);
                                if (targetDepth > 0 && targetDepth < parallelLoopScopeDepth_) {
                                    diag_.report(expr.loc, Severity::Error,
                                                 "data-race safety violation: closure with mutable captures cannot be used inside parallel loop; outer variable '" +
                                                 b.rootTarget + "' is captured as mutable");
                                }
                            }
                        }
                    }
                }
            }
            checkCallSiteBorrowSafety("<function pointer>", expr.args, calleeTy->paramTypes, expr.loc);
            return calleeTy->returnType ? calleeTy->returnType : tc_.getVoid();
        } else {
            diag_.report(expr.loc, Severity::Error, "called object is not a function or function pointer");
            return tc_.getI32();
        }
    }

    if (expr.callee == "move") {
        if (expr.args.size() != 1) {
            diag_.report(expr.loc, Severity::Error, "move() expects exactly 1 argument, got " + std::to_string(expr.args.size()));
            return tc_.getI32();
        }
        if (expr.args[0]->kind != ExprKind::Identifier) {
            diag_.report(expr.loc, Severity::Error, "cannot move non-lvalue expression; argument to move() must be a variable");
            return checkExpr(*expr.args[0]);
        }
        auto& id = static_cast<IdentifierExpr&>(*expr.args[0]);
        auto sym = syms_.lookup(id.name);
        if (!sym) {
            diag_.report(id.loc, Severity::Error, "use of undeclared identifier '" + id.name + "'");
            return tc_.getI32();
        }
        const Type* varTy = sym->type->isReference() ? sym->type->elementType : sym->type;
        setExprType(&id, varTy);

        // Check if actively borrowed
        checkBorrowMove(id.name, expr.loc);

        // Check if already moved or uninitialized
        if (!isAssigned(id.name)) {
            if (isMoved(id.name)) {
                diag_.report(expr.loc, Severity::Error, "use of moved variable '" + id.name + "'");
            } else {
                diag_.report(expr.loc, Severity::Error, "variable '" + id.name + "' is used before being initialized");
            }
        }

        markMoved(id.name);
        setExprType(&expr, varTy);
        return varTy;
    }

    if (expr.callee.find("::") != std::string::npos) {
        size_t colon = expr.callee.find("::");
        std::string eName = expr.callee.substr(0, colon);
        std::string vName = expr.callee.substr(colon + 2);

        if (eName == "Option" && vName == "Some" && expr.args.size() == 1) {
            auto valTy = checkExpr(*expr.args[0]);
            if (valTy && valTy->hasNonTrivialDestructor() && !isRValue(expr.args[0].get())) {
                std::string typeName = valTy->isEnum() ? valTy->enumName : valTy->structName;
                std::string srcName = (expr.args[0]->kind == ExprKind::Identifier) ? static_cast<IdentifierExpr&>(*expr.args[0]).name : "argument";
                diag_.report(expr.args[0]->loc, Severity::Error,
                             "cannot copy resource-owning " +
                             std::string(valTy->isClass() ? "class '" : "type '") +
                             typeName + "'; use 'move(" + srcName + ")'");
            }
            bool isNiche = valTy && valTy->isHandle();
            std::string optName = "Option<" + (valTy ? valTy->toString() : "int") + ">";
            auto existing = tc_.findEnum(optName);
            if (existing) return existing;
            std::vector<EnumVariantType> variants;
            variants.push_back(EnumVariantType{"Some", 0, {valTy}});
            variants.push_back(EnumVariantType{"None", 1, {}});
            return tc_.getEnum(optName, std::move(variants), isNiche);
        }
        if (eName == "Result" && vName == "Ok" && expr.args.size() == 1) {
            auto valTy = checkExpr(*expr.args[0]);
            if (valTy && valTy->hasNonTrivialDestructor() && !isRValue(expr.args[0].get())) {
                std::string typeName = valTy->isEnum() ? valTy->enumName : valTy->structName;
                std::string srcName = (expr.args[0]->kind == ExprKind::Identifier) ? static_cast<IdentifierExpr&>(*expr.args[0]).name : "argument";
                diag_.report(expr.args[0]->loc, Severity::Error,
                             "cannot copy resource-owning " +
                             std::string(valTy->isClass() ? "class '" : "type '") +
                             typeName + "'; use 'move(" + srcName + ")'");
            }
            std::string resName = "Result<" + (valTy ? valTy->toString() : "int") + ", int>";
            auto existing = tc_.findEnum(resName);
            if (existing) return existing;
            std::vector<EnumVariantType> variants;
            variants.push_back(EnumVariantType{"Ok", 0, {valTy}});
            variants.push_back(EnumVariantType{"Err", 1, {tc_.getI32()}});
            return tc_.getEnum(resName, std::move(variants), false);
        }
        if (eName == "Result" && vName == "Err" && expr.args.size() == 1) {
            auto errTy = checkExpr(*expr.args[0]);
            if (errTy && errTy->hasNonTrivialDestructor() && !isRValue(expr.args[0].get())) {
                std::string typeName = errTy->isEnum() ? errTy->enumName : errTy->structName;
                std::string srcName = (expr.args[0]->kind == ExprKind::Identifier) ? static_cast<IdentifierExpr&>(*expr.args[0]).name : "argument";
                diag_.report(expr.args[0]->loc, Severity::Error,
                             "cannot copy resource-owning " +
                             std::string(errTy->isClass() ? "class '" : "type '") +
                             typeName + "'; use 'move(" + srcName + ")'");
            }
            std::string resName = "Result<int, " + (errTy ? errTy->toString() : "int") + ">";
            auto existing = tc_.findEnum(resName);
            if (existing) return existing;
            std::vector<EnumVariantType> variants;
            variants.push_back(EnumVariantType{"Ok", 0, {tc_.getI32()}});
            variants.push_back(EnumVariantType{"Err", 1, {errTy}});
            return tc_.getEnum(resName, std::move(variants), false);
        }

        const Type* eTy = tc_.findEnum(eName);
        if (eTy) {
            auto v = eTy->findVariant(vName);
            if (v) {
                if (expr.args.size() != v->payloadTypes.size()) {
                    diag_.report(expr.loc, Severity::Error,
                        "variant '" + vName + "' expects " + std::to_string(v->payloadTypes.size()) +
                        " arguments, but " + std::to_string(expr.args.size()) + " were provided");
                }
                for (size_t i = 0; i < expr.args.size(); ++i) {
                    auto aTy = checkExpr(*expr.args[i]);
                    if (i < v->payloadTypes.size() && v->payloadTypes[i] && aTy) {
                        if (v->payloadTypes[i]->hasNonTrivialDestructor() && !isRValue(expr.args[i].get())) {
                            std::string typeName = v->payloadTypes[i]->isEnum() ? v->payloadTypes[i]->enumName : v->payloadTypes[i]->structName;
                            std::string srcName = (expr.args[i]->kind == ExprKind::Identifier) ? static_cast<IdentifierExpr&>(*expr.args[i]).name : "argument";
                            diag_.report(expr.args[i]->loc, Severity::Error,
                                         "cannot copy resource-owning " +
                                         std::string(v->payloadTypes[i]->isClass() ? "class '" : "type '") +
                                         typeName + "'; use 'move(" + srcName + ")'");
                        }
                        if (!aTy->isImplicitlyConvertibleTo(*v->payloadTypes[i])) {
                            diag_.report(expr.args[i]->loc, Severity::Error,
                                "type mismatch in variant '" + vName + "' argument " + std::to_string(i + 1) +
                                ": expected " + v->payloadTypes[i]->toString() + ", got " + aTy->toString());
                        }
                    }
                }
                return eTy;
            }
        }
    }

    // Check if callee is a generic function template
    auto tmplFn = findGenericFunction(expr.callee);
    if (tmplFn) {
        std::vector<const Type*> typeArgs;
        if (!expr.typeArgs.empty()) {
            for (const auto& ta : expr.typeArgs) {
                typeArgs.push_back(resolveType(*ta));
            }
        } else {
            // Type argument deduction from actual arguments
            std::vector<const Type*> deduced(tmplFn->typeParams.size(), nullptr);
            for (size_t i = 0; i < std::min(expr.args.size(), tmplFn->params.size()); ++i) {
                auto argTy = checkExpr(*expr.args[i]);
                if (argTy && argTy->isReference()) argTy = argTy->elementType;
                if (tmplFn->params[i].type->kind == TypeNodeKind::Named) {
                    auto& pNt = static_cast<const NamedTypeNode&>(*tmplFn->params[i].type);
                    for (size_t j = 0; j < tmplFn->typeParams.size(); ++j) {
                        if (tmplFn->typeParams[j] == pNt.name) {
                            if (!deduced[j]) {
                                deduced[j] = argTy;
                            } else if (argTy && *deduced[j] != *argTy) {
                                diag_.report(expr.args[i]->loc, Severity::Error,
                                    "conflicting deduced types for type parameter '" + tmplFn->typeParams[j] +
                                    "': deduced both '" + deduced[j]->toString() + "' and '" + argTy->toString() + "'");
                            }
                        }
                    }
                    if (!pNt.typeArgs.empty() && argTy && argTy->isRecord()) {
                        auto structTmpl = findGenericStruct(pNt.name);
                        if (structTmpl) {
                            for (size_t k = 0; k < structTmpl->fields.size() && k < argTy->structFields.size(); ++k) {
                                if (structTmpl->fields[k].type->kind == TypeNodeKind::Named) {
                                    auto& fNt = static_cast<const NamedTypeNode&>(*structTmpl->fields[k].type);
                                    for (size_t j = 0; j < tmplFn->typeParams.size(); ++j) {
                                        if (tmplFn->typeParams[j] == fNt.name) {
                                            if (!deduced[j]) {
                                                deduced[j] = argTy->structFields[k].type;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            for (size_t j = 0; j < tmplFn->typeParams.size(); ++j) {
                if (!deduced[j]) {
                    diag_.report(expr.loc, Severity::Error,
                        "cannot deduce type argument for parameter '" + tmplFn->typeParams[j] +
                        "' in call to generic function '" + expr.callee + "'");
                    return tc_.getI32();
                }
            }
            typeArgs = std::move(deduced);
        }

        std::string specializedName;
        auto specializedFnTy = instantiateGenericFunction(*tmplFn, typeArgs, expr.loc, specializedName);
        expr.callee = specializedName;
        expr.typeArgs.clear();

        if (specializedFnTy && specializedFnTy->kind == TypeKind::Function) {
            for (size_t i = 0; i < expr.args.size(); ++i) {
                auto argTy = checkExpr(*expr.args[i]);
                if (i < specializedFnTy->paramTypes.size() && specializedFnTy->paramTypes[i]) {
                    if (argTy && !argTy->isImplicitlyConvertibleTo(*specializedFnTy->paramTypes[i])) {
                        diag_.report(expr.args[i]->loc, Severity::Error,
                            "type mismatch in argument " + std::to_string(i + 1) +
                            " to generic function '" + tmplFn->name + "': expected " +
                            specializedFnTy->paramTypes[i]->toString() + ", got " + argTy->toString());
                    }
                }
            }
            return specializedFnTy->returnType ? specializedFnTy->returnType : tc_.getVoid();
        }
    }

    // Benchmark timing intrinsics (test/benchmark only)
    if (expr.callee == "__benchmark_start") {
        if (!expr.args.empty()) {
            diag_.report(expr.loc, Severity::Error, "__benchmark_start() takes 0 arguments");
        }
        return tc_.getVoid();
    }
    if (expr.callee == "__benchmark_elapsed_ms" || expr.callee == "__benchmark_stop") {
        if (!expr.args.empty()) {
            diag_.report(expr.loc, Severity::Error, expr.callee + "() takes 0 arguments");
        }
        return tc_.getF64();
    }
    if (expr.callee == "__f64_to_bits" || expr.callee == "__double_to_bits") {
        if (expr.args.size() != 1) {
            diag_.report(expr.loc, Severity::Error, expr.callee + "() takes exactly 1 argument");
            return tc_.getU64();
        }
        auto argTy = checkExpr(*expr.args[0]);
        if (!argTy || argTy->kind != TypeKind::F64) {
            diag_.report(expr.loc, Severity::Error,
                         expr.callee + "() argument must be f64 or double, got " +
                         (argTy ? argTy->toString() : "unknown"));
        }
        return tc_.getU64();
    }

    // Bit manipulation builtins: ctz, clz, popcount, popcnt
    if (expr.callee == "ctz" || expr.callee == "__builtin_ctz" ||
        expr.callee == "clz" || expr.callee == "__builtin_clz" ||
        expr.callee == "popcount" || expr.callee == "__builtin_popcount" ||
        expr.callee == "popcnt" || expr.callee == "__builtin_popcnt") {
        if (expr.args.size() != 1) {
            diag_.report(expr.loc, Severity::Error, expr.callee + "() takes exactly 1 argument");
            return tc_.getI32();
        }
        auto argTy = checkExpr(*expr.args[0]);
        if (!argTy || !argTy->isInteger()) {
            diag_.report(expr.loc, Severity::Error,
                         expr.callee + "() argument must be an integer, got " +
                         (argTy ? argTy->toString() : "unknown"));
        }
        return tc_.getI32();
    }

    if (expr.callee == "bitrev" || expr.callee == "__builtin_bitrev" ||
        expr.callee == "bitreverse" || expr.callee == "__builtin_bitreverse" ||
        expr.callee == "bswap" || expr.callee == "__builtin_bswap") {
        if (expr.args.size() != 1) {
            diag_.report(expr.loc, Severity::Error, expr.callee + "() takes exactly 1 argument");
            return tc_.getI32();
        }
        auto argTy = checkExpr(*expr.args[0]);
        if (!argTy || !argTy->isInteger()) {
            diag_.report(expr.loc, Severity::Error,
                         expr.callee + "() argument must be an integer, got " +
                         (argTy ? argTy->toString() : "unknown"));
            return tc_.getI32();
        }
        return argTy;
    }

    // Builtin hash
    if (expr.callee == "__builtin_hash" || expr.callee == "hash") {
        if (expr.args.size() != 1) {
            diag_.report(expr.loc, Severity::Error, expr.callee + "() takes exactly 1 argument");
            return tc_.getI32();
        }
        checkExpr(*expr.args[0]);
        return tc_.getI32();
    }

    // SIMD vector reduction builtins: reduce_add, dot_product
    if (expr.callee == "reduce_add" || expr.callee == "__builtin_reduce_add") {
        if (expr.args.size() != 1) {
            diag_.report(expr.loc, Severity::Error, "reduce_add() takes exactly 1 argument");
            return tc_.getF32();
        }
        auto argTy = checkExpr(*expr.args[0]);
        if (!argTy || !argTy->isSIMDVector()) {
            diag_.report(expr.loc, Severity::Error,
                         "reduce_add() argument must be a SIMD vector, got " +
                         (argTy ? argTy->toString() : "unknown"));
            return tc_.getF32();
        }
        return argTy->elementType ? argTy->elementType : tc_.getF32();
    }

    if (expr.callee == "dot_product" || expr.callee == "__builtin_dot_product") {
        if (expr.args.size() != 2) {
            diag_.report(expr.loc, Severity::Error, "dot_product() takes exactly 2 arguments");
            return tc_.getF32();
        }
        auto a1Ty = checkExpr(*expr.args[0]);
        auto a2Ty = checkExpr(*expr.args[1]);
        if (!a1Ty || !a1Ty->isSIMDVector() || !a2Ty || !a2Ty->isSIMDVector() || *a1Ty != *a2Ty) {
            diag_.report(expr.loc, Severity::Error,
                         "dot_product() requires two arguments of identical SIMD vector type");
            return tc_.getF32();
        }
        return a1Ty->elementType ? a1Ty->elementType : tc_.getF32();
    }

    // Math builtins: sqrt, sqrtf
    if (expr.callee == "sqrtf" || expr.callee == "__builtin_sqrtf" ||
        expr.callee == "sqrt" || expr.callee == "__builtin_sqrt" ||
        expr.callee == "std::math::sqrtf" || expr.callee == "math::sqrtf") {
        if (expr.args.size() != 1) {
            diag_.report(expr.loc, Severity::Error, expr.callee + "() takes exactly 1 argument");
            return tc_.getF32();
        }
        auto argTy = checkExpr(*expr.args[0]);
        if (!argTy || (!argTy->isFloat() && !argTy->isInteger())) {
            diag_.report(expr.loc, Severity::Error,
                         expr.callee + "() argument must be a number, got " +
                         (argTy ? argTy->toString() : "unknown"));
        }
        if (expr.callee == "sqrt" || expr.callee == "__builtin_sqrt" || (argTy && argTy->kind == TypeKind::F64)) {
            return tc_.getF64();
        }
        return tc_.getF32();
    }

    // Builtin len() function returns size_t (usize)
    if (expr.callee == "len") {
        if (expr.args.size() != 1) {
            diag_.report(expr.loc, Severity::Error, "len() takes exactly 1 argument");
            return tc_.getUsize();
        }
        auto argTy = checkExpr(*expr.args[0]);
        if (!argTy || (!argTy->isArray() && !argTy->isArrayView())) {
            diag_.report(expr.loc, Severity::Error,
                         "len() argument must be an array or array view, got " +
                         (argTy ? argTy->toString() : "unknown"));
        }
        return tc_.getUsize();
    }

    // Builtin print() and println()
    if (expr.callee == "println" || expr.callee == "io::println" || expr.callee == "__builtin_println") {
        if (expr.args.size() > 1) {
            diag_.report(expr.loc, Severity::Error, "println() takes at most 1 argument");
        }
        for (auto& arg : expr.args) {
            auto argTy = checkExpr(*arg);
            if (!argTy) continue;
            if (argTy->isReference()) argTy = argTy->elementType;
            bool valid = argTy->isInteger() || argTy->isFloat() || argTy->isBool() || isCharBufferOrView(argTy);
            if (!valid) {
                diag_.report(arg->loc, Severity::Error,
                             "unsupported argument type for println(): " + argTy->toString() +
                             "; aggregate, record, function, or void expressions cannot be printed");
            }
        }
        return tc_.getVoid();
    }
    if (expr.callee == "print" || expr.callee == "io::print" || expr.callee == "__builtin_print") {
        if (expr.args.empty()) {
            diag_.report(expr.loc, Severity::Error, "print() takes at least 1 argument");
            return tc_.getVoid();
        }
        if (expr.args.size() > 1) {
            diag_.report(expr.loc, Severity::Error, "print() takes exactly 1 argument");
        }
        for (auto& arg : expr.args) {
            auto argTy = checkExpr(*arg);
            if (!argTy) continue;
            if (argTy->isReference()) argTy = argTy->elementType;
            bool valid = argTy->isInteger() || argTy->isFloat() || argTy->isBool() || isCharBufferOrView(argTy);
            if (!valid) {
                diag_.report(arg->loc, Severity::Error,
                             "unsupported argument type for print(): " + argTy->toString() +
                             "; aggregate, record, function, or void expressions cannot be printed");
            }
        }
        return tc_.getVoid();
    }
    // Builtin getInput()
    if (expr.callee == "getInput" || expr.callee == "io::getInput" || expr.callee == "__builtin_get_input") {
        if (expr.args.empty()) {
            diag_.report(expr.loc, Severity::Warning,
                         "zero-argument getInput() is deprecated; it returns a temporary view into shared static storage that will be overwritten by subsequent input calls. Use getInput(buffer, capacity) instead.");
            return tc_.getArrayView(tc_.getU8(), /*isConst=*/true);
        }
        if (expr.args.size() > 2) {
            diag_.report(expr.loc, Severity::Error, "getInput() takes 0, 1, or 2 arguments");
            return tc_.getI32();
        }
        auto bTy = checkExpr(*expr.args[0]);
        if (bTy) {
            if (!isCharBufferOrView(bTy)) {
                diag_.report(expr.args[0]->loc, Severity::Error,
                             "getInput() buffer must be a character array or view, got " + bTy->toString());
            }
            if (bTy->isConst) {
                diag_.report(expr.args[0]->loc, Severity::Error, "getInput() buffer cannot be const");
            }
            if (expr.args[0]->kind == ExprKind::StringLiteral) {
                diag_.report(expr.args[0]->loc, Severity::Error,
                             "cannot pass string literal as writable input buffer to getInput()");
            }
        }
        if (expr.args.size() == 2) {
            auto capTy = checkExpr(*expr.args[1]);
            if (capTy && !capTy->isInteger()) {
                diag_.report(expr.args[1]->loc, Severity::Error,
                             "getInput() capacity must be an integer, got " + capTy->toString());
            }
        }
        return tc_.getI32();
    }

    // Builtin readLine()
    if (expr.callee == "readLine" || expr.callee == "io::readLine") {
        if (expr.args.empty() || expr.args.size() > 2) {
            diag_.report(expr.loc, Severity::Error, "readLine() takes 1 or 2 arguments");
            return tc_.getI32();
        }
        auto bTy = checkExpr(*expr.args[0]);
        if (bTy) {
            if (!isCharBufferOrView(bTy)) {
                diag_.report(expr.args[0]->loc, Severity::Error,
                             "readLine() buffer must be a character array or view, got " + bTy->toString());
            }
            if (bTy->isConst) {
                diag_.report(expr.args[0]->loc, Severity::Error, "readLine() buffer cannot be const");
            }
            if (expr.args[0]->kind == ExprKind::StringLiteral) {
                diag_.report(expr.args[0]->loc, Severity::Error,
                             "cannot pass string literal as writable input buffer to readLine()");
            }
        }
        if (expr.args.size() == 2) {
            auto capTy = checkExpr(*expr.args[1]);
            if (capTy && !capTy->isInteger()) {
                diag_.report(expr.args[1]->loc, Severity::Error,
                             "readLine() capacity must be an integer, got " + capTy->toString());
            }
        }
        return tc_.getI32();
    }

    // Builtin file operations
    if (expr.callee == "openFile" || expr.callee == "io::openFile" || expr.callee == "file::open" || expr.callee == "__builtin_file_open") {
        if (expr.args.size() != 2) {
            diag_.report(expr.loc, Severity::Error, "openFile() takes exactly 2 arguments (path, mode)");
            return tc_.getI64();
        }
        auto pTy = checkExpr(*expr.args[0]);
        auto mTy = checkExpr(*expr.args[1]);
        if (pTy && !isCharBufferOrView(pTy)) {
            diag_.report(expr.args[0]->loc, Severity::Error, "openFile() path must be a character array or view, got " + pTy->toString());
        }
        if (mTy && !isCharBufferOrView(mTy)) {
            diag_.report(expr.args[1]->loc, Severity::Error, "openFile() mode must be a character array or view, got " + mTy->toString());
        }
        return tc_.getI64();
    }
    if (expr.callee == "closeFile" || expr.callee == "io::closeFile" || expr.callee == "file::close" || expr.callee == "__builtin_file_close") {
        if (expr.args.size() != 1) {
            diag_.report(expr.loc, Severity::Error, "closeFile() takes exactly 1 argument (handle)");
            return tc_.getI32();
        }
        auto hTy = checkExpr(*expr.args[0]);
        if (hTy && !hTy->isInteger()) {
            diag_.report(expr.args[0]->loc, Severity::Error, "closeFile() handle must be an integer, got " + hTy->toString());
        }
        return tc_.getI32();
    }
    if (expr.callee == "readFile" || expr.callee == "io::readFile" || expr.callee == "file::read" || expr.callee == "__builtin_read_file") {
        if (expr.args.size() != 3) {
            diag_.report(expr.loc, Severity::Error, "readFile() takes exactly 3 arguments (path, buffer, maxBytes)");
            return tc_.getI32();
        }
        auto pTy = checkExpr(*expr.args[0]);
        auto bTy = checkExpr(*expr.args[1]);
        auto mTy = checkExpr(*expr.args[2]);
        if (pTy && !isCharBufferOrView(pTy)) {
            diag_.report(expr.args[0]->loc, Severity::Error, "readFile() path must be a character array or view, got " + pTy->toString());
        }
        if (bTy) {
            if (!isCharBufferOrView(bTy)) {
                diag_.report(expr.args[1]->loc, Severity::Error, "readFile() buffer must be a character array or view, got " + bTy->toString());
            }
            bool isArgConst = bTy->isConst || (bTy->elementType && bTy->elementType->isConst);
            if (expr.args[1]->kind == ExprKind::Identifier) {
                auto idSym = syms_.lookup(static_cast<IdentifierExpr*>(expr.args[1].get())->name);
                if (idSym && idSym->isConst) isArgConst = true;
            }
            if (isArgConst) {
                diag_.report(expr.args[1]->loc, Severity::Error, "readFile() buffer cannot be const");
            }
            if (expr.args[1]->kind == ExprKind::StringLiteral) {
                diag_.report(expr.args[1]->loc, Severity::Error, "cannot pass string literal as writable input buffer to readFile()");
            }
        }
        if (mTy && !mTy->isInteger()) {
            diag_.report(expr.args[2]->loc, Severity::Error, "readFile() maxBytes must be an integer, got " + mTy->toString());
        }
        return tc_.getI32();
    }
    if (expr.callee == "writeFile" || expr.callee == "io::writeFile" || expr.callee == "file::write" || expr.callee == "__builtin_write_file") {
        if (expr.args.size() != 2) {
            diag_.report(expr.loc, Severity::Error, "writeFile() takes exactly 2 arguments (path, text)");
            return tc_.getI32();
        }
        auto pTy = checkExpr(*expr.args[0]);
        auto tTy = checkExpr(*expr.args[1]);
        if (pTy && !isCharBufferOrView(pTy)) {
            diag_.report(expr.args[0]->loc, Severity::Error, "writeFile() path must be a character array or view, got " + pTy->toString());
        }
        if (tTy && !isCharBufferOrView(tTy)) {
            diag_.report(expr.args[1]->loc, Severity::Error, "writeFile() text must be a character array or view, got " + tTy->toString());
        }
        return tc_.getI32();
    }
    if (expr.callee == "appendFile" || expr.callee == "io::appendFile" || expr.callee == "file::append" || expr.callee == "__builtin_append_file") {
        if (expr.args.size() != 2) {
            diag_.report(expr.loc, Severity::Error, "appendFile() takes exactly 2 arguments (path, text)");
            return tc_.getI32();
        }
        auto pTy = checkExpr(*expr.args[0]);
        auto tTy = checkExpr(*expr.args[1]);
        if (pTy && !isCharBufferOrView(pTy)) {
            diag_.report(expr.args[0]->loc, Severity::Error, "appendFile() path must be a character array or view, got " + pTy->toString());
        }
        if (tTy && !isCharBufferOrView(tTy)) {
            diag_.report(expr.args[1]->loc, Severity::Error, "appendFile() text must be a character array or view, got " + tTy->toString());
        }
        return tc_.getI32();
    }
    if (expr.callee == "fileExists" || expr.callee == "io::fileExists" || expr.callee == "file::exists" || expr.callee == "__builtin_file_exists") {
        if (expr.args.size() != 1) {
            diag_.report(expr.loc, Severity::Error, "fileExists() takes exactly 1 argument (path)");
            return tc_.getBool();
        }
        auto pTy = checkExpr(*expr.args[0]);
        if (pTy && !isCharBufferOrView(pTy)) {
            diag_.report(expr.args[0]->loc, Severity::Error, "fileExists() path must be a character array or view, got " + pTy->toString());
        }
        return tc_.getBool();
    }
    if (expr.callee == "removeFile" || expr.callee == "io::removeFile" || expr.callee == "file::remove" || expr.callee == "__builtin_file_remove") {
        if (expr.args.size() != 1) {
            diag_.report(expr.loc, Severity::Error, "removeFile() takes exactly 1 argument (path)");
            return tc_.getI32();
        }
        auto pTy = checkExpr(*expr.args[0]);
        if (pTy && !isCharBufferOrView(pTy)) {
            diag_.report(expr.args[0]->loc, Severity::Error, "removeFile() path must be a character array or view, got " + pTy->toString());
        }
        return tc_.getI32();
    }
    if (expr.callee == "fileRead" || expr.callee == "__builtin_file_read") {
        if (expr.args.size() != 3) {
            diag_.report(expr.loc, Severity::Error, "fileRead() takes exactly 3 arguments (handle, buffer, maxBytes)");
            return tc_.getI32();
        }
        auto hTy = checkExpr(*expr.args[0]);
        auto bTy = checkExpr(*expr.args[1]);
        auto mTy = checkExpr(*expr.args[2]);
        if (hTy && !hTy->isInteger()) {
            diag_.report(expr.args[0]->loc, Severity::Error, "fileRead() handle must be an integer, got " + hTy->toString());
        }
        if (bTy) {
            if (!isCharBufferOrView(bTy)) {
                diag_.report(expr.args[1]->loc, Severity::Error, "fileRead() buffer must be a character array or view, got " + bTy->toString());
            }
            bool isArgConst = bTy->isConst || (bTy->elementType && bTy->elementType->isConst);
            if (expr.args[1]->kind == ExprKind::Identifier) {
                auto idSym = syms_.lookup(static_cast<IdentifierExpr*>(expr.args[1].get())->name);
                if (idSym && idSym->isConst) isArgConst = true;
            }
            if (isArgConst) {
                diag_.report(expr.args[1]->loc, Severity::Error, "fileRead() buffer cannot be const");
            }
            if (expr.args[1]->kind == ExprKind::StringLiteral) {
                diag_.report(expr.args[1]->loc, Severity::Error, "cannot pass string literal as writable input buffer to fileRead()");
            }
        }
        if (mTy && !mTy->isInteger()) {
            diag_.report(expr.args[2]->loc, Severity::Error, "fileRead() maxBytes must be an integer, got " + mTy->toString());
        }
        return tc_.getI32();
    }
    if (expr.callee == "fileWrite" || expr.callee == "__builtin_file_write") {
        if (expr.args.size() != 2) {
            diag_.report(expr.loc, Severity::Error, "fileWrite() takes exactly 2 arguments (handle, text)");
            return tc_.getI32();
        }
        auto hTy = checkExpr(*expr.args[0]);
        auto tTy = checkExpr(*expr.args[1]);
        if (hTy && !hTy->isInteger()) {
            diag_.report(expr.args[0]->loc, Severity::Error, "fileWrite() handle must be an integer, got " + hTy->toString());
        }
        if (tTy && !isCharBufferOrView(tTy)) {
            diag_.report(expr.args[1]->loc, Severity::Error, "fileWrite() text must be a character array or view, got " + tTy->toString());
        }
        return tc_.getI32();
    }
    if (expr.callee == "fileFlush" || expr.callee == "__builtin_file_flush") {
        if (expr.args.size() != 1) {
            diag_.report(expr.loc, Severity::Error, "fileFlush() takes exactly 1 argument (handle)");
            return tc_.getVoid();
        }
        auto hTy = checkExpr(*expr.args[0]);
        if (hTy && !hTy->isInteger()) {
            diag_.report(expr.args[0]->loc, Severity::Error, "fileFlush() handle must be an integer, got " + hTy->toString());
        }
        return tc_.getVoid();
    }

    // Dynamic Heap & Memory Builtins
    if (expr.callee == "__builtin_alloc" || expr.callee == "mem::alloc") {
        if (expr.typeArgs.empty()) {
            diag_.report(expr.loc, Severity::Error, "__builtin_alloc requires a type argument, e.g. __builtin_alloc<int>(count)");
            return tc_.getArrayView(tc_.getI32(), false);
        }
        if (expr.args.size() != 1) {
            diag_.report(expr.loc, Severity::Error, "__builtin_alloc takes exactly 1 argument (count)");
            return tc_.getArrayView(tc_.getI32(), false);
        }
        auto elemTy = resolveType(*expr.typeArgs[0]);
        auto cntTy = checkExpr(*expr.args[0]);
        if (cntTy && !cntTy->isInteger()) {
            diag_.report(expr.args[0]->loc, Severity::Error, "__builtin_alloc count must be an integer, got " + cntTy->toString());
        }
        return tc_.getArrayView(elemTy, false);
    }
    if (expr.callee == "__builtin_realloc" || expr.callee == "mem::realloc") {
        if (expr.typeArgs.empty()) {
            diag_.report(expr.loc, Severity::Error, "__builtin_realloc requires a type argument, e.g. __builtin_realloc<int>(slice, newCount)");
            return tc_.getArrayView(tc_.getI32(), false);
        }
        if (expr.args.size() != 2) {
            diag_.report(expr.loc, Severity::Error, "__builtin_realloc takes exactly 2 arguments (slice, newCount)");
            return tc_.getArrayView(tc_.getI32(), false);
        }
        auto elemTy = resolveType(*expr.typeArgs[0]);
        auto sliceTy = checkExpr(*expr.args[0]);
        auto cntTy = checkExpr(*expr.args[1]);
        if (sliceTy && !sliceTy->isArrayView() && !sliceTy->isArray()) {
            diag_.report(expr.args[0]->loc, Severity::Error, "__builtin_realloc first argument must be an array view");
        }
        if (cntTy && !cntTy->isInteger()) {
            diag_.report(expr.args[1]->loc, Severity::Error, "__builtin_realloc new count must be an integer");
        }
        return tc_.getArrayView(elemTy, false);
    }
    if (expr.callee == "__builtin_free" || expr.callee == "mem::free") {
        if (expr.args.size() != 1) {
            diag_.report(expr.loc, Severity::Error, "__builtin_free takes exactly 1 argument (slice)");
            return tc_.getVoid();
        }
        auto sliceTy = checkExpr(*expr.args[0]);
        if (sliceTy && !sliceTy->isArrayView() && !sliceTy->isArray()) {
            diag_.report(expr.args[0]->loc, Severity::Error, "__builtin_free argument must be an array view");
        }
        return tc_.getVoid();
    }
    if (expr.callee == "__builtin_memcpy" || expr.callee == "mem::copy") {
        if (expr.args.size() != 5) {
            diag_.report(expr.loc, Severity::Error, "__builtin_memcpy takes 5 arguments (dst, dstOffset, src, srcOffset, count)");
            return tc_.getVoid();
        }
        checkExpr(*expr.args[0]);
        checkExpr(*expr.args[1]);
        checkExpr(*expr.args[2]);
        checkExpr(*expr.args[3]);
        checkExpr(*expr.args[4]);
        return tc_.getVoid();
    }
    if (expr.callee == "__builtin_memmove" || expr.callee == "mem::move") {
        if (expr.args.size() != 5) {
            diag_.report(expr.loc, Severity::Error, "__builtin_memmove takes 5 arguments (dst, dstOffset, src, srcOffset, count)");
            return tc_.getVoid();
        }
        checkExpr(*expr.args[0]);
        checkExpr(*expr.args[1]);
        checkExpr(*expr.args[2]);
        checkExpr(*expr.args[3]);
        checkExpr(*expr.args[4]);
        return tc_.getVoid();
    }
    if (expr.callee == "__builtin_memset" || expr.callee == "mem::set") {
        if (expr.args.size() != 4) {
            diag_.report(expr.loc, Severity::Error, "__builtin_memset takes 4 arguments (dst, dstOffset, byteVal, count)");
            return tc_.getVoid();
        }
        checkExpr(*expr.args[0]);
        checkExpr(*expr.args[1]);
        checkExpr(*expr.args[2]);
        checkExpr(*expr.args[3]);
        return tc_.getVoid();
    }
    if (expr.callee == "__builtin_get_unchecked" || expr.callee == "mem::get_unchecked") {
        if (expr.typeArgs.empty()) {
            diag_.report(expr.loc, Severity::Error, "__builtin_get_unchecked requires a type argument, e.g. __builtin_get_unchecked<int>(slice, idx)");
            return tc_.getI32();
        }
        if (expr.args.size() != 2) {
            diag_.report(expr.loc, Severity::Error, "__builtin_get_unchecked takes exactly 2 arguments (slice, idx)");
            return tc_.getI32();
        }
        auto elemTy = resolveType(*expr.typeArgs[0]);
        checkExpr(*expr.args[0]);
        auto iTy = checkExpr(*expr.args[1]);
        if (iTy && !iTy->isInteger()) {
            diag_.report(expr.args[1]->loc, Severity::Error, "index must be an integer, got " + iTy->toString());
        }
        return elemTy;
    }
    if (expr.callee == "__builtin_set_unchecked" || expr.callee == "mem::set_unchecked") {
        if (expr.typeArgs.empty()) {
            diag_.report(expr.loc, Severity::Error, "__builtin_set_unchecked requires a type argument, e.g. __builtin_set_unchecked<int>(slice, idx, val)");
            return tc_.getVoid();
        }
        if (expr.args.size() != 3) {
            diag_.report(expr.loc, Severity::Error, "__builtin_set_unchecked takes exactly 3 arguments (slice, idx, val)");
            return tc_.getVoid();
        }
        resolveType(*expr.typeArgs[0]);
        checkExpr(*expr.args[0]);
        auto iTy = checkExpr(*expr.args[1]);
        checkExpr(*expr.args[2]);
        if (iTy && !iTy->isInteger()) {
            diag_.report(expr.args[1]->loc, Severity::Error, "index must be an integer, got " + iTy->toString());
        }
        return tc_.getVoid();
    }
    auto isAtomicTarget = [](const ast::Expr& e, const Type* ty) {
        if (!ty) return false;
        if (ty->isReference() || ty->isPointer()) return true;
        return (e.kind == ExprKind::Identifier || e.kind == ExprKind::Field ||
                e.kind == ExprKind::Index || (e.kind == ExprKind::Unary && static_cast<const UnaryExpr&>(e).op == UnaryOp::Deref));
    };

    auto isTargetConst = [&](const ast::Expr& e, const Type* ty) {
        if (ty && ty->isConstRef) return true;
        if (e.kind == ExprKind::Identifier) {
            auto sym = findSymbol(static_cast<const IdentifierExpr&>(e).name, e.loc);
            if (sym && (sym->isConst || (sym->type && sym->type->isConstRef))) return true;
        }
        return false;
    };

    auto getTargetValueType = [](const Type* ty) -> const Type* {
        if (!ty) return nullptr;
        if (ty->isReference() || ty->isPointer()) return ty->elementType;
        return ty;
    };

    if (expr.callee == "__builtin_atomic_load") {
        if (expr.args.size() != 1) {
            diag_.report(expr.loc, Severity::Error, "__builtin_atomic_load takes 1 argument (target)");
            return tc_.getI32();
        }
        auto targetTy = checkExpr(*expr.args[0]);
        if (!isAtomicTarget(*expr.args[0], targetTy)) {
            diag_.report(expr.args[0]->loc, Severity::Error, "__builtin_atomic_load target must be an lvalue, pointer, or reference");
            return tc_.getI32();
        }
        auto valTy = getTargetValueType(targetTy);
        if (!expr.typeArgs.empty()) {
            auto explicitTy = resolveType(*expr.typeArgs[0]);
            if (valTy && explicitTy && *explicitTy != *valTy) {
                diag_.report(expr.loc, Severity::Error,
                             "explicit type argument '" + explicitTy->toString() + "' does not match target type '" +
                             valTy->toString() + "'");
            }
            return explicitTy ? explicitTy : (valTy ? valTy : tc_.getI32());
        }
        return valTy ? valTy : tc_.getI32();
    }
    if (expr.callee == "__builtin_atomic_store") {
        if (expr.args.size() != 2) {
            diag_.report(expr.loc, Severity::Error, "__builtin_atomic_store takes 2 arguments (target, val)");
            return tc_.getVoid();
        }
        auto targetTy = checkExpr(*expr.args[0]);
        auto valTy = checkExpr(*expr.args[1]);
        if (!isAtomicTarget(*expr.args[0], targetTy)) {
            diag_.report(expr.args[0]->loc, Severity::Error, "__builtin_atomic_store target must be an lvalue, pointer, or reference");
        } else if (isTargetConst(*expr.args[0], targetTy)) {
            diag_.report(expr.args[0]->loc, Severity::Error, "cannot atomically store to a const target");
        } else {
            auto targetValTy = getTargetValueType(targetTy);
            if (valTy && targetValTy && *valTy != *targetValTy) {
                diag_.report(expr.args[1]->loc, Severity::Error,
                             "cannot store value of type '" + valTy->toString() + "' to atomic target of type '" +
                             targetValTy->toString() + "'");
            }
        }
        return tc_.getVoid();
    }
    if (expr.callee == "__builtin_atomic_add" || expr.callee == "__builtin_atomic_sub") {
        if (expr.args.size() != 2) {
            diag_.report(expr.loc, Severity::Error, expr.callee + " takes 2 arguments (target, delta)");
            return tc_.getI32();
        }
        auto targetTy = checkExpr(*expr.args[0]);
        auto deltaTy = checkExpr(*expr.args[1]);
        if (!isAtomicTarget(*expr.args[0], targetTy)) {
            diag_.report(expr.args[0]->loc, Severity::Error, expr.callee + " target must be an lvalue, pointer, or reference");
        } else if (isTargetConst(*expr.args[0], targetTy)) {
            diag_.report(expr.args[0]->loc, Severity::Error, "cannot atomically modify a const target");
        } else {
            auto targetValTy = getTargetValueType(targetTy);
            if (deltaTy && targetValTy && *deltaTy != *targetValTy) {
                diag_.report(expr.args[1]->loc, Severity::Error,
                             "cannot modify atomic target of type '" + targetValTy->toString() +
                             "' with delta of type '" + deltaTy->toString() + "'");
            }
        }
        auto targetValTy = getTargetValueType(targetTy);
        if (!expr.typeArgs.empty()) {
            return resolveType(*expr.typeArgs[0]);
        }
        return targetValTy ? targetValTy : tc_.getI32();
    }
    if (expr.callee == "__builtin_atomic_cmpxchg") {
        if (expr.args.size() != 3) {
            diag_.report(expr.loc, Severity::Error, "__builtin_atomic_cmpxchg takes 3 arguments (target, expected, desired)");
            return tc_.getBool();
        }
        auto targetTy = checkExpr(*expr.args[0]);
        auto expTy = checkExpr(*expr.args[1]);
        auto desTy = checkExpr(*expr.args[2]);
        if (!isAtomicTarget(*expr.args[0], targetTy)) {
            diag_.report(expr.args[0]->loc, Severity::Error, "__builtin_atomic_cmpxchg target must be an lvalue, pointer, or reference");
        } else if (isTargetConst(*expr.args[0], targetTy)) {
            diag_.report(expr.args[0]->loc, Severity::Error, "cannot atomically modify a const target");
        } else {
            auto targetValTy = getTargetValueType(targetTy);
            if (expTy && targetValTy && *expTy != *targetValTy) {
                diag_.report(expr.args[1]->loc, Severity::Error,
                             "expected value of type '" + expTy->toString() + "' does not match atomic target of type '" +
                             targetValTy->toString() + "'");
            }
            if (desTy && targetValTy && *desTy != *targetValTy) {
                diag_.report(expr.args[2]->loc, Severity::Error,
                             "desired value of type '" + desTy->toString() + "' does not match atomic target of type '" +
                             targetValTy->toString() + "'");
            }
        }
        return tc_.getBool();
    }

    auto sym = findSymbol(expr.callee, expr.loc);
    if (!sym && currentRecord_) {
        auto m = currentRecord_->findMethod(expr.callee);
        if (m) {
            if (isCurrentMethodConst_ && !m->isConst) {
                diag_.report(expr.loc, Severity::Error,
                             "cannot call non-const method '" + expr.callee +
                             "' from const method of '" + currentRecord_->structName + "'");
            }
            if (expr.args.size() < m->paramTypes.size() && m->defaultArgs.size() == m->paramTypes.size()) {
                for (size_t i = expr.args.size(); i < m->paramTypes.size(); ++i) {
                    if (!m->defaultArgs[i]) break;
                    expr.args.push_back(Monomorphizer::substituteExpr(m->defaultArgs[i], {}));
                }
            }
            if (expr.args.size() != m->paramTypes.size()) {
                diag_.report(expr.loc, Severity::Error,
                             "method '" + expr.callee + "' expects " +
                             std::to_string(m->paramTypes.size()) + " arguments, got " +
                             std::to_string(expr.args.size()));
                return m->returnType ? m->returnType : tc_.getVoid();
            }
            for (size_t i = 0; i < expr.args.size(); ++i) {
                auto argTy = checkExpr(*expr.args[i]);
                auto paramTy = m->paramTypes[i];
                if (paramTy && paramTy->hasNonTrivialDestructor() && !paramTy->isReference() && !isRValue(expr.args[i].get())) {
                    std::string typeName = paramTy->isEnum() ? paramTy->enumName : paramTy->structName;
                    std::string srcName = (expr.args[i]->kind == ExprKind::Identifier) ? static_cast<IdentifierExpr&>(*expr.args[i]).name : "argument";
                    diag_.report(expr.args[i]->loc, Severity::Error,
                                 "cannot pass resource-owning " +
                                 std::string(paramTy->isClass() ? "class '" : "type '") +
                                 typeName + "' by value; use 'move(" + srcName + ")' or pass by reference");
                }
                if (argTy && !argTy->isImplicitlyConvertibleTo(*paramTy)) {
                    diag_.report(expr.args[i]->loc, Severity::Error,
                                  "argument " + std::to_string(i + 1) + " type mismatch in call to '" +
                                  expr.callee + "': expected " + paramTy->toString() + ", got " + argTy->toString());
                }
            }
            ProvenanceSet retProv(ProvenanceKind::ThisBorrow, 0, "this");
            setExprProvenance(&expr, retProv);
            return m->returnType ? m->returnType : tc_.getVoid();
        }
    }

    if (!sym) {
        auto recTy = tc_.findRecord(expr.callee);
        if (recTy && recTy->isRecord()) {
            if (recTy->isAbstract) {
                diag_.report(expr.loc, Severity::Error,
                             "cannot instantiate abstract class '" + recTy->structName +
                             "' with pure virtual methods");
                return recTy;
            }
            bool foundCtor = false;
            for (const auto& ctor : recTy->constructors) {
                if (ctor.paramTypes.size() == expr.args.size()) {
                    foundCtor = true;
                    for (size_t i = 0; i < expr.args.size(); ++i) {
                        auto argTy = checkExpr(*expr.args[i]);
                        auto paramTy = ctor.paramTypes[i];
                        if (argTy && paramTy && !argTy->isImplicitlyConvertibleTo(*paramTy)) {
                            diag_.report(expr.args[i]->loc, Severity::Error,
                                         "argument " + std::to_string(i + 1) + " type mismatch in constructor call to '" +
                                         recTy->structName + "': expected " + paramTy->toString() + ", got " + argTy->toString());
                        }
                    }
                    break;
                }
            }
            if (!foundCtor && !(recTy->constructors.empty() && expr.args.empty())) {
                diag_.report(expr.loc, Severity::Error,
                             "type '" + recTy->structName + "' has no constructor taking " +
                             std::to_string(expr.args.size()) + " arguments");
            }
            return recTy;
        }

        if (expr.callee.find("::") != std::string::npos) {
            return tc_.getI32(); // already reported in findSymbol
        }
        diag_.report(expr.loc, Severity::Error,
                     "call to undeclared function '" + expr.callee + "'");
        return tc_.getI32(); // recovery
    }

    if (!sym->mangledName.empty()) {
        expr.callee = sym->mangledName;
    }

    if (!sym->isFunction) {
        if (sym->type && sym->type->isFunction()) {
            auto fnTy = sym->type;
            if (expr.args.size() != fnTy->paramTypes.size()) {
                diag_.report(expr.loc, Severity::Error,
                             "function pointer '" + expr.callee + "' expects " +
                             std::to_string(fnTy->paramTypes.size()) + " arguments, got " +
                             std::to_string(expr.args.size()));
                return fnTy->returnType ? fnTy->returnType : tc_.getVoid();
            }
            for (size_t i = 0; i < expr.args.size(); ++i) {
                auto argTy = checkExpr(*expr.args[i]);
                auto paramTy = fnTy->paramTypes[i];
                if (argTy && paramTy && !argTy->isImplicitlyConvertibleTo(*paramTy)) {
                    diag_.report(expr.args[i]->loc, Severity::Error,
                                 "argument " + std::to_string(i + 1) + " type mismatch in call to '" +
                                 expr.callee + "': expected " + paramTy->toString() + ", got " + argTy->toString());
                }
            }
            if (parallelLoopScopeDepth_ > 0) {
                for (const auto& frame : activeBorrowsStack_) {
                    for (const auto& b : frame) {
                        if (b.borrower == expr.callee && b.isMutable) {
                            size_t targetDepth = syms_.lookupScopeDepth(b.rootTarget);
                            if (targetDepth > 0 && targetDepth < parallelLoopScopeDepth_) {
                                diag_.report(expr.loc, Severity::Error,
                                             "data-race safety violation: closure with mutable captures cannot be used inside parallel loop; outer variable '" +
                                             b.rootTarget + "' is captured as mutable");
                            }
                        }
                    }
                }
            }
            checkCallSiteBorrowSafety("<function pointer>", expr.args, fnTy->paramTypes, expr.loc);
            return fnTy->returnType ? fnTy->returnType : tc_.getVoid();
        }
        diag_.report(expr.loc, Severity::Error,
                     "'" + expr.callee + "' is not a function");
        return tc_.getI32();
    }

    auto fnTy = sym->type;
    if (expr.args.size() < fnTy->paramTypes.size() && fnTy->defaultArgs.size() == fnTy->paramTypes.size()) {
        for (size_t i = expr.args.size(); i < fnTy->paramTypes.size(); ++i) {
            if (!fnTy->defaultArgs[i]) break;
            expr.args.push_back(Monomorphizer::substituteExpr(fnTy->defaultArgs[i], {}));
        }
    }
    if (expr.args.size() != fnTy->paramTypes.size()) {
        diag_.report(expr.loc, Severity::Error,
                     "function '" + expr.callee + "' expects " +
                     std::to_string(fnTy->paramTypes.size()) + " arguments, got " +
                     std::to_string(expr.args.size()));
        return fnTy->returnType;
    }

    for (size_t i = 0; i < expr.args.size(); ++i) {
        auto paramTy = fnTy->paramTypes[i];
        auto argTy = checkExpr(*expr.args[i]);
        if (paramTy && paramTy->isEnum() && paramTy->enumName.rfind("Result<", 0) == 0 && argTy && argTy->isEnum() && argTy->enumName.rfind("Result<", 0) == 0) {
            setExprType(expr.args[i].get(), paramTy);
            argTy = paramTy;
        } else if (paramTy && paramTy->isEnum() && paramTy->enumName.rfind("Option<", 0) == 0 && argTy && argTy->isEnum() && argTy->enumName.rfind("Option<", 0) == 0) {
            setExprType(expr.args[i].get(), paramTy);
            argTy = paramTy;
        }
        if (paramTy && paramTy->hasNonTrivialDestructor() && !paramTy->isReference() && !isRValue(expr.args[i].get())) {
            std::string typeName = paramTy->isEnum() ? paramTy->enumName : paramTy->structName;
            std::string srcName = (expr.args[i]->kind == ExprKind::Identifier) ? static_cast<IdentifierExpr&>(*expr.args[i]).name : "argument";
            diag_.report(expr.args[i]->loc, Severity::Error,
                         "cannot pass resource-owning " +
                         std::string(paramTy->isClass() ? "class '" : "type '") +
                         typeName + "' by value; use 'move(" + srcName + ")' or pass by reference");
        }
        if (argTy && !argTy->isImplicitlyConvertibleTo(*paramTy)) {
            diag_.report(expr.args[i]->loc, Severity::Error,
                         "argument " + std::to_string(i + 1) + " type mismatch: expected " +
                         paramTy->toString() + ", got " + argTy->toString());
        }
    }

    checkCallSiteBorrowSafety(expr.callee, expr.args, fnTy->paramTypes, expr.loc);

    const FunctionBorrowSummary* summary = getFunctionSummary(expr.callee);
    if (!summary && sym && !sym->mangledName.empty()) {
        summary = getFunctionSummary(sym->mangledName);
    }

    bool isExternal = (summary && summary->isExternal);
    if (!summary && sym) {
        // If symbol exists but no summary, check if it has a body
        isExternal = false; // by default defined in same translation unit or module
    }

    for (size_t i = 0; i < expr.args.size(); ++i) {
        ProvenanceSet aProv = getExprProvenance(expr.args[i].get());
        if (aProv.empty()) {
            uint64_t aRid = getExprRegionProvenance(expr.args[i].get());
            if (aRid > 0) aProv = ProvenanceSet(ProvenanceKind::LocalRegion, aRid);
        }

        auto argTy = getExprType(expr.args[i].get());
        bool isStackArg = aProv.hasLocalStack();
        if (argTy && (argTy->isArray() || argTy->isReference())) {
            if (expr.args[i]->kind == ExprKind::Identifier) {
                auto name = static_cast<IdentifierExpr&>(*expr.args[i]).name;
                auto s = syms_.lookup(name);
                if (s && s->regionId == 0 && !s->isParameter) {
                    isStackArg = true;
                }
                if (i < fnTy->paramTypes.size() && fnTy->paramTypes[i]) {
                    auto pTy = fnTy->paramTypes[i];
                    if (pTy->isArray() || pTy->isArrayView() || (pTy->isReference() && !pTy->isConstRef)) {
                        markAllArrayElementsAssigned(name);
                    }
                }
            }
        }
        if (isExternal && isStackArg) {
            diag_.report(expr.args[i]->loc, Severity::Error,
                         "cannot pass safe stack pointer, reference, or array to unknown external function '" +
                         expr.callee + "' without verified no-escape contract");
            continue;
        }

        if (aProv.hasNonStaticBorrowOrRegion()) {
            if (isExternal || (summary && summary->isParamUnknown(i))) {
                diag_.report(expr.args[i]->loc, Severity::Error,
                             "cannot pass borrowed parameter, reference, or view to unknown external function '" +
                             expr.callee + "' without verified no-escape contract");
            } else if (summary && summary->paramEscapes(i)) {
                diag_.report(expr.args[i]->loc, Severity::Error,
                             "cannot pass region-backed reference or view to parameter " +
                             std::to_string(i + 1) + " of function '" + expr.callee +
                             "' which escapes to global storage");
            }
        }
    }

    ProvenanceSet retProv;
    if (summary && !summary->returnBorrowsFromParam.empty()) {
        for (int pIdx : summary->returnBorrowsFromParam) {
            if (pIdx >= 0 && pIdx < static_cast<int>(expr.args.size())) {
                retProv.merge(getExprProvenance(expr.args[pIdx].get()));
            }
        }
    } else if (fnTy->returnType && (fnTy->returnType->isReference() || fnTy->returnType->isArrayView() || fnTy->returnType->isRecord())) {
        for (size_t i = 0; i < expr.args.size(); ++i) {
            retProv.merge(getExprProvenance(expr.args[i].get()));
        }
    }

    if (!retProv.empty()) {
        setExprProvenance(&expr, retProv);
        if (retProv.hasRegion()) {
            setExprRegionProvenance(&expr, retProv.primaryRegionId());
        }
    }

    if (sym->isAsync) {
        return tc_.getTask(fnTy->returnType ? fnTy->returnType : tc_.getVoid());
    }

    return fnTy->returnType;
}

const Type* TypeChecker::checkIndex(IndexExpr& expr) {
    auto arrTy = checkExpr(*expr.array);
    auto idxTy = checkExpr(*expr.index);

    if (!arrTy) return tc_.getI32();
    if (arrTy->isPointer()) {
        if (!idxTy || !idxTy->isInteger()) {
            diag_.report(expr.index->loc, Severity::Error,
                         "pointer subscript index must be an integer, got " + (idxTy ? idxTy->toString() : "unknown"));
        }
        return arrTy->elementType ? arrTy->elementType : tc_.getI32();
    }
    if (arrTy->isReference()) arrTy = arrTy->elementType;

    if (arrTy->isSlab()) {
        if (!idxTy || !idxTy->isHandle()) {
            diag_.report(expr.index->loc, Severity::Error,
                         "slab subscript index must be a Handle, got " + (idxTy ? idxTy->toString() : "unknown"));
            return arrTy->elementType ? arrTy->elementType : tc_.getI32();
        }
        if (arrTy->elementType && idxTy->elementType && *arrTy->elementType != *idxTy->elementType) {
            diag_.report(expr.index->loc, Severity::Error,
                         "handle type mismatch: cannot index " + arrTy->toString() + " with " + idxTy->toString());
        }
        return arrTy->elementType;
    }

    if (arrTy->isSIMDVector()) {
        if (!idxTy || !idxTy->isInteger()) {
            diag_.report(expr.index->loc, Severity::Error,
                         "SIMD vector subscript index must be an integer, got " + (idxTy ? idxTy->toString() : "unknown"));
        }
        return arrTy->elementType ? arrTy->elementType : tc_.getF32();
    }

    if (!arrTy->isArray() && !arrTy->isArrayView()) {
        if (arrTy->isRecord()) {
            auto m = arrTy->findMethod("operator[]");
            if (!m) m = arrTy->findMethod("get");
            if (m && m->paramTypes.size() == 1) {
                const Type* expectedIdx = m->paramTypes[0];
                if (expectedIdx->isReference()) expectedIdx = expectedIdx->elementType;
                const Type* actualIdx = idxTy;
                if (actualIdx && actualIdx->isReference()) actualIdx = actualIdx->elementType;
                if (actualIdx && (*expectedIdx == *actualIdx || actualIdx->isImplicitlyConvertibleTo(*expectedIdx))) {
                    expr.overloadedMethod = m->name;
                    setExprType(&expr, m->returnType);
                    return m->returnType ? m->returnType : tc_.getI32();
                }
            }
        }
        diag_.report(expr.loc, Severity::Error,
                     "subscripted value is not an array, got " + arrTy->toString());
        return tc_.getI32();
    }

    if (expr.index->kind == ExprKind::Range) {
        auto& range = static_cast<RangeExpr&>(*expr.index);
        int64_t startVal = 0;
        bool startIsConst = false;
        if (range.start) {
            auto sTy = checkExpr(*range.start);
            if (sTy && !sTy->isInteger()) {
                diag_.report(range.start->loc, Severity::Error,
                             "range start must be an integer, got " + sTy->toString());
            }
            auto eval = evalIntConstExpr(*range.start);
            if (eval.isConst && !eval.overflow) {
                startVal = eval.value;
                startIsConst = true;
            }
        } else {
            startIsConst = true;
        }

        int64_t endVal = arrTy->arraySize >= 0 ? arrTy->arraySize : 0;
        bool endIsConst = false;
        if (range.end) {
            auto eTy = checkExpr(*range.end);
            if (eTy && !eTy->isInteger()) {
                diag_.report(range.end->loc, Severity::Error,
                             "range end must be an integer, got " + eTy->toString());
            }
            auto eval = evalIntConstExpr(*range.end);
            if (eval.isConst && !eval.overflow) {
                endVal = eval.value;
                endIsConst = true;
            }
        } else if (arrTy->arraySize >= 0) {
            endIsConst = true;
        }

        if (startIsConst && startVal < 0) {
            diag_.report(expr.index->loc, Severity::Error,
                         "range start cannot be negative: " + std::to_string(startVal));
        }
        if (startIsConst && endIsConst && startVal > endVal) {
            diag_.report(expr.index->loc, Severity::Error,
                         "range start (" + std::to_string(startVal) + ") cannot exceed range end (" +
                         std::to_string(endVal) + ")");
        }
        if (arrTy->isArray() && arrTy->arraySize >= 0 && endIsConst && endVal > arrTy->arraySize) {
            diag_.report(expr.index->loc, Severity::Error,
                         "range end (" + std::to_string(endVal) + ") exceeds fixed array length " +
                         std::to_string(arrTy->arraySize));
        }

        ProvenanceSet arrProv = getExprProvenance(expr.array.get());
        if (arrProv.empty()) {
            uint64_t arrRid = getExprRegionProvenance(expr.array.get());
            if (arrRid > 0) arrProv = ProvenanceSet(ProvenanceKind::LocalRegion, arrRid);
        }
        if (!arrProv.empty()) {
            setExprProvenance(&expr, arrProv);
            if (arrProv.hasRegion()) {
                setExprRegionProvenance(&expr, arrProv.primaryRegionId());
            }
        }

        return tc_.getArrayView(arrTy->elementType, arrTy->isConst);
    }

    if (idxTy && idxTy->isReference()) {
        idxTy = idxTy->elementType;
    }
    if (idxTy && !idxTy->isInteger()) {
        diag_.report(expr.index->loc, Severity::Error,
                     "array index must be an integer, got " + idxTy->toString());
    }

    if (parallelLoopScopeDepth_ > 0 && expr.array->kind == ExprKind::Identifier) {
        auto arrName = static_cast<IdentifierExpr&>(*expr.array).name;
        size_t arrDepth = syms_.lookupScopeDepth(arrName);
        if (arrDepth > 0 && arrDepth < parallelLoopScopeDepth_) {
            if (parallelLoopWrittenArrays_.count(arrName)) {
                if (expr.index->kind == ExprKind::Binary) {
                    auto& bin = static_cast<BinaryExpr&>(*expr.index);
                    if (bin.op == BinaryOp::Add || bin.op == BinaryOp::Sub) {
                        for (const auto& indVar : parallelLoopInductionVars_) {
                            if ((bin.lhs->kind == ExprKind::Identifier &&
                                 static_cast<IdentifierExpr&>(*bin.lhs).name == indVar) ||
                                (bin.rhs->kind == ExprKind::Identifier &&
                                 static_cast<IdentifierExpr&>(*bin.rhs).name == indVar)) {
                                diag_.report(expr.loc, Severity::Error,
                                             "data-race safety violation: loop-carried dependency on array '" + arrName +
                                             "' in parallel loop; iterations read and write across iteration boundaries");
                                break;
                            }
                        }
                    }
                }
            }
        }
    }

    // Compile-time bounds checking for fixed arrays with constant indices
    if (arrTy->isArray() && arrTy->arraySize >= 0 && expr.index->kind == ExprKind::IntLiteral) {
        auto idxVal = static_cast<IntLiteralExpr*>(expr.index.get())->value;
        if (idxVal < 0 || idxVal >= arrTy->arraySize) {
            diag_.report(expr.index->loc, Severity::Error,
                         "array index out of bounds: index " + std::to_string(idxVal) +
                         " exceeds fixed array length " + std::to_string(arrTy->arraySize));
        }
    }

    if (expr.array->kind == ExprKind::Identifier && expr.index->kind == ExprKind::IntLiteral) {
        auto& id = static_cast<IdentifierExpr&>(*expr.array);
        auto idxVal = static_cast<IntLiteralExpr&>(*expr.index).value;
        if (isArrayElementUninitialized(id.name, idxVal) &&
            isArrayPartiallyInitialized(id.name, arrTy->arraySize)) {
            diag_.report(expr.loc, Severity::Error,
                         "array element '" + id.name + "[" + std::to_string(idxVal) + "]' is used before being initialized");
        }
    }

    ProvenanceSet arrProv = getExprProvenance(expr.array.get());
    if (arrProv.empty()) {
        uint64_t arrRid = getExprRegionProvenance(expr.array.get());
        if (arrRid > 0) arrProv = ProvenanceSet(ProvenanceKind::LocalRegion, arrRid);
    }
    if (arrTy->elementType && (arrTy->elementType->isReference() || arrTy->elementType->isArrayView() || arrTy->elementType->canContainBorrow())) {
        if (!arrProv.empty()) {
            setExprProvenance(&expr, arrProv);
            if (arrProv.hasRegion()) {
                setExprRegionProvenance(&expr, arrProv.primaryRegionId());
            }
        }
    }

    return arrTy->elementType;
}

const Type* TypeChecker::checkField(FieldExpr& expr) {
    auto baseTy = checkExpr(*expr.object);
    if (!baseTy) return tc_.getI32();

    if (baseTy->isReference()) {
        baseTy = baseTy->elementType;
    }

    if (baseTy->isHandle()) {
        if (expr.fieldName == "slot" || expr.fieldName == "slot_index") {
            return tc_.getU32();
        }
        if (expr.fieldName == "generation" || expr.fieldName == "gen") {
            return tc_.getU32();
        }
        diag_.report(expr.loc, Severity::Error,
                     "unknown field '" + expr.fieldName + "' on " + baseTy->toString());
        return tc_.getI32();
    }

    if (baseTy->isSIMDVector()) {
        uint32_t lane = 0xFFFFFFFF;
        if (expr.fieldName == "x" || expr.fieldName == "r") lane = 0;
        else if (expr.fieldName == "y" || expr.fieldName == "g") lane = 1;
        else if (expr.fieldName == "z" || expr.fieldName == "b") lane = 2;
        else if (expr.fieldName == "w" || expr.fieldName == "a") lane = 3;

        if (lane != 0xFFFFFFFF && lane < baseTy->vectorWidth) {
            return baseTy->elementType ? baseTy->elementType : tc_.getF32();
        }
        diag_.report(expr.loc, Severity::Error,
                     "invalid swizzle/component '" + expr.fieldName + "' on " + baseTy->toString());
        return baseTy->elementType ? baseTy->elementType : tc_.getF32();
    }

    if (!baseTy->isRecord()) {
        diag_.report(expr.loc, Severity::Error,
                     "cannot access member '" + expr.fieldName + "' on non-record type " +
                     baseTy->toString());
        return tc_.getI32();
    }

    auto f = baseTy->findField(expr.fieldName);
    if (!f) {
        diag_.report(expr.loc, Severity::Error,
                     "type '" + baseTy->structName + "' has no member named '" +
                     expr.fieldName + "'");
        return tc_.getI32();
    }

    // Access control check
    bool isAccessible = true;
    if (f->access == 1 /* Private */) {
        if (!currentRecord_ || (currentRecord_ != baseTy && currentRecord_->structName != baseTy->structName)) {
            isAccessible = false;
        }
    } else if (f->access == 2 /* Protected */) {
        if (!currentRecord_ || (!currentRecord_->inheritsFrom(baseTy) && currentRecord_ != baseTy && currentRecord_->structName != baseTy->structName)) {
            isAccessible = false;
        }
    }
    if (!isAccessible) {
        diag_.report(expr.loc, Severity::Error,
                     "cannot access " + std::string(f->access == 1 ? "private" : "protected") +
                     " member '" + expr.fieldName + "' of class '" + baseTy->structName + "'");
        return tc_.getI32();
    }

    if (expr.object->kind == ExprKind::Identifier) {
        auto& id = static_cast<IdentifierExpr&>(*expr.object);
        if (isFieldUninitialized(id.name, expr.fieldName)) {
            diag_.report(expr.loc, Severity::Error,
                         "field '" + expr.fieldName + "' of variable '" + id.name + "' is used before being initialized");
        }
    }

    ProvenanceSet fieldProv;
    auto lval = resolveLValuePath(&expr);
    if (lval.isValid && lval.bindingId > 0) {
        fieldProv = getFieldProvenance(lval.bindingId, lval.path);
    }
    if (fieldProv.empty() && expr.object->kind == ExprKind::Identifier) {
        auto& id = static_cast<IdentifierExpr&>(*expr.object);
        fieldProv = getFieldProvenance(id.name, expr.fieldName);
    }
    if (fieldProv.empty()) {
        fieldProv = getExprProvenance(expr.object.get());
    }
    if (expr.object->kind == ExprKind::This && fieldProv.empty()) {
        fieldProv = ProvenanceSet(ProvenanceKind::ThisBorrow, 0, "this");
    }
    if (f->type && (f->type->isArray() || f->type->isReference() || f->type->isArrayView() || f->type->canContainBorrow())) {
        if (!fieldProv.empty()) {
            setExprProvenance(&expr, fieldProv);
            if (fieldProv.hasRegion()) {
                setExprRegionProvenance(&expr, fieldProv.primaryRegionId());
            }
        }
    }

    return f->type ? f->type : tc_.getI32();
}

const Type* TypeChecker::checkCast(CastExpr& expr) {
    auto targetTy = resolveType(*expr.targetType);
    auto opTy = checkExpr(*expr.operand);
    if (!targetTy || !opTy) return targetTy ? targetTy : tc_.getI32();

    if (opTy->isFunction()) {
        diag_.report(expr.loc, Severity::Error,
                     "closure type '" + opTy->toString() +
                     "' has a 16-byte fat-pointer layout and cannot cross C ABI boundaries directly");
        return targetTy;
    }
    if (targetTy->isFunction()) {
        diag_.report(expr.loc, Severity::Error,
                     "closure type '" + targetTy->toString() +
                     "' has a 16-byte fat-pointer layout and cannot cross C ABI boundaries directly");
        return targetTy;
    }

    // Reject integer-to-pointer cast laundering (prevents bypassing borrow checker & lifetime escape)
    if (!opTy->isReference() && targetTy->isReference()) {
        diag_.report(expr.loc, Severity::Error,
                     "cannot cast integer of type '" + opTy->toString() +
                     "' to pointer or reference type '" + targetTy->toString() +
                     "'; integer-to-pointer provenance laundering is disallowed");
        return targetTy;
    }

    // Reject pointer/reference type punning (prevents arbitrary memory reinterpretation and pointer forging)
    if (opTy->isReference() && targetTy->isReference()) {
        const Type* opElem = opTy->elementType;
        const Type* targetElem = targetTy->elementType;
        bool isCompatible = false;
        if (opElem && targetElem) {
            if (*opElem == *targetElem) {
                isCompatible = true;
            } else if (opElem->isVoid() || targetElem->isVoid()) {
                isCompatible = true;
            } else if (opElem->isRecord() && targetElem->isRecord() &&
                       (opElem->inheritsFrom(targetElem) || targetElem->inheritsFrom(opElem))) {
                isCompatible = true;
            }
        }
        if (!isCompatible) {
            diag_.report(expr.loc, Severity::Error,
                         "cannot cast pointer/reference from '" + opTy->toString() +
                         "' to incompatible type '" + targetTy->toString() +
                         "'; pointer type punning is disallowed");
            return targetTy;
        }
    }

    ProvenanceSet opProv = getExprProvenance(expr.operand.get());
    if (opProv.empty()) {
        uint64_t opRid = getExprRegionProvenance(expr.operand.get());
        if (opRid > 0) opProv = ProvenanceSet(ProvenanceKind::LocalRegion, opRid);
    }
    if (!opProv.empty()) {
        setExprProvenance(&expr, opProv);
        if (opProv.hasRegion()) {
            setExprRegionProvenance(&expr, opProv.primaryRegionId());
        }
    }
    return targetTy;
}

const Type* TypeChecker::checkArrayInit(ArrayInitExpr& expr) {
    if (expr.elements.empty()) {
        return tc_.getArray(tc_.getI32(), 0);
    }
    const Type* elemTy = nullptr;
    for (auto& el : expr.elements) {
        auto ty = checkExpr(*el);
        if (ty && ty->hasNonTrivialDestructor() && !isRValue(el.get())) {
            std::string typeName = ty->isEnum() ? ty->enumName : ty->structName;
            std::string srcName = (el->kind == ExprKind::Identifier) ? static_cast<IdentifierExpr&>(*el).name : "element";
            diag_.report(el->loc, Severity::Error,
                         "cannot copy resource-owning " +
                         std::string(ty->isClass() ? "class '" : "type '") +
                         typeName + "' into array element; use 'move(" + srcName + ")'");
        }
        if (!elemTy) elemTy = ty;
        else if (ty && *ty != *elemTy && !ty->isImplicitlyConvertibleTo(*elemTy)) {
            diag_.report(el->loc, Severity::Error,
                         "array initializer element type mismatch: expected " +
                         elemTy->toString() + ", got " + ty->toString());
        }
    }
    return tc_.getArray(elemTy ? elemTy : tc_.getI32(),
                        static_cast<int64_t>(expr.elements.size()));
}

const Type* TypeChecker::checkCompoundInit(CompoundInitExpr& expr) {
    const Type* structTy = nullptr;
    if (expr.type) {
        structTy = resolveType(*expr.type);
    } else {
        structTy = getExprType(&expr);
    }

    if (!structTy || !structTy->isStruct()) {
        if (expr.type) {
            diag_.report(expr.loc, Severity::Error,
                         "compound literal type must be a struct, got " +
                         (structTy ? structTy->toString() : "unknown"));
        }
        return tc_.getI32();
    }

    ProvenanceSet aggregateProv;
    for (size_t i = 0; i < expr.fields.size(); ++i) {
        auto& f = expr.fields[i];
        const StructFieldType* sft = nullptr;
        if (!f.fieldName.empty()) {
            sft = structTy->findField(f.fieldName);
            if (!sft) {
                diag_.report(f.loc, Severity::Error,
                             "struct '" + structTy->structName + "' has no field named '" +
                             f.fieldName + "'");
                continue;
            }
        } else {
            if (i < structTy->structFields.size()) {
                sft = &structTy->structFields[i];
            } else {
                diag_.report(f.loc, Severity::Error,
                             "too many initializers for struct '" + structTy->structName + "'");
                continue;
            }
        }

        if (sft && sft->type) {
            if (sft->type->hasNonTrivialDestructor() && !isRValue(f.value.get())) {
                std::string typeName = sft->type->isEnum() ? sft->type->enumName : sft->type->structName;
                std::string srcName = (f.value->kind == ExprKind::Identifier) ? static_cast<IdentifierExpr&>(*f.value).name : "value";
                diag_.report(f.loc, Severity::Error,
                             "cannot copy resource-owning " +
                             std::string(sft->type->isClass() ? "class '" : "type '") +
                             typeName + "' into field '" + sft->name + "'; use 'move(" + srcName + ")'");
            }
            if (sft->type->isStruct() && f.value->kind == ExprKind::ArrayInit) {
                auto* aInit = static_cast<ArrayInitExpr*>(f.value.get());
                std::vector<StructInitField> sFields;
                for (auto& elem : aInit->elements) {
                    auto eLoc = elem->loc;
                    sFields.push_back(StructInitField{"", std::move(elem), eLoc});
                }
                auto cInit = std::make_unique<CompoundInitExpr>(nullptr, std::move(sFields), f.value->loc);
                setExprType(cInit.get(), sft->type);
                f.value = std::move(cInit);
            } else if (sft->type->isStruct() && f.value->kind == ExprKind::CompoundInit) {
                auto* cInit = static_cast<CompoundInitExpr*>(f.value.get());
                if (!cInit->type) {
                    setExprType(cInit, sft->type);
                }
            }
            auto valTy = checkExpr(*f.value);
            ProvenanceSet fProv = getExprProvenance(f.value.get());
            if (!fProv.empty()) {
                aggregateProv.merge(fProv);
            }
            if (valTy && *valTy != *sft->type && !valTy->isImplicitlyConvertibleTo(*sft->type)) {
                diag_.report(f.loc, Severity::Error,
                             "type mismatch initializing field '" + sft->name + "': expected " +
                             sft->type->toString() + ", got " + valTy->toString());
            }
        }
    }

    if (!aggregateProv.empty()) {
        setExprProvenance(&expr, aggregateProv);
        if (aggregateProv.hasRegion()) {
            setExprRegionProvenance(&expr, aggregateProv.primaryRegionId());
        }
    }

    return structTy;
}

const Type* TypeChecker::checkThis(ThisExpr& expr) {
    if (!currentRecord_) {
        diag_.report(expr.loc, Severity::Error,
                     "'this' is only available inside member methods or constructors");
        return tc_.getI32();
    }
    setExprProvenance(&expr, ProvenanceSet(ProvenanceKind::ThisBorrow, 0, "this"));
    return tc_.getReference(currentRecord_, isCurrentMethodConst_);
}

const Type* TypeChecker::checkMethodCall(MethodCallExpr& expr) {
    if (expr.object->kind == ExprKind::Identifier) {
        auto& id = static_cast<IdentifierExpr&>(*expr.object);
        markAllFieldsAssigned(id.name);
    }
    auto baseTy = checkExpr(*expr.object);
    if (!baseTy) return tc_.getI32();

    if (baseTy->isReference()) {
        baseTy = baseTy->elementType;
    }

    if (baseTy->isSlab()) {
        const std::string& method = expr.methodName;
        if (method == "insert") {
            if (expr.args.size() != 1) {
                diag_.report(expr.loc, Severity::Error,
                             "slab.insert expects exactly 1 argument, got " + std::to_string(expr.args.size()));
            } else {
                auto argTy = checkExpr(*expr.args[0]);
                if (argTy && baseTy->elementType && *argTy != *baseTy->elementType &&
                    !argTy->isImplicitlyConvertibleTo(*baseTy->elementType)) {
                    diag_.report(expr.args[0]->loc, Severity::Error,
                                 "cannot insert element of type " + argTy->toString() +
                                 " into " + baseTy->toString());
                }
            }
            return tc_.getHandle(baseTy->elementType);
        }
        if (method == "get") {
            if (expr.args.size() != 1) {
                diag_.report(expr.loc, Severity::Error,
                             "slab.get expects exactly 1 argument (Handle<T>), got " + std::to_string(expr.args.size()));
            } else {
                auto argTy = checkExpr(*expr.args[0]);
                if (!argTy || !argTy->isHandle()) {
                    diag_.report(expr.args[0]->loc, Severity::Error,
                                 "slab.get argument must be a Handle, got " + (argTy ? argTy->toString() : "unknown"));
                } else if (baseTy->elementType && argTy->elementType && *baseTy->elementType != *argTy->elementType) {
                    diag_.report(expr.args[0]->loc, Severity::Error,
                                 "handle type mismatch: cannot retrieve from " + baseTy->toString() + " using " + argTy->toString());
                }
            }
            return tc_.getReference(baseTy->elementType, false);
        }
        if (method == "is_valid") {
            if (expr.args.size() != 1) {
                diag_.report(expr.loc, Severity::Error,
                             "slab.is_valid expects exactly 1 argument (Handle<T>), got " + std::to_string(expr.args.size()));
            } else {
                auto argTy = checkExpr(*expr.args[0]);
                if (!argTy || !argTy->isHandle()) {
                    diag_.report(expr.args[0]->loc, Severity::Error,
                                 "slab.is_valid argument must be a Handle, got " + (argTy ? argTy->toString() : "unknown"));
                }
            }
            return tc_.getBool();
        }
        if (method == "try_get") {
            if (expr.args.size() != 2) {
                diag_.report(expr.loc, Severity::Error,
                             "slab.try_get expects 2 arguments: (Handle<T>, out_ref), got " + std::to_string(expr.args.size()));
            } else {
                auto arg0Ty = checkExpr(*expr.args[0]);
                auto arg1Ty = checkExpr(*expr.args[1]);
                if (!arg0Ty || !arg0Ty->isHandle()) {
                    diag_.report(expr.args[0]->loc, Severity::Error,
                                 "slab.try_get first argument must be a Handle, got " + (arg0Ty ? arg0Ty->toString() : "unknown"));
                }
                const Type* targetElem = arg1Ty;
                if (targetElem && targetElem->isReference()) targetElem = targetElem->elementType;
                if (baseTy->elementType && targetElem && *baseTy->elementType != *targetElem) {
                    diag_.report(expr.args[1]->loc, Severity::Error,
                                 "slab.try_get output target type mismatch: " + targetElem->toString() + " vs " + baseTy->elementType->toString());
                }
            }
            return tc_.getBool();
        }
        if (method == "remove") {
            if (expr.args.size() != 1) {
                diag_.report(expr.loc, Severity::Error,
                             "slab.remove expects exactly 1 argument (Handle<T>), got " + std::to_string(expr.args.size()));
            } else {
                auto argTy = checkExpr(*expr.args[0]);
                if (!argTy || !argTy->isHandle()) {
                    diag_.report(expr.args[0]->loc, Severity::Error,
                                 "slab.remove argument must be a Handle, got " + (argTy ? argTy->toString() : "unknown"));
                }
            }
            return tc_.getBool();
        }
        if (method == "count") {
            if (!expr.args.empty()) {
                diag_.report(expr.loc, Severity::Error, "slab.count takes no arguments");
            }
            return tc_.getI64();
        }
        if (method == "capacity") {
            if (!expr.args.empty()) {
                diag_.report(expr.loc, Severity::Error, "slab.capacity takes no arguments");
            }
            return tc_.getI64();
        }
        if (method == "clear") {
            if (!expr.args.empty()) {
                diag_.report(expr.loc, Severity::Error, "slab.clear takes no arguments");
            }
            return tc_.getVoid();
        }
        diag_.report(expr.loc, Severity::Error,
                     "unknown method '" + method + "' on " + baseTy->toString());
        return tc_.getI32();
    }

    if (baseTy->isHandle()) {
        const std::string& method = expr.methodName;
        if (method == "is_valid") {
            if (!expr.args.empty()) {
                diag_.report(expr.loc, Severity::Error, "handle.is_valid takes no arguments");
            }
            return tc_.getBool();
        }
        if (method == "slot" || method == "slot_index") {
            if (!expr.args.empty()) {
                diag_.report(expr.loc, Severity::Error, "handle." + method + " takes no arguments");
            }
            return tc_.getU32();
        }
        if (method == "generation" || method == "gen") {
            if (!expr.args.empty()) {
                diag_.report(expr.loc, Severity::Error, "handle." + method + " takes no arguments");
            }
            return tc_.getU32();
        }
        diag_.report(expr.loc, Severity::Error,
                     "unknown method '" + method + "' on " + baseTy->toString());
        return tc_.getI32();
    }

    if (!baseTy->isRecord()) {
        diag_.report(expr.loc, Severity::Error,
                     "cannot call method '" + expr.methodName + "' on non-record type " +
                     baseTy->toString());
        return tc_.getI32();
    }

    auto m = baseTy->findMethod(expr.methodName);
    if (!m) {
        diag_.report(expr.loc, Severity::Error,
                     "type '" + baseTy->structName + "' has no method named '" +
                     expr.methodName + "'");
        return tc_.getI32();
    }

    // Access control
    bool isAccessible = true;
    if (m->access == 1 /* Private */) {
        if (!currentRecord_ || (currentRecord_ != baseTy && currentRecord_->structName != baseTy->structName)) {
            isAccessible = false;
        }
    } else if (m->access == 2 /* Protected */) {
        if (!currentRecord_ || (!currentRecord_->inheritsFrom(baseTy) && currentRecord_ != baseTy && currentRecord_->structName != baseTy->structName)) {
            isAccessible = false;
        }
    }
    if (!isAccessible) {
        diag_.report(expr.loc, Severity::Error,
                     "cannot call " + std::string(m->access == 1 ? "private" : "protected") +
                     " method '" + expr.methodName + "' of class '" + baseTy->structName + "'");
        return tc_.getI32();
    }

    // Const checking
    bool isObjectConst = false;
    auto objType = getExprType(expr.object.get());
    if (objType && objType->isReference() && objType->isConstRef) isObjectConst = true;
    if (expr.object->kind == ExprKind::Identifier) {
        auto sym = syms_.lookup(static_cast<IdentifierExpr*>(expr.object.get())->name);
        if (sym && (sym->isConst || (sym->type->isReference() && sym->type->isConstRef))) {
            isObjectConst = true;
        }
    } else if (expr.object->kind == ExprKind::This && isCurrentMethodConst_) {
        isObjectConst = true;
    }

    if (isObjectConst && !m->isConst) {
        diag_.report(expr.loc, Severity::Error,
                     "cannot call non-const method '" + expr.methodName +
                     "' on const instance of '" + baseTy->structName + "'");
    }

    if (!m->isConst && !activeLambdaStack_.empty() && expr.object->kind == ExprKind::Identifier) {
        auto idName = static_cast<IdentifierExpr*>(expr.object.get())->name;
        size_t symDepth = syms_.lookupScopeDepth(idName);
        auto sym = syms_.lookup(idName);
        if (sym && !sym->isFunction && !sym->isGlobal) {
            for (auto& lctx : activeLambdaStack_) {
                if (symDepth > 1 && symDepth < lctx.scopeDepth) {
                    lctx.captures[idName] = LambdaCapture{idName, sym->type, true, symDepth, expr.loc};
                }
            }
        }
    }

    // Argument checking
    if (expr.args.size() < m->paramTypes.size() && m->defaultArgs.size() == m->paramTypes.size()) {
        for (size_t i = expr.args.size(); i < m->paramTypes.size(); ++i) {
            if (!m->defaultArgs[i]) break;
            expr.args.push_back(Monomorphizer::substituteExpr(m->defaultArgs[i], {}));
        }
    }
    if (expr.args.size() != m->paramTypes.size()) {
        diag_.report(expr.loc, Severity::Error,
                     "method '" + expr.methodName + "' expects " +
                     std::to_string(m->paramTypes.size()) + " arguments, got " +
                     std::to_string(expr.args.size()));
        return m->returnType ? m->returnType : tc_.getVoid();
    }

    for (size_t i = 0; i < expr.args.size(); ++i) {
        auto paramTy = m->paramTypes[i];
        if (paramTy && paramTy->isEnum() && expr.args[i]->kind == ExprKind::Identifier) {
            auto& id = static_cast<IdentifierExpr&>(*expr.args[i]);
            if (id.name == "Option::None" && paramTy->enumName.rfind("Option<", 0) == 0) {
                setExprType(&id, paramTy);
            }
        }
        auto argTy = checkExpr(*expr.args[i]);
        auto expectedTy = m->paramTypes[i];
        if (expectedTy && expectedTy->hasNonTrivialDestructor() && !expectedTy->isReference()) {
            diag_.report(expr.args[i]->loc, Severity::Error,
                         "cannot pass resource-owning " +
                         std::string(expectedTy->isClass() ? "class '" : "type '") +
                         expectedTy->structName + "' by value; pass by reference instead");
        }
        if (argTy && expectedTy && !argTy->isImplicitlyConvertibleTo(*expectedTy)) {
            diag_.report(expr.args[i]->loc, Severity::Error,
                         "argument " + std::to_string(i + 1) + " type mismatch in call to '" +
                         expr.methodName + "': expected " + expectedTy->toString() +
                         ", got " + argTy->toString());
        }
    }

    checkCallSiteBorrowSafety(expr.methodName, expr.args, m->paramTypes, expr.loc);

    std::string methodMangled = baseTy->structName + "_" + expr.methodName;
    const FunctionBorrowSummary* summary = getFunctionSummary(methodMangled);
    if (!summary) {
        summary = getFunctionSummary(expr.methodName);
    }

    ProvenanceSet retProv;
    ProvenanceSet objProv = getExprProvenance(expr.object.get());
    if (objProv.empty()) {
        uint64_t objRid = getExprRegionProvenance(expr.object.get());
        if (objRid > 0) objProv = ProvenanceSet(ProvenanceKind::LocalRegion, objRid);
    }

    bool isExternalMethod = (summary && summary->isExternal);
    if (!summary) {
        if (m->isPureVirtual) isExternalMethod = true;
    }

    if (objProv.hasNonStaticBorrowOrRegion()) {
        if (isExternalMethod || (summary && summary->isReceiverUnknown())) {
            diag_.report(expr.object->loc, Severity::Error,
                         "cannot call unknown external method '" + expr.methodName +
                         "' on borrowed or region-backed receiver without verified no-escape contract");
        } else if (summary && summary->thisEscapes()) {
            diag_.report(expr.object->loc, Severity::Error,
                         "cannot call method '" + expr.methodName +
                         "' on borrowed or region-backed receiver because 'this' escapes to static or global storage");
        }
    }

    for (size_t i = 0; i < expr.args.size(); ++i) {
        ProvenanceSet aProv = getExprProvenance(expr.args[i].get());
        if (aProv.empty()) {
            uint64_t aRid = getExprRegionProvenance(expr.args[i].get());
            if (aRid > 0) aProv = ProvenanceSet(ProvenanceKind::LocalRegion, aRid);
        }
        if (aProv.hasNonStaticBorrowOrRegion()) {
            if (isExternalMethod || (summary && summary->isParamUnknown(i))) {
                diag_.report(expr.args[i]->loc, Severity::Error,
                             "cannot pass borrowed parameter, reference, or view to unknown external method '" +
                             expr.methodName + "' without verified no-escape contract");
            } else if (summary && summary->paramEscapes(i)) {
                diag_.report(expr.args[i]->loc, Severity::Error,
                             "cannot pass region-backed reference or view to parameter " +
                             std::to_string(i + 1) + " of method '" + expr.methodName +
                             "' which escapes to global storage");
            }
        }
    }

    if (summary) {
        if (summary->returnBorrowsFromThis) {
            retProv.merge(objProv);
        }
        for (int pIdx : summary->returnBorrowsFromParam) {
            if (pIdx >= 0 && pIdx < static_cast<int>(expr.args.size())) {
                retProv.merge(getExprProvenance(expr.args[pIdx].get()));
            }
        }
    } else {
        if (m->returnType && (m->returnType->isReference() || m->returnType->isArrayView() || m->returnType->isRecord())) {
            retProv.merge(objProv);
            for (auto& arg : expr.args) {
                retProv.merge(getExprProvenance(arg.get()));
            }
        }
    }

    if (!retProv.empty() && m->returnType && (m->returnType->isReference() || m->returnType->isArrayView() || m->returnType->isRecord())) {
        setExprProvenance(&expr, retProv);
        if (retProv.hasRegion()) {
            setExprRegionProvenance(&expr, retProv.primaryRegionId());
        }
    }

    const Type* resRetTy = m->returnType ? m->returnType : tc_.getVoid();
    if (m->isAsync) {
        return tc_.getTask(resRetTy);
    }
    return resRetTy;
}

const Type* TypeChecker::checkConstruct(ConstructExpr& expr) {
    if (expr.typeName == "float4" || expr.typeName == "float8" ||
        expr.typeName == "int4" || expr.typeName == "int8" || expr.typeName == "int16") {
        uint32_t width = 4;
        const Type* simdTy = tc_.getFloat4();
        if (expr.typeName == "float8") {
            width = 8;
            simdTy = tc_.getFloat8();
        } else if (expr.typeName == "int4") {
            simdTy = tc_.getInt4();
        } else if (expr.typeName == "int8") {
            width = 8;
            simdTy = tc_.getInt8();
        } else if (expr.typeName == "int16") {
            width = 16;
            simdTy = tc_.getInt16();
        }

        if (expr.args.empty()) {
            diag_.report(expr.loc, Severity::Error, expr.typeName + " constructor requires at least 1 argument");
            return simdTy;
        }

        if (expr.args.size() == 1) {
            auto argTy = checkExpr(*expr.args[0]);
            if (argTy && !argTy->isNumeric()) {
                diag_.report(expr.args[0]->loc, Severity::Error,
                             "SIMD splat argument must be numeric, got " + argTy->toString());
            }
            return simdTy;
        }

        if (expr.args.size() == width) {
            for (auto& arg : expr.args) {
                auto argTy = checkExpr(*arg);
                if (argTy && !argTy->isNumeric()) {
                    diag_.report(arg->loc, Severity::Error,
                                 "SIMD vector component must be numeric, got " + argTy->toString());
                }
            }
            return simdTy;
        }

        diag_.report(expr.loc, Severity::Error,
                     expr.typeName + " constructor expects 1 argument (splat) or " +
                     std::to_string(width) + " component arguments, got " +
                     std::to_string(expr.args.size()));
        return simdTy;
    }

    if (expr.typeName == "Slab") {
        if (!expr.args.empty()) {
            if (expr.args.size() > 1) {
                diag_.report(expr.loc, Severity::Error,
                             "Slab constructor takes at most 1 capacity argument");
            } else {
                auto argTy = checkExpr(*expr.args[0]);
                if (argTy && !argTy->isInteger()) {
                    diag_.report(expr.args[0]->loc, Severity::Error,
                                 "Slab capacity argument must be an integer, got " + argTy->toString());
                }
            }
        }
        return tc_.getSlab(tc_.getI32());
    }

    auto recTy = resolveNamedType(expr.typeName, expr.loc);
    if (!recTy || !recTy->isRecord()) {
        if (expr.typeName.find("::") == std::string::npos) {
            diag_.report(expr.loc, Severity::Error,
                         "unresolved type in constructor call: " + expr.typeName);
        }
        return tc_.getI32();
    }
    expr.typeName = recTy->structName;

    if (recTy->isAbstract) {
        diag_.report(expr.loc, Severity::Error,
                     "cannot instantiate abstract class '" + recTy->structName +
                     "' with pure virtual methods");
        return recTy;
    }

    if (recTy->constructors.empty()) {
        if (!expr.args.empty()) {
            diag_.report(expr.loc, Severity::Error,
                         "type '" + recTy->structName + "' has no constructor taking " +
                         std::to_string(expr.args.size()) + " arguments");
        }
        return recTy;
    }

    bool matched = false;
    for (const auto& ctor : recTy->constructors) {
        if (ctor.paramTypes.size() == expr.args.size()) {
            bool typesMatch = true;
            for (size_t i = 0; i < expr.args.size(); ++i) {
                auto argTy = checkExpr(*expr.args[i]);
                if (argTy && !argTy->isImplicitlyConvertibleTo(*ctor.paramTypes[i])) {
                    typesMatch = false;
                    break;
                }
            }
            if (typesMatch) {
                matched = true;
                if (ctor.access == 1 /* Private */) {
                    if (!currentRecord_ || (currentRecord_ != recTy && currentRecord_->structName != recTy->structName)) {
                        diag_.report(expr.loc, Severity::Error,
                                     "cannot call private constructor of class '" + recTy->structName + "'");
                    }
                } else if (ctor.access == 2 /* Protected */) {
                    if (!currentRecord_ || (!currentRecord_->inheritsFrom(recTy) && currentRecord_ != recTy && currentRecord_->structName != recTy->structName)) {
                        diag_.report(expr.loc, Severity::Error,
                                     "cannot call protected constructor of class '" + recTy->structName + "'");
                    }
                }
                break;
            }
        }
    }

    if (!matched) {
        diag_.report(expr.loc, Severity::Error,
                     "no matching constructor for '" + recTy->structName +
                     "' taking " + std::to_string(expr.args.size()) + " arguments");
    }

    return recTy;
}

const Type* TypeChecker::checkMatchExpr(ast::MatchExpr& expr) {
    if (!expr.expr) return tc_.getI32();
    auto targetTy = checkExpr(*expr.expr);
    if (!targetTy) return tc_.getI32();

    if (expr.expr->kind == ExprKind::Identifier && targetTy && targetTy->hasNonTrivialDestructor()) {
        auto& id = static_cast<IdentifierExpr&>(*expr.expr);
        checkBorrowMove(id.name, expr.loc);
        markMoved(id.name);
    }

    const Type* commonResultTy = nullptr;

    if (targetTy->isEnum()) {
        std::vector<bool> matchedVariants(targetTy->enumVariants.size(), false);
        bool hasCatchAll = false;

        for (auto& c : expr.cases) {
            if (hasCatchAll) {
                diag_.report(c.loc, Severity::Warning, "unreachable pattern in match expression");
            }

            pushScope();
            syms_.pushScope();

            if (c.pattern.kind == MatchPatternKind::Wildcard) {
                hasCatchAll = true;
            } else if (c.pattern.kind == MatchPatternKind::Binding) {
                hasCatchAll = true;
                if (!c.pattern.bindings.empty()) {
                    Symbol sym;
                    sym.name = c.pattern.bindings[0];
                    sym.type = targetTy;
                    sym.isConst = true;
                    sym.loc = c.pattern.loc;
                    syms_.declare(c.pattern.bindings[0], std::move(sym));
                    markAssigned(c.pattern.bindings[0]);
                }
            } else if (c.pattern.kind == MatchPatternKind::Variant) {
                int varIdx = targetTy->getVariantIndex(c.pattern.variantName);
                if (varIdx < 0) {
                    diag_.report(c.pattern.loc, Severity::Error,
                        "variant '" + c.pattern.variantName + "' is not a member of enum '" + targetTy->enumName + "'");
                } else {
                    if (matchedVariants[varIdx] && !c.guard) {
                        diag_.report(c.pattern.loc, Severity::Warning,
                            "unreachable pattern: variant '" + c.pattern.variantName + "' has already been matched");
                    }
                    if (!c.guard) {
                        matchedVariants[varIdx] = true;
                    }
                    const auto& v = targetTy->enumVariants[varIdx];
                    if (c.pattern.bindings.size() != v.payloadTypes.size()) {
                        diag_.report(c.pattern.loc, Severity::Error,
                            "variant pattern '" + c.pattern.variantName + "' binds " +
                            std::to_string(c.pattern.bindings.size()) + " variables, but variant has " +
                            std::to_string(v.payloadTypes.size()) + " payload fields");
                    } else {
                        for (size_t i = 0; i < c.pattern.bindings.size(); ++i) {
                            Symbol sym;
                            sym.name = c.pattern.bindings[i];
                            sym.type = v.payloadTypes[i];
                            sym.isConst = true;
                            sym.isConst = false;
                            sym.loc = c.pattern.loc;
                            syms_.declare(c.pattern.bindings[i], std::move(sym));
                            markAssigned(c.pattern.bindings[i]);
                        }
                    }
                }
            }

            if (c.guard) {
                auto gTy = checkExpr(*c.guard);
                if (gTy && !gTy->isBool()) {
                    diag_.report(c.guard->loc, Severity::Error,
                        "match guard condition must be of type 'bool', got " + gTy->toString());
                }
            }

            const Type* armTy = nullptr;
            if (c.body) {
                if (c.body->kind == StmtKind::Expr) {
                    auto& es = static_cast<ExprStmt&>(*c.body);
                    if (es.expr) {
                        armTy = checkExpr(*es.expr);
                    }
                } else {
                    checkStmt(*c.body, currentFuncReturnType_);
                }
            }

            if (armTy) {
                if (!commonResultTy) {
                    commonResultTy = armTy;
                } else if (*armTy != *commonResultTy) {
                    if (armTy->isImplicitlyConvertibleTo(*commonResultTy)) {
                        // fine
                    } else if (commonResultTy->isImplicitlyConvertibleTo(*armTy)) {
                        commonResultTy = armTy;
                    } else {
                        diag_.report(c.loc, Severity::Error,
                            "match arms have incompatible types: " + armTy->toString() + " vs " + commonResultTy->toString());
                    }
                }
            }

            syms_.popScope();
            popScope();
        }

        if (!hasCatchAll) {
            for (size_t i = 0; i < matchedVariants.size(); ++i) {
                if (!matchedVariants[i]) {
                    diag_.report(expr.loc, Severity::Error,
                        "non-exhaustive patterns in match: missing variant '" + targetTy->enumVariants[i].name + "'");
                }
            }
        }
    }

    return commonResultTy ? commonResultTy : tc_.getI32();
}

const Type* TypeChecker::checkTryExpr(ast::TryExpr& expr) {
    if (!expr.operand) return tc_.getI32();
    auto operandTy = checkExpr(*expr.operand);
    if (!operandTy) return tc_.getI32();

    if (!operandTy->isEnum()) {
        diag_.report(expr.loc, Severity::Error,
            "the '?' operator can only be applied to 'Option' or 'Result' types, got " + operandTy->toString());
        return tc_.getI32();
    }

    if (operandTy->enumName.rfind("Option", 0) == 0) {
        auto someVar = operandTy->findVariant("Some");
        if (!someVar || someVar->payloadTypes.empty()) {
            return tc_.getI32();
        }
        return someVar->payloadTypes[0];
    }

    if (operandTy->enumName.rfind("Result", 0) == 0) {
        auto okVar = operandTy->findVariant("Ok");
        if (!okVar || okVar->payloadTypes.empty()) {
            return tc_.getI32();
        }
        return okVar->payloadTypes[0];
    }

    diag_.report(expr.loc, Severity::Error,
        "the '?' operator can only be applied to 'Option' or 'Result' types, got " + operandTy->toString());
    return tc_.getI32();
}

const Type* TypeChecker::checkTernary(ast::TernaryExpr& expr) {
    if (!expr.condition || !expr.thenExpr || !expr.elseExpr) return tc_.getI32();

    auto condTy = checkExpr(*expr.condition);
    auto thenTy = checkExpr(*expr.thenExpr);
    auto elseTy = checkExpr(*expr.elseExpr);

    if (condTy && !condTy->isBool() && !condTy->isInteger() && !condTy->isReference()) {
        diag_.report(expr.condition->loc, Severity::Error,
                     "ternary condition must be convertible to boolean, got " + condTy->toString());
    }

    if (!thenTy || !elseTy) return tc_.getI32();

    const Type* resultTy = nullptr;
    if (*thenTy == *elseTy) {
        resultTy = thenTy;
    } else if (thenTy->isNumeric() && elseTy->isNumeric()) {
        resultTy = tc_.getCommonArithmeticType(thenTy, elseTy);
    } else if (thenTy->isImplicitlyConvertibleTo(*elseTy)) {
        resultTy = elseTy;
    } else if (elseTy->isImplicitlyConvertibleTo(*thenTy)) {
        resultTy = thenTy;
    } else {
        diag_.report(expr.loc, Severity::Error,
                     "incompatible branch types in ternary operator: " + thenTy->toString() + " vs " + elseTy->toString());
        resultTy = thenTy;
    }

    // Merge lifetime and provenance for memory safety
    ProvenanceSet thenProv = getExprProvenance(expr.thenExpr.get());
    ProvenanceSet elseProv = getExprProvenance(expr.elseExpr.get());
    thenProv.merge(elseProv);
    setExprProvenance(&expr, thenProv);
    if (thenProv.primaryRegionId() > 0) {
        setExprRegionProvenance(&expr, thenProv.primaryRegionId());
    }

    return resultTy;
}

const Type* TypeChecker::checkTuple(ast::TupleExpr& expr) {
    std::vector<const Type*> elemTypes;
    ProvenanceSet mergedProv;
    for (auto& el : expr.elements) {
        auto elTy = checkExpr(*el);
        elemTypes.push_back(elTy ? elTy : tc_.getI32());
        mergedProv.merge(getExprProvenance(el.get()));
    }
    setExprProvenance(&expr, mergedProv);
    if (mergedProv.primaryRegionId() > 0) {
        setExprRegionProvenance(&expr, mergedProv.primaryRegionId());
    }
    return tc_.getTuple(std::move(elemTypes));
}

const Type* TypeChecker::checkTupleIndex(ast::TupleIndexExpr& expr) {
    if (!expr.tuple) return tc_.getI32();
    auto baseTy = checkExpr(*expr.tuple);
    if (baseTy && baseTy->isReference()) baseTy = baseTy->elementType;
    if (!baseTy || !baseTy->isTuple()) {
        diag_.report(expr.loc, Severity::Error,
                     "cannot index non-tuple type '" + (baseTy ? baseTy->toString() : "unknown") + "' with tuple index");
        return tc_.getI32();
    }
    if (expr.index >= baseTy->tupleElements.size()) {
        diag_.report(expr.loc, Severity::Error,
                     "tuple index " + std::to_string(expr.index) + " is out of bounds for tuple with " +
                     std::to_string(baseTy->tupleElements.size()) + " elements");
        return tc_.getI32();
    }
    setExprProvenance(&expr, getExprProvenance(expr.tuple.get()));
    return baseTy->tupleElements[expr.index];
}

const Type* TypeChecker::checkLambda(ast::LambdaExpr& expr) {
    std::vector<const Type*> paramTypes;
    syms_.pushScope();
    pushScope();
    size_t lambdaScopeDepth = syms_.depth();

    LambdaCheckContext ctx;
    ctx.expr = &expr;
    ctx.scopeDepth = lambdaScopeDepth;
    activeLambdaStack_.push_back(std::move(ctx));

    for (auto& param : expr.params) {
        const Type* pTy = nullptr;
        if (param.type && param.type->kind != ast::TypeNodeKind::Auto) {
            pTy = resolveType(*param.type);
        } else {
            pTy = tc_.getI32();
        }
        if (param.isArray) {
            pTy = tc_.getArrayView(pTy, param.isConst);
        }
        paramTypes.push_back(pTy);

        Symbol sym;
        sym.name = param.name;
        sym.type = pTy;
        sym.isConst = param.isConst;
        sym.isParameter = true;
        sym.loc = param.loc;
        syms_.declare(param.name, sym);
        markAssigned(param.name);
    }

    const Type* retTy = nullptr;
    if (expr.returnType) {
        retTy = resolveType(*expr.returnType);
    }

    if (expr.exprBody) {
        auto bodyTy = checkExpr(*expr.exprBody);
        if (!retTy) {
            retTy = bodyTy ? bodyTy : tc_.getI32();
        } else if (bodyTy && !bodyTy->isImplicitlyConvertibleTo(*retTy)) {
            diag_.report(expr.exprBody->loc, Severity::Error,
                         "lambda return type mismatch: expected " + retTy->toString() +
                         ", got " + bodyTy->toString());
        }
    } else if (expr.blockBody) {
        const Type* savedFuncRet = currentFuncReturnType_;
        currentFuncReturnType_ = retTy;
        for (auto& s : expr.blockBody->stmts) {
            checkStmt(*s, currentFuncReturnType_);
            if (!retTy && !activeLambdaStack_.empty() && activeLambdaStack_.back().inferredReturnType) {
                retTy = activeLambdaStack_.back().inferredReturnType;
                currentFuncReturnType_ = retTy;
            }
        }
        if (!retTy && !activeLambdaStack_.empty() && activeLambdaStack_.back().inferredReturnType) {
            retTy = activeLambdaStack_.back().inferredReturnType;
        }
        currentFuncReturnType_ = savedFuncRet;
        if (!retTy) retTy = tc_.getVoid();
    } else {
        if (!retTy) retTy = tc_.getVoid();
    }

    auto finishedCtx = std::move(activeLambdaStack_.back());
    activeLambdaStack_.pop_back();

    popScope();
    syms_.popScope();

    // Propagate captures to outer lambdas in stack if any
    for (auto& outerCtx : activeLambdaStack_) {
        for (const auto& [name, cap] : finishedCtx.captures) {
            if (cap.scopeDepth < outerCtx.scopeDepth) {
                if (!outerCtx.captures.count(name)) {
                    outerCtx.captures[name] = cap;
                } else if (cap.isMutable) {
                    outerCtx.captures[name].isMutable = true;
                }
            }
        }
    }

    std::vector<LambdaCapture> capturesList;
    expr.captures.clear();
    ProvenanceSet lambdaProv;
    for (auto& [name, cap] : finishedCtx.captures) {
        expr.captures.push_back(name);
        capturesList.push_back(cap);
        lambdaProv.add(ProvenanceOrigin{ProvenanceKind::LocalStack, cap.scopeDepth, name});
    }
    lambdaCaptures_[&expr] = std::move(capturesList);
    setExprProvenance(&expr, lambdaProv);

    return tc_.getFunction(retTy, std::move(paramTypes));
}

const Type* TypeChecker::checkRange(ast::RangeExpr& expr) {
    if (expr.start) {
        auto sTy = checkExpr(*expr.start);
        if (sTy && !sTy->isInteger()) {
            diag_.report(expr.start->loc, Severity::Error,
                         "range start must be an integer, got " + sTy->toString());
        }
    }
    if (expr.end) {
        auto eTy = checkExpr(*expr.end);
        if (eTy && !eTy->isInteger()) {
            diag_.report(expr.end->loc, Severity::Error,
                         "range end must be an integer, got " + eTy->toString());
        }
    }
    return tc_.getVoid();
}

const Type* TypeChecker::checkAwait(ast::AwaitExpr& expr) {
    if (!expr.operand) {
        diag_.report(expr.loc, Severity::Error, "expected operand after 'await'");
        return tc_.getVoid();
    }
    auto opTy = checkExpr(*expr.operand);
    if (!opTy) return tc_.getVoid();
    if (!opTy->isTask()) {
        diag_.report(expr.loc, Severity::Error,
                     "cannot await non-task type '" + opTy->toString() + "'");
        return tc_.getI32();
    }
    const Type* unwrapped = opTy->elementType;
    if (!unwrapped || unwrapped->isVoid()) {
        return tc_.getVoid();
    }
    return unwrapped;
}

void TypeChecker::checkMatchStmt(ast::MatchStmt& stmt, const Type* currentFuncReturnType) {
    if (!stmt.expr) return;
    auto targetTy = checkExpr(*stmt.expr);
    if (!targetTy) return;

    if (stmt.expr->kind == ExprKind::Identifier && targetTy && targetTy->hasNonTrivialDestructor()) {
        auto& id = static_cast<IdentifierExpr&>(*stmt.expr);
        checkBorrowMove(id.name, stmt.loc);
        markMoved(id.name);
    }

    if (targetTy->isEnum()) {
        std::vector<bool> matchedVariants(targetTy->enumVariants.size(), false);
        bool hasCatchAll = false;

        for (auto& c : stmt.cases) {
            if (hasCatchAll) {
                diag_.report(c.loc, Severity::Warning, "unreachable pattern in match: pattern appears after a wildcard or catch-all arm");
            }

            pushScope();
            syms_.pushScope();

            if (c.pattern.kind == MatchPatternKind::Wildcard) {
                hasCatchAll = true;
            } else if (c.pattern.kind == MatchPatternKind::Binding) {
                hasCatchAll = true;
                if (!c.pattern.bindings.empty()) {
                    Symbol sym;
                    sym.name = c.pattern.bindings[0];
                    sym.type = targetTy;
                    sym.isConst = true;
                    sym.loc = c.pattern.loc;
                    syms_.declare(c.pattern.bindings[0], std::move(sym));
                    markAssigned(c.pattern.bindings[0]);
                }
            } else if (c.pattern.kind == MatchPatternKind::Variant) {
                int varIdx = targetTy->getVariantIndex(c.pattern.variantName);
                if (varIdx < 0) {
                    diag_.report(c.pattern.loc, Severity::Error,
                        "variant '" + c.pattern.variantName + "' is not a member of enum '" + targetTy->enumName + "'");
                } else {
                    if (matchedVariants[varIdx] && !c.guard) {
                        diag_.report(c.pattern.loc, Severity::Warning,
                            "unreachable pattern: variant '" + c.pattern.variantName + "' has already been matched");
                    }
                    if (!c.guard) {
                        matchedVariants[varIdx] = true;
                    }
                    const auto& v = targetTy->enumVariants[varIdx];
                    if (c.pattern.bindings.size() != v.payloadTypes.size()) {
                        diag_.report(c.pattern.loc, Severity::Error,
                            "variant pattern '" + c.pattern.variantName + "' binds " +
                            std::to_string(c.pattern.bindings.size()) + " variables, but variant has " +
                            std::to_string(v.payloadTypes.size()) + " payload fields");
                    } else {
                        for (size_t i = 0; i < c.pattern.bindings.size(); ++i) {
                            Symbol sym;
                            sym.name = c.pattern.bindings[i];
                            sym.type = v.payloadTypes[i];
                            sym.isConst = true;
                            sym.isConst = false;
                            sym.loc = c.pattern.loc;
                            syms_.declare(c.pattern.bindings[i], std::move(sym));
                            markAssigned(c.pattern.bindings[i]);
                        }
                    }
                }
            }

            if (c.guard) {
                auto gTy = checkExpr(*c.guard);
                if (gTy && !gTy->isBool()) {
                    diag_.report(c.guard->loc, Severity::Error,
                        "match guard condition must be of type 'bool', got " + gTy->toString());
                }
            }

            if (c.body) {
                checkStmt(*c.body, currentFuncReturnType);
            }

            syms_.popScope();
            popScope();
        }

        if (!hasCatchAll) {
            for (size_t i = 0; i < matchedVariants.size(); ++i) {
                if (!matchedVariants[i]) {
                    diag_.report(stmt.loc, Severity::Error,
                        "non-exhaustive patterns in match: missing variant '" + targetTy->enumVariants[i].name + "'");
                }
            }
        }
    } else if (targetTy->isInteger()) {
        bool hasCatchAll = false;
        for (auto& c : stmt.cases) {
            pushScope();
            syms_.pushScope();
            if (c.pattern.kind == MatchPatternKind::Wildcard) {
                hasCatchAll = true;
            } else if (c.pattern.kind == MatchPatternKind::Binding) {
                hasCatchAll = true;
                if (!c.pattern.bindings.empty()) {
                    Symbol sym;
                    sym.name = c.pattern.bindings[0];
                    sym.type = targetTy;
                    sym.isConst = true;
                    sym.loc = c.pattern.loc;
                    syms_.declare(c.pattern.bindings[0], std::move(sym));
                    markAssigned(c.pattern.bindings[0]);
                }
            } else if (c.pattern.kind == MatchPatternKind::Literal) {
                if (c.pattern.literalExpr) {
                    auto lTy = checkExpr(*c.pattern.literalExpr);
                    if (lTy && !lTy->isInteger()) {
                        diag_.report(c.pattern.loc, Severity::Error, "literal pattern type mismatch with match target");
                    }
                }
            }
            if (c.guard) {
                checkExpr(*c.guard);
            }
            if (c.body) {
                checkStmt(*c.body, currentFuncReturnType);
            }
            syms_.popScope();
            popScope();
        }
        if (!hasCatchAll) {
            diag_.report(stmt.loc, Severity::Error,
                "non-exhaustive patterns in match: integer match requires a wildcard '_' or binding arm");
        }
    }
}

// ── Statement Checking ──────────────────────────────────────────────────────

void TypeChecker::checkStmt(ast::Stmt& stmt, const Type* currentFuncReturnType) {
    if (++stmtDepth_ > MAX_STMT_DEPTH) {
        diag_.report(stmt.loc, Severity::Error,
                     "statement nesting depth exceeds maximum supported limit of " +
                     std::to_string(MAX_STMT_DEPTH));
        --stmtDepth_;
        return;
    }
    struct StmtDepthScope {
        int& d;
        ~StmtDepthScope() { --d; }
    } scope{stmtDepth_};

    currentFuncReturnType_ = currentFuncReturnType;
    switch (stmt.kind) {
        case StmtKind::Block: {
            auto& b = static_cast<BlockStmt&>(stmt);
            pushScope();
            syms_.pushScope();
            for (auto& s : b.stmts) checkStmt(*s, currentFuncReturnType);
            syms_.popScope();
            popScope();
            break;
        }
        case StmtKind::VarDecl: {
            auto& v = static_cast<VarDeclStmt&>(stmt);
            bool isAuto = false;
            bool isConstAuto = false;
            bool isAutoRef = false;
            bool isConstAutoRef = false;

            if (v.type->kind == TypeNodeKind::Auto) {
                isAuto = true;
                isConstAuto = static_cast<const AutoTypeNode*>(v.type.get())->isConst || v.isConst;
            } else if (v.type->kind == TypeNodeKind::Reference) {
                auto& rt = static_cast<const ReferenceTypeNode&>(*v.type);
                if (rt.referentType && rt.referentType->kind == TypeNodeKind::Auto) {
                    isAutoRef = true;
                    isConstAutoRef = rt.isConst || static_cast<const AutoTypeNode*>(rt.referentType.get())->isConst || v.isConst;
                }
            }

            const Type* varTy = nullptr;
            const Type* elemTy = nullptr;

            if (isAuto || isAutoRef) {
                if (!v.initializer) {
                    diag_.report(v.loc, Severity::Error,
                                 "variable declared with 'auto' must have an initializer");
                    return;
                }
                auto initTy = checkExpr(*v.initializer);
                if (!initTy || initTy->isVoid()) {
                    diag_.report(v.initializer->loc, Severity::Error,
                                 "cannot infer 'auto' type from void or invalid initializer");
                    return;
                }
                if (isAutoRef) {
                    elemTy = initTy->isReference() ? initTy->elementType : initTy;
                    varTy = tc_.getReference(elemTy, isConstAutoRef);
                    if (isConstAutoRef) v.isConst = true;
                } else {
                    elemTy = initTy->isReference() ? initTy->elementType : initTy;
                    varTy = elemTy;
                    if (isConstAuto) v.isConst = true;
                }
            } else {
                elemTy = resolveType(*v.type);
                varTy = elemTy;
            }

            if (elemTy && elemTy->isVoid()) {
                if (v.isArray) {
                    diag_.report(v.loc, Severity::Error, "cannot declare array of type 'void'");
                } else {
                    diag_.report(v.loc, Severity::Error, "variable '" + v.name + "' cannot be of type 'void'");
                }
                return;
            }

            if (v.isArray) {
                if (v.arraySize) {
                    // Fixed array T[N]
                    checkExpr(*v.arraySize);
                    auto eval = evalIntConstExpr(*v.arraySize);
                    if (!eval.isConst) {
                        diag_.report(v.arraySize->loc, Severity::Error,
                                     "variable-length arrays are not supported; array size must be a constant integer");
                        varTy = tc_.getArray(elemTy, 0, v.isConst);
                    } else if (eval.overflow) {
                        diag_.report(v.arraySize->loc, Severity::Error,
                                     "array size expression overflows 64-bit integer limit");
                        varTy = tc_.getArray(elemTy, 0, v.isConst);
                    } else if (eval.value < 0) {
                        diag_.report(v.arraySize->loc, Severity::Error,
                                     "array size cannot be negative (" + std::to_string(eval.value) + ")");
                        varTy = tc_.getArray(elemTy, 0, v.isConst);
                    } else {
                        int64_t size = eval.value;
                        int64_t elemSize = elemTy ? elemTy->size() : 4;
                        if (elemSize > 0 && size > (INT64_MAX / elemSize)) {
                            diag_.report(v.arraySize->loc, Severity::Error,
                                         "array total byte size overflows 64-bit integer limit (" +
                                         std::to_string(size) + " * " + std::to_string(elemSize) + " bytes)");
                        } else if (size * elemSize > 0x10000000000LL /* 1 TB */) {
                            diag_.report(v.arraySize->loc, Severity::Error,
                                         "array total size exceeds maximum supported allocation limit (" +
                                         std::to_string(size * elemSize) + " bytes)");
                        }
                        v.evaluatedArraySize = size;
                        varTy = tc_.getArray(elemTy, size, v.isConst);
                    }
                } else if (v.initializer) {
                    // Inferred array T[] = { ... } or T[] = arr
                    if (auto* aInit = dynamic_cast<ArrayInitExpr*>(v.initializer.get())) {
                        checkExpr(*v.initializer);
                        int64_t count = static_cast<int64_t>(aInit->elements.size());
                        v.evaluatedArraySize = count;
                        varTy = tc_.getArray(elemTy, count, v.isConst);
                    } else {
                        auto initTy = checkExpr(*v.initializer);
                        if (initTy && initTy->isArray()) {
                            varTy = tc_.getArrayView(initTy->elementType, v.isConst || initTy->isConst);
                        } else if (initTy && (initTy->isArrayView() || v.initializer->kind == ExprKind::StringLiteral)) {
                            varTy = initTy;
                        } else {
                            diag_.report(v.loc, Severity::Error,
                                         "unsized array declaration initializer must be an array initializer");
                            varTy = tc_.getArray(elemTy, 0, v.isConst);
                        }
                    }
                } else {
                    diag_.report(v.loc, Severity::Error,
                                 "unsized array declaration must have an initializer");
                    varTy = tc_.getArray(elemTy, 0, v.isConst);
                }

                if (v.initializer) {
                    if (auto* initExpr = dynamic_cast<ArrayInitExpr*>(v.initializer.get())) {
                        if (varTy->arraySize >= 0 && static_cast<int64_t>(initExpr->elements.size()) > varTy->arraySize) {
                            diag_.report(v.initializer->loc, Severity::Error,
                                         "excess elements in array initializer: expected at most " +
                                         std::to_string(varTy->arraySize) + ", got " +
                                         std::to_string(initExpr->elements.size()));
                        }
                    }
                }
            }

            bool isInitialized = false;
            const Type* initTy = nullptr;
            if (v.initializer) {
                if (varTy->hasNonTrivialDestructor() && !isRValue(v.initializer.get())) {
                    std::string typeName = varTy->isEnum() ? varTy->enumName : varTy->structName;
                    std::string srcName = (v.initializer->kind == ExprKind::Identifier) ? static_cast<IdentifierExpr&>(*v.initializer).name : "value";
                    diag_.report(v.loc, Severity::Error,
                                 "cannot copy resource-owning class '" + typeName + "'; use 'move(" + srcName + ")' to transfer ownership");
                }
                if (varTy->isStruct() && v.initializer->kind == ExprKind::CompoundInit) {
                    auto& cie = static_cast<CompoundInitExpr&>(*v.initializer);
                    if (!cie.type) {
                        setExprType(&cie, varTy);
                    }
                }
                initTy = checkExpr(*v.initializer);
                if (varTy->isEnum() && varTy->enumName.rfind("Result<", 0) == 0 && initTy && initTy->isEnum() && initTy->enumName.rfind("Result<", 0) == 0) {
                    setExprType(v.initializer.get(), varTy);
                    initTy = varTy;
                } else if (varTy->isEnum() && varTy->enumName.rfind("Option<", 0) == 0 && initTy && initTy->isEnum() && initTy->enumName.rfind("Option<", 0) == 0) {
                    setExprType(v.initializer.get(), varTy);
                    initTy = varTy;
                }
                bool canInit = initTy && initTy->isImplicitlyConvertibleTo(*varTy);
                if (!canInit && varTy->isArray() && initTy && initTy->isArrayView() && *varTy->elementType == *initTy->elementType) {
                    canInit = true;
                }
                if (!canInit && varTy->isSlab() && v.initializer->kind == ExprKind::Construct) {
                    canInit = true;
                }
                if (!canInit && varTy->isPointer() && v.initializer->kind == ExprKind::IntLiteral &&
                    static_cast<IntLiteralExpr&>(*v.initializer).value == 0) {
                    canInit = true;
                }
                if (!canInit) {
                    diag_.report(v.initializer->loc, Severity::Error,
                                 "cannot initialize " + varTy->toString() +
                                 " with " + (initTy ? initTy->toString() : "unknown"));
                }
                isInitialized = true;
            }

            // Reference checking
            if (varTy->isReference()) {
                if (!v.initializer) {
                    if (varTy->isRef()) {
                        diag_.report(v.loc, Severity::Error,
                                     "reference variable '" + v.name + "' must be initialized");
                    }
                } else if (varTy->isRef() && !varTy->isConstRef && (!initTy || !initTy->isReference())) {
                    bool isLValue = (v.initializer->kind == ExprKind::Identifier ||
                                     v.initializer->kind == ExprKind::Field ||
                                     v.initializer->kind == ExprKind::Index);
                    if (!isLValue) {
                        diag_.report(v.initializer->loc, Severity::Error,
                                     "cannot bind non-const reference '" + v.name + "' to an rvalue/temporary");
                    }
                }
                std::string tStr, rTarget;
                if (v.initializer) {
                    if (v.initializer->kind == ExprKind::Unary &&
                        static_cast<UnaryExpr&>(*v.initializer).op == UnaryOp::AddrOf) {
                        auto& un = static_cast<UnaryExpr&>(*v.initializer);
                        std::tie(tStr, rTarget) = extractBorrowTarget(un.operand.get());
                    } else if (v.initializer->kind == ExprKind::Identifier) {
                        auto srcName = static_cast<IdentifierExpr&>(*v.initializer).name;
                        for (const auto& frame : activeBorrowsStack_) {
                            for (const auto& b : frame) {
                                if (b.borrower == srcName) {
                                    tStr = b.target;
                                    rTarget = b.rootTarget;
                                    break;
                                }
                            }
                            if (!tStr.empty()) break;
                        }
                    }
                }
                if (tStr.empty() && v.initializer) {
                    ProvenanceSet initProv = getExprProvenance(v.initializer.get());
                    for (const auto& o : initProv.origins) {
                        if ((o.kind == ProvenanceKind::LocalStack || o.kind == ProvenanceKind::ParameterBorrow) &&
                            !o.detail.empty()) {
                            tStr = o.detail;
                            rTarget = o.detail;
                            break;
                        }
                    }
                }
                if (!tStr.empty()) {
                    registerBorrow(v.name, tStr, rTarget, !varTy->isConstRef, syms_.depth(), v.loc);
                }
                if (v.initializer && v.initializer->kind == ExprKind::Index) {
                    auto& idx = static_cast<IndexExpr&>(*v.initializer);
                    auto arrTy = getExprType(idx.array.get());
                    if (arrTy && arrTy->isArray() && arrTy->elementType && arrTy->elementType->isRecord()) {
                        if (arrTy->elementType->layoutDirective == 2 /* Scan / SoA */) {
                            diag_.report(v.initializer->loc, Severity::Error,
                                         "cannot take address of element in Structure-of-Arrays (@layout(scan)) storage; logical elements are not stored contiguously");
                        }
                    }
                }
            }

            // Record default construction checking
            if (varTy->isRecord() && !v.initializer) {
                if (varTy->isAbstract) {
                    diag_.report(v.loc, Severity::Error,
                                 "cannot instantiate abstract class '" + varTy->structName +
                                 "' with pure virtual methods");
                } else if (!varTy->constructors.empty()) {
                    bool hasDefaultCtor = false;
                    for (const auto& ctor : varTy->constructors) {
                        if (ctor.paramTypes.empty()) {
                            hasDefaultCtor = true;
                            if (ctor.access == 1 /* Private */) {
                                if (!currentRecord_ || (currentRecord_ != varTy && currentRecord_->structName != varTy->structName)) {
                                    diag_.report(v.loc, Severity::Error,
                                                 "cannot call private default constructor of class '" + varTy->structName + "'");
                                }
                            }
                            break;
                        }
                    }
                    if (!hasDefaultCtor) {
                        diag_.report(v.loc, Severity::Error,
                                     "class '" + varTy->structName + "' does not provide a default constructor");
                    }
                }
            }

            if (!v.initializer && varTy->isRecord() && varTy->constructors.empty() && currentRegionId() == 0) {
                if (!uninitFieldsStack_.empty()) {
                    auto& set = uninitFieldsStack_.back()[v.name];
                    for (const auto& f : varTy->structFields) {
                        set.insert(f.name);
                    }
                }
            }

            if (v.isArray && !v.initializer && varTy->arraySize > 0 && varTy->arraySize <= 256) {
                if (!uninitArrayElementsStack_.empty()) {
                    auto& set = uninitArrayElementsStack_.back()[v.name];
                    for (int64_t i = 0; i < varTy->arraySize; ++i) {
                        set.insert(i);
                    }
                }
            }

            if (v.isArray || varTy->isStruct()) {
                isInitialized = true;
            }
            if (varTy->isSlab()) {
                isInitialized = true;
            }

            if (!assignedStack_.empty()) {
                assignedStack_.back()[v.name] = isInitialized;
            }

            uint64_t curRid = currentRegionId();
            if (curRid > 0) {
                if (v.isArray || varTy->isArray() || varTy->isRecord()) {
                    if (elemTy && elemTy->isRecord() && elemTy->layoutDirective == 2 /* Scan / SoA */) {
                        diag_.report(v.loc, Severity::Error,
                                     "region allocation does not support @layout(scan) SoA arrays in v0.5.1; use contiguous Array-of-Structures or stack allocation");
                    }
                }
            }

            ProvenanceSet symProv;
            uint64_t symRid = 0;
            if (curRid > 0 && (v.isArray || varTy->isArray() || varTy->isRecord())) {
                symRid = curRid;
                symProv = ProvenanceSet(ProvenanceKind::LocalRegion, curRid);
            } else if (varTy->isReference() || varTy->isArrayView() || varTy->isRecord() || varTy->isFunction()) {
                if (v.initializer) {
                    symProv = getExprProvenance(v.initializer.get());
                    if (symProv.empty()) {
                        uint64_t initRid = getExprRegionProvenance(v.initializer.get());
                        if (initRid > 0) symProv = ProvenanceSet(ProvenanceKind::LocalRegion, initRid);
                    }
                    std::string reason;
                    if (!canAssignProvenance(symProv, curRid, /*isDestGlobal=*/false, reason)) {
                        diag_.report(v.loc, Severity::Error,
                                     "cannot bind outer reference to region-backed storage");
                    }
                    symRid = symProv.primaryRegionId();
                }
            }

            setVarDeclType(&v, varTy);

            Symbol sym{
                .name        = v.name,
                .type        = varTy,
                .isConst     = v.isConst || varTy->isConst,
                .isFunction  = false,
                .isParameter = false,
                .regionId    = symRid,
                .provenance  = symProv,
                .loc         = v.loc,
            };
            if (!syms_.declare(v.name, std::move(sym))) {
                diag_.report(v.loc, Severity::Error,
                             "redefinition of variable '" + v.name + "'");
            }

            setVarProvenance(v.name, symProv);

            if (varTy && varTy->isFunction()) {
                if (v.initializer && v.initializer->kind == ExprKind::Lambda) {
                    auto* caps = getLambdaCaptures(static_cast<const LambdaExpr*>(v.initializer.get()));
                    if (caps) {
                        for (const auto& cap : *caps) {
                            registerBorrow(v.name, cap.name, cap.name, cap.isMutable, syms_.depth(), v.loc);
                        }
                    }
                } else if (!symProv.empty()) {
                    for (const auto& o : symProv.origins) {
                        if (o.kind == ProvenanceKind::LocalStack && !o.detail.empty()) {
                            registerBorrow(v.name, o.detail, o.detail, true, syms_.depth(), v.loc);
                        }
                    }
                }
            }

            MemoryReportEntry rep;
            rep.name = v.name;
            rep.typeName = varTy ? varTy->toString() : "unknown";
            rep.size = varTy ? varTy->size() : 0;
            rep.align = varTy ? varTy->alignment() : 0;
            if (symRid > 0) {
                rep.storageClass = "Region";
                rep.storageRegion = "r" + std::to_string(symRid);
                rep.lifetime = "Region(r" + std::to_string(symRid) + ")";
                rep.dtorKind = (varTy && varTy->hasNonTrivialDestructor()) ? "Sparse Sidecar" : "None";
            } else {
                rep.storageClass = "Stack";
                rep.storageRegion = "-";
                rep.lifetime = "Lexical";
                rep.dtorKind = (varTy && varTy->hasNonTrivialDestructor()) ? "Direct RAII" : "None";
            }
            rep.hasNonTrivialDestructor = varTy ? varTy->hasNonTrivialDestructor() : false;
            rep.requiresSidecar = (symRid > 0) && rep.hasNonTrivialDestructor;
            rep.containsReferencesOrViews = varTy ? varTy->canContainBorrow() : false;
            rep.hasExternalBorrow = false;
            for (const auto& orig : symProv.origins) {
                if (orig.isBorrow() || (orig.isRegion() && static_cast<int>(orig.identity) != symRid)) {
                    rep.hasExternalBorrow = true;
                    break;
                }
            }
            // Disentangle storage provenance from reference-containing types
            rep.containsBorrows = rep.containsReferencesOrViews || rep.hasExternalBorrow;
            rep.payloadContribution = rep.size;
            memoryReport_.push_back(std::move(rep));

            if (v.initializer && v.initializer->kind == ExprKind::CompoundInit && varTy->isRecord()) {
                auto& cInit = static_cast<CompoundInitExpr&>(*v.initializer);
                for (size_t i = 0; i < cInit.fields.size(); ++i) {
                    auto& f = cInit.fields[i];
                    std::string fName = f.fieldName;
                    if (fName.empty() && i < varTy->structFields.size()) {
                        fName = varTy->structFields[i].name;
                    }
                    if (!fName.empty()) {
                        ProvenanceSet fProv = getExprProvenance(f.value.get());
                        if (!fProv.empty()) {
                            setFieldProvenance(v.name, fName, fProv);
                        }
                    }
                }
            }
            break;
        }
        case StmtKind::DestructureVarDecl: {
            auto& ds = static_cast<DestructureVarDeclStmt&>(stmt);
            if (!ds.initializer) {
                diag_.report(ds.loc, Severity::Error, "destructuring declaration requires an initializer");
                break;
            }
            auto initTy = checkExpr(*ds.initializer);
            if (initTy && initTy->isReference()) initTy = initTy->elementType;
            if (!initTy || !initTy->isTuple()) {
                diag_.report(ds.loc, Severity::Error,
                    "cannot destructure non-tuple type '" + (initTy ? initTy->toString() : "unknown") + "'");
                break;
            }
            if (ds.names.size() != initTy->tupleElements.size()) {
                diag_.report(ds.loc, Severity::Error,
                    "destructuring arity mismatch: tuple has " + std::to_string(initTy->tupleElements.size()) +
                    " elements, but " + std::to_string(ds.names.size()) + " variables were provided");
                break;
            }
            ProvenanceSet initProv = getExprProvenance(ds.initializer.get());
            for (size_t i = 0; i < ds.names.size(); ++i) {
                const auto* elemTy = initTy->tupleElements[i];
                destructureTypes_[&ds].push_back(elemTy);
                Symbol sym{
                    .name        = ds.names[i],
                    .type        = elemTy,
                    .isConst     = ds.isConst || elemTy->isConst,
                    .isFunction  = false,
                    .isParameter = false,
                    .regionId    = initProv.primaryRegionId(),
                    .provenance  = initProv,
                    .loc         = ds.loc,
                };
                if (!syms_.declare(ds.names[i], std::move(sym))) {
                    diag_.report(ds.loc, Severity::Error,
                                 "redefinition of variable '" + ds.names[i] + "'");
                }
                setVarProvenance(ds.names[i], initProv);
                markAssigned(ds.names[i]);
            }
            break;
        }
        case StmtKind::Expr: {
            auto& e = static_cast<ExprStmt&>(stmt);
            if (e.expr) checkExpr(*e.expr);
            break;
        }
        case StmtKind::Return: {
            auto& r = static_cast<ReturnStmt&>(stmt);
            if (r.value) {
                auto valTy = checkExpr(*r.value);
                if (!activeLambdaStack_.empty() && !activeLambdaStack_.back().inferredReturnType) {
                    activeLambdaStack_.back().inferredReturnType = valTy;
                }
                if (currentFuncReturnType && currentFuncReturnType->isEnum() && currentFuncReturnType->enumName.rfind("Result<", 0) == 0 &&
                    valTy && valTy->isEnum() && valTy->enumName.rfind("Result<", 0) == 0) {
                    setExprType(r.value.get(), currentFuncReturnType);
                    valTy = currentFuncReturnType;
                } else if (currentFuncReturnType && currentFuncReturnType->isEnum() && currentFuncReturnType->enumName.rfind("Option<", 0) == 0 &&
                    valTy && valTy->isEnum() && valTy->enumName.rfind("Option<", 0) == 0) {
                    setExprType(r.value.get(), currentFuncReturnType);
                    valTy = currentFuncReturnType;
                }
                if (valTy && valTy->hasNonTrivialDestructor() && (!currentFuncReturnType || !currentFuncReturnType->isReference())) {
                    if (r.value->kind == ExprKind::Identifier) {
                        auto& id = static_cast<IdentifierExpr&>(*r.value);
                        checkBorrowMove(id.name, r.loc);
                        markMoved(id.name);
                    }
                }
                if (currentFuncReturnType && currentFuncReturnType->isReference()) {
                    if (r.value->kind == ExprKind::Identifier) {
                        auto& id = static_cast<IdentifierExpr&>(*r.value);
                        auto sym = syms_.lookup(id.name);
                        if (sym && !sym->isParameter) {
                            diag_.report(r.value->loc, Severity::Error,
                                         "cannot return reference to local variable '" + id.name + "'");
                        }
                    }
                }
                ProvenanceSet valProv = getExprProvenance(r.value.get());
                if (valProv.empty()) {
                    uint64_t oldRid = getExprRegionProvenance(r.value.get());
                    if (oldRid > 0) valProv = ProvenanceSet(ProvenanceKind::LocalRegion, oldRid);
                }
                bool retIsRefOrView = (currentFuncReturnType && (currentFuncReturnType->isReference() || currentFuncReturnType->isArrayView() || currentFuncReturnType->isFunction())) ||
                                      (valTy && (valTy->isReference() || valTy->isArrayView() || valTy->isArray() || valTy->canContainBorrow() || valTy->isFunction()));
                if (retIsRefOrView) {
                    ProvenanceSet retProv = valProv;
                    if (r.value->kind == ExprKind::Identifier) {
                        auto& id = static_cast<IdentifierExpr&>(*r.value);
                        retProv.merge(getVarProvenance(id.name));
                    } else if (r.value->kind == ExprKind::Index) {
                        auto& idx = static_cast<IndexExpr&>(*r.value);
                        retProv.merge(getExprProvenance(idx.array.get()));
                    } else if (r.value->kind == ExprKind::Field) {
                        auto& fe = static_cast<FieldExpr&>(*r.value);
                        retProv.merge(getExprProvenance(fe.object.get()));
                    }
                    auto lval = resolveLValuePath(r.value.get());
                    if (lval.isValid && lval.bindingId > 0) {
                        retProv.merge(getVarProvenance(lval.bindingId));
                        auto it = bindingFieldProvenance_.find(lval.bindingId);
                        if (it != bindingFieldProvenance_.end()) {
                            for (const auto& [fp, fprov] : it->second) {
                                retProv.merge(fprov);
                            }
                        }
                    }
                    if (retProv.hasRegion()) {
                        diag_.report(r.loc, Severity::Error,
                                     "cannot return reference or view to local region-backed storage");
                    }
                    if (retProv.hasLocalStack()) {
                        std::string varName;
                        for (const auto& o : retProv.origins) {
                            if (o.isStack()) { varName = o.detail; break; }
                        }
                        if (valTy && valTy->isFunction()) {
                            diag_.report(r.loc, Severity::Error,
                                         "cannot return closure capturing local variable '" + varName + "'");
                        } else if (valTy && valTy->isRecord()) {
                            diag_.report(r.loc, Severity::Error,
                                         "cannot return struct containing pointer or reference to local stack variable '" + varName + "'");
                        } else {
                            diag_.report(r.loc, Severity::Error,
                                         "cannot return pointer or reference to local stack variable '" + varName + "'");
                        }
                    }
                    setExprProvenance(r.value.get(), retProv);
                }
                if (currentFuncReturnType && currentFuncReturnType->isVoid()) {
                    diag_.report(r.loc, Severity::Error,
                                 "void function should not return a value");
                } else if (valTy && currentFuncReturnType && !valTy->isImplicitlyConvertibleTo(*currentFuncReturnType)) {
                    diag_.report(r.loc, Severity::Error,
                                 "return type mismatch: expected " +
                                 currentFuncReturnType->toString() + ", got " +
                                 valTy->toString());
                }
            } else {
                if (currentFuncReturnType && !currentFuncReturnType->isVoid()) {
                    diag_.report(r.loc, Severity::Error,
                                 "non-void function must return a value");
                }
            }
            break;
        }
        case StmtKind::If: {
            auto& i = static_cast<IfStmt&>(stmt);
            auto condTy = checkExpr(*i.condition);
            if (condTy && !condTy->isBool() && !condTy->isInteger()) {
                diag_.report(i.condition->loc, Severity::Error,
                             "if condition must be bool or integer");
            }

            auto beforeStack = assignedStack_;
            auto beforeMoved = movedStack_;
            checkStmt(*i.thenBlock, currentFuncReturnType);
            auto thenStack = assignedStack_;
            auto thenMoved = movedStack_;

            if (i.elseBlock) {
                assignedStack_ = beforeStack;
                movedStack_ = beforeMoved;
                checkStmt(*i.elseBlock, currentFuncReturnType);
                auto elseStack = assignedStack_;
                auto elseMoved = movedStack_;

                assignedStack_ = beforeStack;
                movedStack_ = beforeMoved;
                for (size_t depth = 0; depth < assignedStack_.size(); ++depth) {
                    for (auto& [var, assigned] : assignedStack_[depth]) {
                        bool inThen = (depth < thenStack.size() && thenStack[depth].count(var) && thenStack[depth].at(var));
                        bool inElse = (depth < elseStack.size() && elseStack[depth].count(var) && elseStack[depth].at(var));
                        if (assigned) {
                            assigned = inThen && inElse;
                        } else {
                            if (inThen && inElse) {
                                assigned = true;
                            }
                        }
                    }
                }
                for (size_t depth = 0; depth < movedStack_.size(); ++depth) {
                    for (auto& [var, moved] : movedStack_[depth]) {
                        bool inThenM = (depth < thenMoved.size() && thenMoved[depth].count(var) && thenMoved[depth].at(var));
                        bool inElseM = (depth < elseMoved.size() && elseMoved[depth].count(var) && elseMoved[depth].at(var));
                        moved = inThenM || inElseM;
                        if (inThenM != inElseM) {
                            conditionallyMovedVars_.insert(var);
                        }
                    }
                }
            } else {
                assignedStack_ = beforeStack;
                movedStack_ = beforeMoved;
                for (size_t depth = 0; depth < assignedStack_.size(); ++depth) {
                    for (auto& [var, assigned] : assignedStack_[depth]) {
                        bool inThen = (depth < thenStack.size() && thenStack[depth].count(var) && thenStack[depth].at(var));
                        if (assigned && !inThen) {
                            assigned = false;
                        }
                    }
                }
                for (size_t depth = 0; depth < movedStack_.size(); ++depth) {
                    for (auto& [var, moved] : movedStack_[depth]) {
                        bool inThenM = (depth < thenMoved.size() && thenMoved[depth].count(var) && thenMoved[depth].at(var));
                        if (inThenM) {
                            moved = true;
                            conditionallyMovedVars_.insert(var);
                        }
                    }
                }
            }
            break;
        }
        case StmtKind::While: {
            auto& w = static_cast<WhileStmt&>(stmt);
            auto condTy = checkExpr(*w.condition);
            if (condTy && !condTy->isBool() && !condTy->isInteger()) {
                diag_.report(w.condition->loc, Severity::Error,
                             "while condition must be bool or integer");
            }
            auto beforeStack = assignedStack_;
            ++loopDepth_;
            checkStmt(*w.body, currentFuncReturnType);
            --loopDepth_;
            assignedStack_ = beforeStack;
            break;
        }
        case StmtKind::For: {
            auto& f = static_cast<ForStmt&>(stmt);
            syms_.pushScope();
            pushScope();
            if (f.init) checkStmt(*f.init, currentFuncReturnType);
            auto outerAssigned = assignedStack_;
            if (f.condition) {
                auto condTy = checkExpr(*f.condition);
                if (condTy && !condTy->isBool() && !condTy->isInteger()) {
                    diag_.report(f.condition->loc, Severity::Error,
                                 "for condition must be bool or integer");
                }
            }
            if (f.increment) checkExpr(*f.increment);
            ++loopDepth_;
            checkStmt(*f.body, currentFuncReturnType);
            --loopDepth_;
            popScope();
            syms_.popScope();
            for (size_t d = 0; d < assignedStack_.size(); ++d) {
                for (auto& [var, assigned] : assignedStack_[d]) {
                    if (d < outerAssigned.size() && outerAssigned[d].count(var)) {
                        assigned = outerAssigned[d].at(var);
                    }
                }
            }
            break;
        }
        case StmtKind::Break:
            if (loopDepth_ == 0) {
                diag_.report(stmt.loc, Severity::Error, "'break' statement not within a loop");
            }
            break;
        case StmtKind::Continue:
            if (loopDepth_ == 0) {
                diag_.report(stmt.loc, Severity::Error, "'continue' statement not within a loop");
            }
            break;
        case StmtKind::Switch: {
            diag_.report(stmt.loc, Severity::Error, "'switch' statements are not yet supported in this compiler release");
            break;
        }
        case StmtKind::Region: {
            auto& r = static_cast<RegionStmt&>(stmt);
            if (r.capacityExpr) {
                auto capTy = checkExpr(*r.capacityExpr);
                if (!capTy || !capTy->isInteger()) {
                    diag_.report(r.capacityExpr->loc, Severity::Error,
                                 "region capacity must be a positive integer constant");
                } else {
                    auto eval = evalIntConstExpr(*r.capacityExpr);
                    if (!eval.isConst) {
                        diag_.report(r.capacityExpr->loc, Severity::Error,
                                     "region capacity must be a compile-time integer constant expression");
                    } else if (eval.overflow) {
                        diag_.report(r.capacityExpr->loc, Severity::Error,
                                     "region capacity expression overflows 64-bit integer limit");
                    } else if (eval.value <= 0) {
                        diag_.report(r.capacityExpr->loc, Severity::Error,
                                     "region capacity must be positive (" + std::to_string(eval.value) + ")");
                    } else if (eval.value > 0x1000000000LL /* 64 GB */) {
                        diag_.report(r.capacityExpr->loc, Severity::Error,
                                     "region capacity exceeds maximum supported limit (" + std::to_string(eval.value) + " bytes)");
                    }
                }
            }

            uint64_t parentRid = currentRegionId();
            uint64_t rid = nextRegionId_++;
            regionParent_[rid] = parentRid;
            regionDepth_[rid] = activeRegionIds_.size() + 1;
            activeRegionIds_.push_back(rid);

            if (r.body) {
                checkStmt(*r.body, currentFuncReturnType);
            }

            activeRegionIds_.pop_back();
            break;
        }
        case StmtKind::Match: {
            auto& m = static_cast<MatchStmt&>(stmt);
            checkMatchStmt(m, currentFuncReturnType);
            break;
        }
        case StmtKind::ParallelFor: {
            auto& pf = static_cast<ParallelForStmt&>(stmt);
            syms_.pushScope();
            pushScope();
            size_t savedPforDepth = parallelLoopScopeDepth_;
            parallelLoopScopeDepth_ = syms_.depth();
            auto savedWrittenArrays = parallelLoopWrittenArrays_;
            parallelLoopWrittenArrays_.clear();

            std::vector<std::string> registeredReductions;
            for (const auto& r : pf.reductions) {
                auto sym = syms_.lookup(r.varName);
                if (!sym) {
                    diag_.report(r.loc, Severity::Error,
                                 "reduction variable '" + r.varName + "' not found");
                    continue;
                }
                if (sym->isConst) {
                    diag_.report(r.loc, Severity::Error,
                                 "cannot reduce into const variable '" + r.varName + "'");
                    continue;
                }
                auto symTy = sym->type;
                if (symTy && symTy->isReference()) symTy = symTy->elementType;
                if (!symTy || (!symTy->isInteger() && !symTy->isFloat())) {
                    diag_.report(r.loc, Severity::Error,
                                 "reduction variable '" + r.varName + "' must have integer or floating-point type");
                    continue;
                }
                activeReductionVars_.insert(r.varName);
                registeredReductions.push_back(r.varName);
            }

            std::string indVar;
            if (pf.init) {
                checkStmt(*pf.init, currentFuncReturnType);
                if (pf.init->kind == StmtKind::VarDecl) {
                    indVar = static_cast<VarDeclStmt&>(*pf.init).name;
                    parallelLoopInductionVars_.insert(indVar);
                }
            }
            auto outerAssigned = assignedStack_;
            if (pf.condition) {
                auto condTy = checkExpr(*pf.condition);
                if (condTy && !condTy->isBool() && !condTy->isInteger()) {
                    diag_.report(pf.condition->loc, Severity::Error,
                                 "parallel for condition must be bool or integer");
                }
            }
            if (pf.increment) checkExpr(*pf.increment);
            if (pf.grainSize) {
                auto gTy = checkExpr(*pf.grainSize);
                if (gTy && !gTy->isInteger()) {
                    diag_.report(pf.grainSize->loc, Severity::Error,
                                 "parallel grain size must be an integer");
                }
            }

            ++loopDepth_;
            if (pf.body) {
                checkStmt(*pf.body, currentFuncReturnType);
            }
            --loopDepth_;

            if (!indVar.empty()) {
                parallelLoopInductionVars_.erase(indVar);
            }
            for (const auto& v : registeredReductions) {
                activeReductionVars_.erase(v);
            }
            parallelLoopScopeDepth_ = savedPforDepth;
            parallelLoopWrittenArrays_ = savedWrittenArrays;
            popScope();
            syms_.popScope();

            for (size_t d = 0; d < assignedStack_.size(); ++d) {
                for (auto& [var, assigned] : assignedStack_[d]) {
                    if (d < outerAssigned.size() && outerAssigned[d].count(var)) {
                        assigned = outerAssigned[d].at(var);
                    }
                }
            }
            break;
        }
        case StmtKind::TaskScope: {
            auto& ts = static_cast<TaskScopeStmt&>(stmt);
            ++taskScopeDepth_;
            if (ts.body) {
                checkStmt(*ts.body, currentFuncReturnType);
            }
            --taskScopeDepth_;
            break;
        }
        case StmtKind::Spawn: {
            auto& sp = static_cast<SpawnStmt&>(stmt);
            if (taskScopeDepth_ == 0) {
                diag_.report(stmt.loc, Severity::Error,
                             "structured concurrency safety violation: 'spawn' is only permitted within an active 'task_scope' block");
            }
            if (sp.body) {
                checkStmt(*sp.body, currentFuncReturnType);
            }
            break;
        }
        case StmtKind::Select: {
            auto& sel = static_cast<SelectStmt&>(stmt);
            for (auto& sc : sel.cases) {
                if (sc.condition) {
                    if (sc.condition->kind == ExprKind::MethodCall) {
                        auto& mc = static_cast<MethodCallExpr&>(*sc.condition);
                        if (mc.methodName == "recv") mc.methodName = "try_recv";
                        else if (mc.methodName == "send") mc.methodName = "try_send";
                    }
                    auto condTy = checkExpr(*sc.condition);
                    if (condTy && !condTy->isBool() && !condTy->isInteger()) {
                        diag_.report(sc.loc, Severity::Error,
                                     "select case condition must evaluate to a boolean");
                    }
                }
                pushScope();
                syms_.pushScope();
                for (auto& s : sc.body) checkStmt(*s, currentFuncReturnType);
                syms_.popScope();
                popScope();
            }
            if (sel.hasDefault) {
                pushScope();
                syms_.pushScope();
                for (auto& s : sel.defaultBody) checkStmt(*s, currentFuncReturnType);
                syms_.popScope();
                popScope();
            }
            break;
        }
    }
}

void TypeChecker::printMemoryReport(std::ostream& os) const {
    os << "=== TAMARIN Static Memory Planning Report ===\n";
    os << std::left << std::setw(18) << "Name"
       << std::left << std::setw(20) << "Type"
       << std::right << std::setw(7) << "Size"
       << std::right << std::setw(7) << "Align"
       << "  " << std::left << std::setw(9) << "Storage"
       << std::left << std::setw(8) << "Region"
       << std::left << std::setw(14) << "Lifetime"
       << std::left << std::setw(16) << "Destructor"
       << std::left << std::setw(9) << "Sidecar"
       << std::left << std::setw(10) << "Refs/Views"
       << std::left << std::setw(10) << "ExtBorrow"
       << std::right << std::setw(9) << "Payload" << "\n";
    os << std::string(135, '-') << "\n";

    int64_t totalRegionPayload = 0;
    int64_t totalStackPayload = 0;
    int64_t totalGlobalPayload = 0;

    for (const auto& e : memoryReport_) {
        std::string dispType = e.typeName.size() > 18 ? (e.typeName.substr(0, 15) + "...") : e.typeName;
        os << std::left << std::setw(18) << e.name
           << std::left << std::setw(20) << dispType
           << std::right << std::setw(7) << e.size
           << std::right << std::setw(7) << e.align
           << "  " << std::left << std::setw(9) << e.storageClass
           << std::left << std::setw(8) << e.storageRegion
           << std::left << std::setw(14) << e.lifetime
           << std::left << std::setw(16) << e.dtorKind
           << std::left << std::setw(9) << (e.requiresSidecar ? "Yes" : "No")
           << std::left << std::setw(10) << (e.containsReferencesOrViews ? "Yes" : "No")
           << std::left << std::setw(10) << (e.hasExternalBorrow ? "Yes" : "No")
           << std::right << std::setw(7) << e.payloadContribution << " B\n";

        if (e.storageClass == "Region") totalRegionPayload += e.payloadContribution;
        else if (e.storageClass == "Stack") totalStackPayload += e.payloadContribution;
        else if (e.storageClass == "Global") totalGlobalPayload += e.payloadContribution;
    }
    os << std::string(135, '-') << "\n";
    os << "Total Static Allocations:  " << memoryReport_.size() << "\n";
    os << "Total Region Payload:      " << totalRegionPayload << " bytes\n";
    os << "Total Stack Payload:       " << totalStackPayload << " bytes\n";
    os << "Total Global Payload:      " << totalGlobalPayload << " bytes\n";
    os << "=============================================\n";
}

} // namespace tamarin::sema

