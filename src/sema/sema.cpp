// ============================================================================
// sema.cpp - Semantic analyzer implementation
// ============================================================================

#include "tamarin/sema/sema.h"
#include <iostream>
#include "tamarin/sema/monomorphizer.h"
#include "tamarin/driver/module_graph.h"
#include <algorithm>

namespace tamarin::sema {

using namespace ast;

static bool returnsOnAllPaths(const ast::Stmt& stmt) {
    switch (stmt.kind) {
        case ast::StmtKind::Return:
            return true;
        case ast::StmtKind::Block: {
            auto& b = static_cast<const ast::BlockStmt&>(stmt);
            for (auto& s : b.stmts) {
                if (returnsOnAllPaths(*s)) {
                    return true;
                }
            }
            return false;
        }
        case ast::StmtKind::If: {
            auto& ifStmt = static_cast<const ast::IfStmt&>(stmt);
            if (!ifStmt.elseBlock) {
                return false;
            }
            return returnsOnAllPaths(*ifStmt.thenBlock) && returnsOnAllPaths(*ifStmt.elseBlock);
        }
        case ast::StmtKind::Region: {
            auto& r = static_cast<const ast::RegionStmt&>(stmt);
            return r.body ? returnsOnAllPaths(*r.body) : false;
        }
        case ast::StmtKind::Match: {
            auto& m = static_cast<const ast::MatchStmt&>(stmt);
            if (m.cases.empty()) return false;
            for (const auto& c : m.cases) {
                if (!c.body || !returnsOnAllPaths(*c.body)) return false;
            }
            return true;
        }
        case ast::StmtKind::Select: {
            auto& sel = static_cast<const ast::SelectStmt&>(stmt);
            if (!sel.hasDefault) return false;
            for (const auto& c : sel.cases) {
                bool caseReturns = false;
                for (const auto& s : c.body) {
                    if (returnsOnAllPaths(*s)) { caseReturns = true; break; }
                }
                if (!caseReturns) return false;
            }
            bool defReturns = false;
            for (const auto& s : sel.defaultBody) {
                if (returnsOnAllPaths(*s)) { defReturns = true; break; }
            }
            return defReturns;
        }
        default:
            return false;
    }
}

static void collectReturnProvenances(const ast::Stmt& stmt, const TypeChecker& checker, ProvenanceSet& outProv) {
    if (stmt.kind == StmtKind::Return) {
        auto& r = static_cast<const ReturnStmt&>(stmt);
        if (r.value) {
            outProv.merge(checker.getExprProvenance(r.value.get()));
            if (r.value->kind == ExprKind::Identifier) {
                auto& id = static_cast<const IdentifierExpr&>(*r.value);
                outProv.merge(checker.getVarProvenance(id.name));
            }
            auto lval = checker.resolveLValuePath(r.value.get());
            if (lval.isValid && lval.bindingId > 0) {
                outProv.merge(checker.getVarProvenance(lval.bindingId));
            }
        }
    } else if (stmt.kind == StmtKind::Block) {
        auto& b = static_cast<const BlockStmt&>(stmt);
        for (const auto& s : b.stmts) collectReturnProvenances(*s, checker, outProv);
    } else if (stmt.kind == StmtKind::If) {
        auto& i = static_cast<const IfStmt&>(stmt);
        if (i.thenBlock) collectReturnProvenances(*i.thenBlock, checker, outProv);
        if (i.elseBlock) collectReturnProvenances(*i.elseBlock, checker, outProv);
    } else if (stmt.kind == StmtKind::While) {
        auto& w = static_cast<const WhileStmt&>(stmt);
        if (w.body) collectReturnProvenances(*w.body, checker, outProv);
    } else if (stmt.kind == StmtKind::For) {
        auto& f = static_cast<const ForStmt&>(stmt);
        if (f.body) collectReturnProvenances(*f.body, checker, outProv);
    } else if (stmt.kind == StmtKind::Region) {
        auto& reg = static_cast<const RegionStmt&>(stmt);
        if (reg.body) collectReturnProvenances(*reg.body, checker, outProv);
    } else if (stmt.kind == StmtKind::ParallelFor) {
        auto& pf = static_cast<const ParallelForStmt&>(stmt);
        if (pf.body) collectReturnProvenances(*pf.body, checker, outProv);
    } else if (stmt.kind == StmtKind::Select) {
        auto& sel = static_cast<const SelectStmt&>(stmt);
        for (const auto& c : sel.cases) {
            for (const auto& s : c.body) collectReturnProvenances(*s, checker, outProv);
        }
        for (const auto& s : sel.defaultBody) collectReturnProvenances(*s, checker, outProv);
    }
}

Sema::Sema(DiagnosticEngine& diag)
    : diag_(diag), checker_(tc_, syms_, diag) {}

void Sema::pruneInactiveDeclarations(ast::Program& prog, const target::TargetTriple& triple) {
    prog.imports.erase(
        std::remove_if(prog.imports.begin(), prog.imports.end(),
                       [&](const ast::ImportDecl& imp) {
                           return imp.cfg && !imp.cfg->evaluate(triple);
                       }),
        prog.imports.end());

    prog.decls.erase(
        std::remove_if(prog.decls.begin(), prog.decls.end(),
                       [&](const ast::DeclPtr& d) {
                           return d && d->cfg && !d->cfg->evaluate(triple);
                       }),
        prog.decls.end());
}

bool Sema::analyze(ast::Program& prog) {
    pruneInactiveDeclarations(prog, targetTriple_);
    auto mod = std::make_shared<driver::Module>();
    mod->id.logicalName = "main";
    mod->id.canonicalPath = "main.tmn";
    mod->ast = std::unique_ptr<ast::Program>(&prog);
    std::vector<std::shared_ptr<driver::Module>> mods = { mod };
    bool res = analyzeModules(mods);
    mod->ast.release(); // release non-owning pointer
    return res;
}

bool Sema::analyzeModules(const std::vector<std::shared_ptr<driver::Module>>& modules) {
    if (modules.empty()) return true;

    for (const auto& mod : modules) {
        if (mod && mod->ast) {
            pruneInactiveDeclarations(*mod->ast, targetTriple_);
        }
    }

    auto entryMod = modules.back();

    // 1. Setup ModuleScopes
    for (const auto& mod : modules) {
        auto scope = std::make_unique<ModuleScope>();
        scope->logicalName = mod->id.logicalName;
        scope->isMainModule = (mod->id.logicalName == "main");
        if (scope->isMainModule) {
            scope->mangledPrefix = "";
        } else {
            std::string pfx = mod->id.logicalName;
            std::replace(pfx.begin(), pfx.end(), '.', '_');
            scope->mangledPrefix = pfx + "_";
        }
        checker_.registerModuleScope(mod->id.logicalName, scope.get());
        moduleScopes_[mod->id.logicalName] = std::move(scope);
    }

    // 2. Link aliases between ModuleScopes
    for (const auto& mod : modules) {
        auto* modScope = moduleScopes_[mod->id.logicalName].get();
        for (const auto& [alias, targetLogical] : mod->aliasToLogicalName) {
            auto it = moduleScopes_.find(targetLogical);
            if (it != moduleScopes_.end()) {
                modScope->aliasToModule[alias] = it->second.get();
                modScope->aliasToLogicalName[alias] = targetLogical;
            }
        }
    }

    // Pass 0: Register all traits and process impl blocks across all modules
    for (const auto& mod : modules) {
        if (!mod || !mod->ast) continue;
        for (auto& decl : mod->ast->decls) {
            if (decl->kind == DeclKind::Trait) {
                auto& td = static_cast<TraitDecl&>(*decl);
                checker_.registerTrait(td.name, &td);
            }
        }
    }

    for (const auto& mod : modules) {
        if (!mod || !mod->ast) continue;
        for (auto& decl : mod->ast->decls) {
            if (decl->kind == DeclKind::Impl) {
                auto& id = static_cast<ImplDecl&>(*decl);
                auto tr = checker_.findTrait(id.traitName);
                if (!tr) {
                    diag_.report(id.loc, Severity::Error, "unknown trait '" + id.traitName + "' in impl block");
                    continue;
                }
                std::string targetName;
                if (id.targetType->kind == TypeNodeKind::Named) {
                    targetName = static_cast<const NamedTypeNode&>(*id.targetType).name;
                }
                if (targetName.empty()) {
                    diag_.report(id.loc, Severity::Error, "impl target must be a named record type");
                    continue;
                }
                checker_.registerTraitImpl(targetName, id.traitName);

                // Verify methods of trait
                for (const auto& tm : tr->methods) {
                    bool found = false;
                    for (const auto& im : id.methods) {
                        if (im.name == tm.name) {
                            found = true;
                            break;
                        }
                    }
                    if (!found && !tm.defaultBody) {
                        diag_.report(id.loc, Severity::Error,
                            "type '" + targetName + "' does not implement required method '" +
                            tm.name + "' of trait '" + id.traitName + "'");
                    }
                }

                // Attach methods from impl to target struct declaration across all modules
                for (const auto& targetMod : modules) {
                    if (!targetMod || !targetMod->ast) continue;
                    for (auto& d : targetMod->ast->decls) {
                        if (d->kind == DeclKind::Struct) {
                            auto& sd = static_cast<StructDecl&>(*d);
                            if (sd.name == targetName) {
                                for (const auto& im : id.methods) {
                                    bool alreadyHas = false;
                                    for (const auto& sm : sd.methods) {
                                        if (sm.name == im.name) { alreadyHas = true; break; }
                                    }
                                    if (!alreadyHas) {
                                        auto clonedRet = Monomorphizer::substituteType(im.returnType.get(), {});
                                        std::vector<ParamDecl> clonedParams;
                                        for (const auto& p : im.params) {
                                            auto pTy = Monomorphizer::substituteType(p.type.get(), {});
                                            auto pSz = Monomorphizer::substituteExpr(p.arraySize.get(), {});
                                            auto pDef = p.defaultValue ? Monomorphizer::substituteExpr(p.defaultValue.get(), {}) : nullptr;
                                            clonedParams.push_back(ParamDecl{
                                                std::move(pTy), p.name, p.isConst, p.isArray, std::move(pSz), p.loc, std::move(pDef)
                                            });
                                        }
                                        std::unique_ptr<BlockStmt> clonedBody;
                                        if (im.body) {
                                            clonedBody = std::unique_ptr<BlockStmt>(static_cast<BlockStmt*>(Monomorphizer::substituteStmt(im.body.get(), {}).release()));
                                        }
                                        sd.methods.push_back(MethodDecl{
                                            std::move(clonedRet), im.name, std::move(clonedParams), std::move(clonedBody),
                                            im.isConst, im.isVirtual, im.isOverride, im.isPureVirtual, im.isOperator, im.access, im.loc
                                        });
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // 3. Pass 1: Forward-declare all struct, class, and interface types across all modules
    for (const auto& mod : modules) {
        auto* modScope = moduleScopes_[mod->id.logicalName].get();
        checker_.setCurrentModuleScope(modScope);
        for (auto& decl : mod->ast->decls) {
            if (decl->kind == DeclKind::Struct) {
                auto& sd = static_cast<StructDecl&>(*decl);
                std::string origName = sd.name;
                if (!sd.typeParams.empty()) {
                    checker_.registerGenericStruct(origName, &sd);
                    modScope->genericStructs[origName] = &sd;
                    if (sd.isPublic) {
                        modScope->exportedGenericStructs[origName] = &sd;
                    }
                    continue; // Skip forward-declaration of unmonomorphized generic struct
                }
                std::string canonicalName = modScope->isMainModule ? origName : (modScope->mangledPrefix + origName);
                sd.name = canonicalName; // Canonical name for AST and code generator

                auto recTy = tc_.getClass(canonicalName, {}, static_cast<int>(sd.layout),
                                          sd.baseClasses.empty() ? "" : sd.baseClasses[0].name,
                                          sd.isClass, sd.isInterface);
                modScope->types[origName] = recTy;
                if (sd.isPublic) {
                    modScope->exportedTypes[origName] = recTy;
                }
            } else if (decl->kind == DeclKind::Enum) {
                auto& ed = static_cast<EnumDecl&>(*decl);
                std::string origName = ed.name;
                if (!ed.typeParams.empty()) {
                    checker_.registerGenericEnum(origName, &ed);
                    modScope->genericEnums[origName] = &ed;
                    if (ed.isPublic) {
                        modScope->exportedGenericEnums[origName] = &ed;
                    }
                    continue; // Skip forward-declaration of unmonomorphized generic enum
                }
                std::string canonicalName = modScope->isMainModule ? origName : (modScope->mangledPrefix + origName);
                ed.name = canonicalName;

                std::vector<EnumVariantType> variants;
                for (int32_t i = 0; i < static_cast<int32_t>(ed.variants.size()); ++i) {
                    const auto& v = ed.variants[i];
                    std::vector<const Type*> payloadTys;
                    for (const auto& pTyNode : v.payloadTypes) {
                        payloadTys.push_back(checker_.resolveType(*pTyNode));
                    }
                    variants.push_back(EnumVariantType{v.name, i, std::move(payloadTys)});
                }
                auto enumTy = tc_.getEnum(canonicalName, std::move(variants));
                modScope->types[origName] = enumTy;
                if (ed.isPublic) {
                    modScope->exportedTypes[origName] = enumTy;
                }
            }
        }
    }

    // 4. Pass 2: Base class hierarchy & inheritance cycle detection
    for (const auto& mod : modules) {
        auto* modScope = moduleScopes_[mod->id.logicalName].get();
        checker_.setCurrentModuleScope(modScope);
        for (auto& decl : mod->ast->decls) {
            if (decl->kind == DeclKind::Struct) {
                auto& sd = static_cast<StructDecl&>(*decl);
                if (!sd.typeParams.empty()) continue; // Skip unmonomorphized template
                if (!sd.baseClasses.empty()) {
                    const auto& baseName = sd.baseClasses[0].name;
                    auto baseTy = checker_.resolveNamedType(baseName, sd.baseClasses[0].loc);
                    if (!baseTy || !baseTy->isRecord()) {
                        diag_.report(sd.baseClasses[0].loc, Severity::Error,
                                     "base class '" + baseName + "' not found");
                    } else {
                        // Check for inheritance cycles
                        bool hasCycle = false;
                        const Type* cur = baseTy;
                        while (cur) {
                            if (cur->structName == sd.name) {
                                diag_.report(sd.baseClasses[0].loc, Severity::Error,
                                             "inheritance cycle detected involving '" + sd.name + "'");
                                hasCycle = true;
                                break;
                            }
                            cur = cur->baseClass;
                        }
                        if (!hasCycle) {
                            auto thisTy = const_cast<Type*>(tc_.findRecord(sd.name));
                            if (thisTy) thisTy->baseClass = baseTy;
                        }
                    }
                }
            }
        }
    }

    // 5. Pass 3: Field types, ABI layouts, methods, constructors, destructors
    for (const auto& mod : modules) {
        auto* modScope = moduleScopes_[mod->id.logicalName].get();
        checker_.setCurrentModuleScope(modScope);
        for (auto& decl : mod->ast->decls) {
            if (decl->kind == DeclKind::Struct) {
                auto& sd = static_cast<StructDecl&>(*decl);
                checker_.checkStructCAbi(sd);
                if (!sd.typeParams.empty()) continue; // Skip unmonomorphized template
                auto thisTy = const_cast<Type*>(tc_.findRecord(sd.name));
                if (!thisTy) continue;
                std::vector<StructFieldType> fieldTypes;

                // Prepend base class fields (single inheritance layout)
                if (thisTy->baseClass) {
                    for (const auto& bf : thisTy->baseClass->structFields) {
                        fieldTypes.push_back(bf);
                    }
                }

                for (auto& f : sd.fields) {
                    auto fTy = checker_.resolveType(*f.type);
                    if (fTy && fTy->isVoid()) {
                        diag_.report(f.loc, Severity::Error,
                                     "field '" + f.name + "' cannot be of type 'void'");
                    }
                    if (fTy->isRecord() && fTy->structName == sd.name) {
                        diag_.report(f.loc, Severity::Error,
                                     "recursive struct definition: struct '" + sd.name +
                                     "' cannot contain itself by value");
                    }
                    fieldTypes.push_back(StructFieldType{
                        .name = f.name,
                        .type = fTy,
                        .isConst = f.isConst,
                        .access = static_cast<int>(f.access),
                    });
                }

                // Determine polymorphism
                bool poly = (thisTy && thisTy->baseClass && thisTy->baseClass->isPolymorphic);
                if (sd.destructor && sd.destructor->isVirtual) poly = true;
                for (const auto& m : sd.methods) {
                    if (m.isVirtual || m.isOverride) poly = true;
                }
                if (thisTy) thisTy->isPolymorphic = poly;

                if (poly && (!thisTy->baseClass || !thisTy->baseClass->isPolymorphic)) {
                    StructFieldType vptr;
                    vptr.name = "_vptr";
                    vptr.type = tc_.getReference(tc_.getI8());
                    vptr.offset = 0;
                    vptr.size = 8;
                    vptr.alignment = 8;
                    vptr.isConst = false;
                    vptr.isCold = false;
                    vptr.access = 0;
                    fieldTypes.insert(fieldTypes.begin(), vptr);
                }

                tc_.getClass(sd.name, std::move(fieldTypes), static_cast<int>(sd.layout),
                             sd.baseClasses.empty() ? "" : sd.baseClasses[0].name,
                             sd.isClass, sd.isInterface);

                int nextVtableSlot = 0;
                if (thisTy->baseClass) {
                    thisTy->vtable = thisTy->baseClass->vtable;
                    nextVtableSlot = static_cast<int>(thisTy->vtable.size());
                }

                std::unordered_map<std::string, bool> pureVirtuals;
                if (thisTy->baseClass) {
                    for (const auto& bm : thisTy->baseClass->methods) {
                        if (bm.isPureVirtual) {
                            pureVirtuals[bm.name] = true;
                        }
                    }
                }

                for (auto& m : sd.methods) {
                    auto retTy = checker_.resolveType(*m.returnType);
                    if (retTy && retTy->hasResourceOwningClass()) {
                        diag_.report(m.loc, Severity::Error,
                                     "cannot return resource-owning class '" +
                                     retTy->structName +
                                     "' by value; return/move semantics are not supported in this release");
                    }
                    std::vector<const Type*> paramTypes;
                    std::vector<std::string> paramNames;
                    std::vector<const ast::Expr*> defaultArgs;
                    bool seenDefault = false;
                    for (auto& p : m.params) {
                        auto pTy = checker_.resolveType(*p.type);
                        if (p.isArray) pTy = tc_.getArrayView(pTy, p.isConst);
                        if (pTy && pTy->hasResourceOwningClass()) {
                            diag_.report(p.loc, Severity::Error,
                                         "cannot pass resource-owning class '" +
                                         pTy->structName + "' by value; pass by reference instead");
                        }
                        if (p.defaultValue) {
                            seenDefault = true;
                            defaultArgs.push_back(p.defaultValue.get());
                        } else {
                            if (seenDefault) {
                                diag_.report(p.loc, Severity::Error,
                                    "parameter without default argument cannot follow parameter with default argument");
                            }
                            defaultArgs.push_back(nullptr);
                        }
                        paramTypes.push_back(pTy);
                        paramNames.push_back(p.name);
                    }

                    int vSlot = -1;
                    bool isVirt = m.isVirtual;

                    const MethodType* baseMethod = thisTy->baseClass ? thisTy->baseClass->findMethod(m.name) : nullptr;
                    if (m.isOverride) {
                        if (!baseMethod || !baseMethod->isVirtual) {
                            diag_.report(m.loc, Severity::Error,
                                         "method '" + m.name + "' marked 'override' does not override any virtual base class method");
                        } else {
                            isVirt = true;
                            vSlot = baseMethod->vtableSlot;
                            pureVirtuals.erase(m.name);
                        }
                    } else if (baseMethod && baseMethod->isVirtual) {
                        isVirt = true;
                        vSlot = baseMethod->vtableSlot;
                        pureVirtuals.erase(m.name);
                    } else if (isVirt) {
                        vSlot = nextVtableSlot++;
                    }

                    if (m.isPureVirtual) {
                        pureVirtuals[m.name] = true;
                        thisTy->isAbstract = true;
                    }

                    if (isVirt && vSlot >= 0) {
                        if (vSlot >= static_cast<int>(thisTy->vtable.size())) {
                            thisTy->vtable.resize(vSlot + 1);
                        }
                        thisTy->vtable[vSlot] = m.isPureVirtual ? "" : mangleMethodName(sd.name, m.name, m.params.size());
                    }

                    thisTy->methods.push_back(MethodType{
                        .name = m.name,
                        .definingClass = sd.name,
                        .returnType = retTy,
                        .paramTypes = std::move(paramTypes),
                        .paramNames = std::move(paramNames),
                        .defaultArgs = std::move(defaultArgs),
                        .isConst = m.isConst,
                        .isVirtual = isVirt,
                        .isOverride = m.isOverride,
                        .isPureVirtual = m.isPureVirtual,
                        .isAsync = m.isAsync,
                        .vtableSlot = vSlot,
                        .access = static_cast<int>(m.access),
                    });
                }

                if (!pureVirtuals.empty()) {
                    thisTy->isAbstract = true;
                }

                for (auto& c : sd.constructors) {
                    std::vector<const Type*> cParamTypes;
                    std::vector<std::string> cParamNames;
                    for (auto& p : c.params) {
                        auto pTy = checker_.resolveType(*p.type);
                        if (p.isArray) pTy = tc_.getArrayView(pTy);
                        if (pTy && pTy->hasResourceOwningClass()) {
                            diag_.report(p.loc, Severity::Error,
                                         "cannot pass resource-owning class '" +
                                         pTy->structName + "' by value; pass by reference instead");
                        }
                        cParamTypes.push_back(pTy);
                        cParamNames.push_back(p.name);
                    }
                    thisTy->constructors.push_back(ConstructorType{
                        .paramTypes = std::move(cParamTypes),
                        .paramNames = std::move(cParamNames),
                        .access = static_cast<int>(c.access),
                    });
                }

                if (sd.destructor) {
                    thisTy->destructor = std::make_unique<DestructorType>(DestructorType{
                        .isVirtual = sd.destructor->isVirtual,
                        .access = static_cast<int>(sd.destructor->access),
                    });
                }
            }
        }
    }

    // 6. Pass 4: Functions and constants
    for (const auto& mod : modules) {
        auto* modScope = moduleScopes_[mod->id.logicalName].get();
        checker_.setCurrentModuleScope(modScope);
        for (auto& decl : mod->ast->decls) {
            if (decl->kind == DeclKind::Function) {
                auto& fn = static_cast<FunctionDecl&>(*decl);
                checker_.checkFunctionCAbi(fn);
                std::string origName = fn.name;
                if (!fn.typeParams.empty()) {
                    checker_.registerGenericFunction(origName, &fn);
                    modScope->genericFunctions[origName] = &fn;
                    if (fn.isPublic) {
                        modScope->exportedGenericFunctions[origName] = &fn;
                    }
                    continue; // Skip forward-declaration of unmonomorphized generic function template
                }
                auto retTy = checker_.resolveType(*fn.returnType);
                if (retTy && retTy->hasResourceOwningClass()) {
                    diag_.report(fn.loc, Severity::Error,
                                 "cannot return resource-owning class '" +
                                 retTy->structName +
                                 "' by value; return/move semantics are not supported in this release");
                }

                std::vector<const Type*> paramTypes;
                std::vector<const ast::Expr*> defaultArgs;
                bool seenDefault = false;
                for (auto& p : fn.params) {
                    auto elemTy = checker_.resolveType(*p.type);
                    if (elemTy && elemTy->isVoid()) {
                        diag_.report(p.loc, Severity::Error,
                                     "parameter '" + p.name + "' cannot be of type 'void'");
                    }
                    if (elemTy && elemTy->hasResourceOwningClass()) {
                        diag_.report(p.loc, Severity::Error,
                                     "cannot pass resource-owning class '" +
                                     elemTy->structName + "' by value; pass by reference instead");
                    }
                    if (p.defaultValue) {
                        seenDefault = true;
                        defaultArgs.push_back(p.defaultValue.get());
                    } else {
                        if (seenDefault) {
                            diag_.report(p.loc, Severity::Error,
                                "parameter without default argument cannot follow parameter with default argument");
                        }
                        defaultArgs.push_back(nullptr);
                    }
                    if (p.isArray) {
                        paramTypes.push_back(tc_.getArrayView(elemTy, p.isConst));
                    } else {
                        paramTypes.push_back(elemTy);
                    }
                }

                auto fnTy = tc_.getFunction(retTy, std::move(paramTypes), std::move(defaultArgs));
                origName = fn.name;
                std::string mangledName = (modScope->isMainModule || fn.name == "main" || fn.isExtern)
                    ? origName
                    : (modScope->mangledPrefix + origName);
                fn.name = mangledName;

                Symbol sym{
                    .name = origName,
                    .mangledName = mangledName,
                    .type = fnTy,
                    .isConst = true,
                    .isFunction = true,
                    .isParameter = false,
                    .isPublic = fn.isPublic,
                    .isAsync = fn.isAsync,
                    .loc = fn.loc,
                };
                modScope->symbols[origName] = sym;
                if (fn.isPublic || fn.isExtern) {
                    modScope->exportedSymbols[origName] = sym;
                }
                FunctionBorrowSummary summary;
                summary.functionName = mangledName;
                summary.hasBody = (fn.body != nullptr);
                summary.isExternal = (fn.body == nullptr);
                summary.isBuiltin = false;
                summary.parameterEffects.resize(fn.params.size(), summary.isExternal ? BorrowEffect::Unknown : BorrowEffect::NoEscape);
                summary.receiverEffect = summary.isExternal ? BorrowEffect::Unknown : BorrowEffect::NoEscape;
                checker_.registerFunctionSummary(mangledName, summary);
                checker_.registerFunctionSummary(origName, summary);

                checker_.registerGlobalFunction(mangledName, fnTy);
                checker_.registerGlobalFunction(origName, fnTy);
            } else if (decl->kind == DeclKind::Const) {
                auto& cd = static_cast<ConstDecl&>(*decl);
                auto ty = checker_.resolveType(*cd.type);
                std::string origName = cd.name;
                std::string mangledName = modScope->isMainModule
                    ? origName
                    : (modScope->mangledPrefix + origName);
                cd.name = mangledName;
                ProvenanceSet cProv(ProvenanceKind::StaticStorage, 0, origName);
                Symbol sym{
                    .name = origName,
                    .mangledName = mangledName,
                    .type = ty,
                    .isConst = true,
                    .isFunction = false,
                    .isParameter = false,
                    .isPublic = cd.isPublic,
                    .regionId = 0,
                    .provenance = cProv,
                    .loc = cd.loc,
                };
                modScope->symbols[origName] = sym;
                modScope->symbols[mangledName] = sym;
                if (cd.isPublic) {
                    modScope->exportedSymbols[origName] = sym;
                }
                checker_.setVarProvenance(origName, cProv);
                checker_.setVarProvenance(mangledName, cProv);
            } else if (decl->kind == DeclKind::Var) {
                auto& gv = static_cast<GlobalVarDecl&>(*decl);
                auto elemTy = checker_.resolveType(*gv.type);
                const Type* varTy = elemTy;
                if (gv.isArray) {
                    if (gv.arraySize) {
                        auto eval = checker_.evalIntConstExpr(*gv.arraySize);
                        if (eval.isConst && !eval.overflow && eval.value >= 0) {
                            gv.evaluatedArraySize = eval.value;
                            varTy = tc_.getArray(elemTy, eval.value);
                        } else {
                            varTy = tc_.getArray(elemTy, 0);
                        }
                    } else {
                        varTy = tc_.getArrayView(elemTy, gv.isConst);
                    }
                }
                std::string origName = gv.name;
                std::string mangledName = modScope->isMainModule ? origName : (modScope->mangledPrefix + origName);
                gv.name = mangledName;

                ProvenanceSet gProv(ProvenanceKind::StaticStorage, 0, origName);
                Symbol sym{
                    .name = origName,
                    .mangledName = mangledName,
                    .type = varTy,
                    .isConst = gv.isConst,
                    .isFunction = false,
                    .isParameter = false,
                    .isPublic = gv.isPublic,
                    .regionId = 0,
                    .provenance = gProv,
                    .loc = gv.loc,
                };
                modScope->symbols[origName] = sym;
                if (gv.isPublic) {
                    modScope->exportedSymbols[origName] = sym;
                }
                checker_.setVarProvenance(origName, gProv);
                checker_.setVarProvenance(mangledName, gProv);

                MemoryReportEntry rep;
                rep.name = origName;
                rep.typeName = varTy ? varTy->toString() : "unknown";
                rep.size = varTy ? varTy->size() : 0;
                rep.align = varTy ? varTy->alignment() : 0;
                rep.storageClass = "Global";
                rep.storageRegion = "-";
                rep.lifetime = "Static";
                rep.dtorKind = (varTy && varTy->hasNonTrivialDestructor()) ? "Direct RAII" : "None";
                rep.hasNonTrivialDestructor = varTy ? varTy->hasNonTrivialDestructor() : false;
                rep.requiresSidecar = false;
                rep.containsReferencesOrViews = varTy ? varTy->canContainBorrow() : false;
                rep.hasExternalBorrow = false;
                rep.containsBorrows = rep.containsReferencesOrViews;
                rep.payloadContribution = rep.size;
                checker_.addMemoryReportEntry(std::move(rep));
            }
        }
    }

    if (entryMod && entryMod->ast) {
        checker_.flushMonomorphizedDecls(*entryMod->ast);
    }

    // 7. Check function bodies for each module
    for (const auto& mod : modules) {
        auto* modScope = moduleScopes_[mod->id.logicalName].get();
        checker_.setCurrentModuleScope(modScope);

        // Reset and populate global symbol scope with mod's symbols
        while (syms_.depth() > 0) syms_.popScope();
        syms_.pushScope();
        for (const auto& [name, sym] : modScope->symbols) {
            syms_.declare(name, sym);
            if (!sym.mangledName.empty() && sym.mangledName != name) {
                syms_.declare(sym.mangledName, sym);
            }
        }

        checkFunctionBodies(*mod->ast);
    }

    if (entryMod && entryMod->ast) {
        checker_.flushMonomorphizedDecls(*entryMod->ast);
    }

    // 8. Validate main in the entry module
    validateMain(*entryMod->ast);

    return !diag_.hasErrors();
}

void Sema::registerTopDecls(ast::Program& prog) {
    // Pass 1: Forward-declare all struct, class, and interface types in TypeContext
    for (auto& decl : prog.decls) {
        if (decl->kind == DeclKind::Struct) {
            auto& sd = static_cast<StructDecl&>(*decl);
            tc_.getClass(sd.name, {}, static_cast<int>(sd.layout),
                         sd.baseClasses.empty() ? "" : sd.baseClasses[0].name,
                         sd.isClass, sd.isInterface);
        } else if (decl->kind == DeclKind::Enum) {
            auto& ed = static_cast<EnumDecl&>(*decl);
            std::vector<EnumVariantType> variants;
            for (int32_t i = 0; i < static_cast<int32_t>(ed.variants.size()); ++i) {
                const auto& v = ed.variants[i];
                std::vector<const Type*> payloadTys;
                for (const auto& pTyNode : v.payloadTypes) {
                    payloadTys.push_back(checker_.resolveType(*pTyNode));
                }
                variants.push_back(EnumVariantType{v.name, i, std::move(payloadTys)});
            }
            tc_.getEnum(ed.name, std::move(variants));
        }
    }

    // Pass 2: Base class hierarchy & inheritance cycle detection
    for (auto& decl : prog.decls) {
        if (decl->kind == DeclKind::Struct) {
            auto& sd = static_cast<StructDecl&>(*decl);
            if (!sd.baseClasses.empty()) {
                const auto& baseName = sd.baseClasses[0].name;
                auto baseTy = tc_.findRecord(baseName);
                if (!baseTy) {
                    diag_.report(sd.baseClasses[0].loc, Severity::Error,
                                 "base class '" + baseName + "' not found");
                } else {
                    // Check for inheritance cycles
                    bool hasCycle = false;
                    const Type* cur = baseTy;
                    while (cur) {
                        if (cur->structName == sd.name) {
                            diag_.report(sd.baseClasses[0].loc, Severity::Error,
                                         "inheritance cycle detected involving '" + sd.name + "'");
                            hasCycle = true;
                            break;
                        }
                        cur = cur->baseClass;
                    }
                    if (!hasCycle) {
                        auto thisTy = const_cast<Type*>(tc_.findRecord(sd.name));
                        if (thisTy) thisTy->baseClass = baseTy;
                    }
                }
            }
        }
    }

    // Pass 3: Resolve struct field types and compute ABI layout (inheriting base fields)
    for (auto& decl : prog.decls) {
        if (decl->kind == DeclKind::Struct) {
            auto& sd = static_cast<StructDecl&>(*decl);
            checker_.checkStructCAbi(sd);
            auto thisTy = const_cast<Type*>(tc_.findRecord(sd.name));
            std::vector<StructFieldType> fieldTypes;

            // Prepend base class fields (single inheritance layout)
            if (thisTy && thisTy->baseClass) {
                for (const auto& bf : thisTy->baseClass->structFields) {
                    fieldTypes.push_back(bf);
                }
            }

            for (auto& f : sd.fields) {
                auto fTy = checker_.resolveType(*f.type);
                if (fTy && fTy->isVoid()) {
                    diag_.report(f.loc, Severity::Error,
                                 "field '" + f.name + "' cannot be of type 'void'");
                }
                if (fTy->isRecord() && fTy->structName == sd.name) {
                    diag_.report(f.loc, Severity::Error,
                                 "recursive struct definition: struct '" + sd.name +
                                 "' cannot contain itself by value");
                }
                fieldTypes.push_back(StructFieldType{
                    .name = f.name,
                    .type = fTy,
                    .offset = 0,
                    .size = 0,
                    .alignment = 1,
                    .isConst = f.isConst,
                    .isCold = f.isCold,
                    .access = static_cast<int>(f.access),
                });
            }

            // Determine polymorphism
            bool poly = (thisTy && thisTy->baseClass && thisTy->baseClass->isPolymorphic);
            if (sd.destructor && sd.destructor->isVirtual) poly = true;
            for (const auto& m : sd.methods) {
                if (m.isVirtual || m.isOverride) poly = true;
            }
            if (thisTy) thisTy->isPolymorphic = poly;

            if (poly && (!thisTy->baseClass || !thisTy->baseClass->isPolymorphic)) {
                StructFieldType vptr;
                vptr.name = "_vptr";
                vptr.type = tc_.getReference(tc_.getI8());
                vptr.offset = 0;
                vptr.size = 8;
                vptr.alignment = 8;
                vptr.isConst = false;
                vptr.isCold = false;
                vptr.access = 0;
                fieldTypes.insert(fieldTypes.begin(), vptr);
            }

            tc_.getClass(sd.name, std::move(fieldTypes), static_cast<int>(sd.layout),
                         sd.baseClasses.empty() ? "" : sd.baseClasses[0].name,
                         sd.isClass, sd.isInterface);
        }
    }

    // Pass 4: Register member methods, constructors, and destructors
    for (auto& decl : prog.decls) {
        if (decl->kind == DeclKind::Struct) {
            auto& sd = static_cast<StructDecl&>(*decl);
            auto thisTy = const_cast<Type*>(tc_.findRecord(sd.name));
            if (!thisTy) continue;

            int nextVtableSlot = 0;
            if (thisTy->baseClass) {
                thisTy->vtable = thisTy->baseClass->vtable;
                for (const auto& bm : thisTy->baseClass->methods) {
                    if (bm.vtableSlot >= nextVtableSlot) {
                        nextVtableSlot = bm.vtableSlot + 1;
                    }
                }
            }

            std::unordered_map<std::string, bool> pureVirtuals;
            if (thisTy->baseClass) {
                for (const auto& bm : thisTy->baseClass->methods) {
                    if (bm.isPureVirtual) pureVirtuals[bm.name] = true;
                }
            }

            for (auto& m : sd.methods) {
                auto retTy = checker_.resolveType(*m.returnType);
                std::vector<const Type*> paramTypes;
                std::vector<std::string> paramNames;
                std::vector<const ast::Expr*> defaultArgs;
                bool seenDefault = false;
                for (auto& p : m.params) {
                    auto pTy = checker_.resolveType(*p.type);
                    if (p.isArray) pTy = tc_.getArrayView(pTy, p.isConst);
                    if (p.defaultValue) {
                        seenDefault = true;
                        defaultArgs.push_back(p.defaultValue.get());
                    } else {
                        if (seenDefault) {
                            diag_.report(p.loc, Severity::Error,
                                "parameter without default argument cannot follow parameter with default argument");
                        }
                        defaultArgs.push_back(nullptr);
                    }
                    paramTypes.push_back(pTy);
                    paramNames.push_back(p.name);
                }

                int vSlot = -1;
                bool isVirt = m.isVirtual;

                const MethodType* baseMethod = thisTy->baseClass ? thisTy->baseClass->findMethod(m.name) : nullptr;
                if (m.isOverride) {
                    if (!baseMethod || !baseMethod->isVirtual) {
                        diag_.report(m.loc, Severity::Error,
                                     "method '" + m.name + "' marked 'override' does not override any virtual base class method");
                    } else {
                        isVirt = true;
                        vSlot = baseMethod->vtableSlot;
                        pureVirtuals.erase(m.name);
                    }
                } else if (baseMethod && baseMethod->isVirtual) {
                    isVirt = true;
                    vSlot = baseMethod->vtableSlot;
                    pureVirtuals.erase(m.name);
                } else if (isVirt) {
                    vSlot = nextVtableSlot++;
                }

                if (m.isPureVirtual) {
                    pureVirtuals[m.name] = true;
                    thisTy->isAbstract = true;
                }

                if (isVirt && vSlot >= 0) {
                    if (vSlot >= static_cast<int>(thisTy->vtable.size())) {
                        thisTy->vtable.resize(vSlot + 1);
                    }
                    thisTy->vtable[vSlot] = m.isPureVirtual ? "" : mangleMethodName(sd.name, m.name, m.params.size());
                }

                thisTy->methods.push_back(MethodType{
                    .name = m.name,
                    .definingClass = sd.name,
                    .returnType = retTy,
                    .paramTypes = std::move(paramTypes),
                    .paramNames = std::move(paramNames),
                    .defaultArgs = std::move(defaultArgs),
                    .isConst = m.isConst,
                    .isVirtual = isVirt,
                    .isOverride = m.isOverride,
                    .isPureVirtual = m.isPureVirtual,
                    .isAsync = m.isAsync,
                    .vtableSlot = vSlot,
                    .access = static_cast<int>(m.access),
                });
            }

            if (!pureVirtuals.empty()) {
                thisTy->isAbstract = true;
            }

            for (auto& c : sd.constructors) {
                std::vector<const Type*> cParamTypes;
                std::vector<std::string> cParamNames;
                for (auto& p : c.params) {
                    auto pTy = checker_.resolveType(*p.type);
                    if (p.isArray) pTy = tc_.getArrayView(pTy);
                    cParamTypes.push_back(pTy);
                    cParamNames.push_back(p.name);
                }
                thisTy->constructors.push_back(ConstructorType{
                    .paramTypes = std::move(cParamTypes),
                    .paramNames = std::move(cParamNames),
                    .access = static_cast<int>(c.access),
                });
            }

            if (sd.destructor) {
                thisTy->destructor = std::make_unique<DestructorType>(DestructorType{
                    .isVirtual = sd.destructor->isVirtual,
                    .access = static_cast<int>(sd.destructor->access),
                });
            }
        }
    }

    // Pass 3: Register functions and constants
    for (auto& decl : prog.decls) {
        if (decl->kind == DeclKind::Function) {
            auto& fn = static_cast<FunctionDecl&>(*decl);
            checker_.checkFunctionCAbi(fn);
            auto retTy = checker_.resolveType(*fn.returnType);

            std::vector<const Type*> paramTypes;
            std::vector<const ast::Expr*> defaultArgs;
            bool seenDefault = false;
            for (auto& p : fn.params) {
                auto elemTy = checker_.resolveType(*p.type);
                if (elemTy && elemTy->isVoid()) {
                    diag_.report(p.loc, Severity::Error,
                                 "parameter '" + p.name + "' cannot be of type 'void'");
                }
                if (p.defaultValue) {
                    seenDefault = true;
                    defaultArgs.push_back(p.defaultValue.get());
                } else {
                    if (seenDefault) {
                        diag_.report(p.loc, Severity::Error,
                            "parameter without default argument cannot follow parameter with default argument");
                    }
                    defaultArgs.push_back(nullptr);
                }
                if (p.isArray) {
                    paramTypes.push_back(tc_.getArrayView(elemTy));
                } else {
                    paramTypes.push_back(elemTy);
                }
            }

            auto fnTy = tc_.getFunction(retTy, std::move(paramTypes), std::move(defaultArgs));
            checker_.registerGlobalFunction(fn.name, fnTy);

            Symbol sym{
                .name        = fn.name,
                .type        = fnTy,
                .isConst     = true,
                .isFunction  = true,
                .isParameter = false,
                .isAsync     = fn.isAsync,
                .loc         = fn.loc,
            };

            if (!syms_.declare(fn.name, std::move(sym))) {
                diag_.report(fn.loc, Severity::Error,
                             "redefinition of function '" + fn.name + "'");
            }
        }
        else if (decl->kind == DeclKind::Const) {
            auto& cd = static_cast<ConstDecl&>(*decl);
            auto ty = checker_.resolveType(*cd.type);
            ProvenanceSet cProv(ProvenanceKind::StaticStorage, 0, cd.name);
            Symbol sym{
                .name        = cd.name,
                .type        = ty,
                .isConst     = true,
                .isFunction  = false,
                .isParameter = false,
                .regionId    = 0,
                .provenance  = cProv,
                .loc         = cd.loc,
            };
            checker_.setVarProvenance(cd.name, cProv);
            if (!syms_.declare(cd.name, std::move(sym))) {
                diag_.report(cd.loc, Severity::Error,
                             "redefinition of constant '" + cd.name + "'");
            }
        }
        else if (decl->kind == DeclKind::Var) {
            auto& gv = static_cast<GlobalVarDecl&>(*decl);
            auto elemTy = checker_.resolveType(*gv.type);
            const Type* varTy = elemTy;
            if (gv.isArray) {
                if (gv.arraySize) {
                    auto eval = checker_.evalIntConstExpr(*gv.arraySize);
                    if (eval.isConst && !eval.overflow && eval.value >= 0) {
                        varTy = tc_.getArray(elemTy, eval.value);
                    } else {
                        varTy = tc_.getArray(elemTy, 0);
                    }
                } else {
                    varTy = tc_.getArrayView(elemTy, gv.isConst);
                }
            }
            ProvenanceSet gProv(ProvenanceKind::StaticStorage, 0, gv.name);
            Symbol sym{
                .name        = gv.name,
                .type        = varTy,
                .isConst     = gv.isConst,
                .isFunction  = false,
                .isParameter = false,
                .regionId    = 0,
                .provenance  = gProv,
                .loc         = gv.loc,
            };
            checker_.setVarProvenance(gv.name, gProv);
            if (!syms_.declare(gv.name, std::move(sym))) {
                diag_.report(gv.loc, Severity::Error,
                             "redefinition of variable '" + gv.name + "'");
            }

            MemoryReportEntry rep;
            rep.name = gv.name;
            rep.typeName = varTy ? varTy->toString() : "unknown";
            rep.size = varTy ? varTy->size() : 0;
            rep.align = varTy ? varTy->alignment() : 0;
            rep.storageClass = "Global";
            rep.storageRegion = "-";
            rep.lifetime = "Static";
            rep.dtorKind = (varTy && varTy->hasNonTrivialDestructor()) ? "Direct RAII" : "None";
            rep.hasNonTrivialDestructor = varTy ? varTy->hasNonTrivialDestructor() : false;
            rep.requiresSidecar = false;
            rep.containsReferencesOrViews = varTy ? varTy->canContainBorrow() : false;
            rep.hasExternalBorrow = false;
            rep.containsBorrows = rep.containsReferencesOrViews;
            rep.payloadContribution = rep.size;
            checker_.addMemoryReportEntry(std::move(rep));
        }
    }
}

void Sema::checkFunctionBodies(ast::Program& prog) {
    checker_.setActiveProgram(&prog);
    for (size_t dIdx = 0; dIdx < prog.decls.size(); ++dIdx) {
        auto& decl = prog.decls[dIdx];
        if (decl->kind == DeclKind::Function) {
            auto& fn = static_cast<FunctionDecl&>(*decl);
            if (!fn.typeParams.empty()) continue; // Skip unmonomorphized generic template
            auto fnSymPtr = syms_.lookup(fn.name);
            if (!fnSymPtr) continue;
            auto fnSym = *fnSymPtr; // Copy symbol to avoid dangling pointer on pushScope

            auto retTy = fnSym.type->returnType;
            checker_.setCurrentFunction(fn.name);

            // Push function scope and register parameters
            syms_.pushScope();
            checker_.pushScope();
            for (size_t i = 0; i < fn.params.size(); ++i) {
                auto& p = fn.params[i];
                auto paramTy = fnSym.type->paramTypes[i];

                ProvenanceSet pProv;
                if (paramTy && (paramTy->isReference() || paramTy->isArrayView() || paramTy->isRecord())) {
                    pProv = ProvenanceSet(ProvenanceKind::ParameterBorrow, i, p.name);
                }

                Symbol pSym{
                    .name        = p.name,
                    .type        = paramTy,
                    .isConst     = p.isConst,
                    .isFunction  = false,
                    .isParameter = true,
                    .regionId    = 0,
                    .provenance  = pProv,
                    .loc         = p.loc,
                };

                if (!syms_.declare(p.name, std::move(pSym))) {
                    diag_.report(p.loc, Severity::Error,
                                 "duplicate parameter name '" + p.name + "'");
                }
                checker_.markAssigned(p.name);
                checker_.setVarProvenance(p.name, pProv);
            }

            if (fn.body) {
                for (auto& stmt : fn.body->stmts) {
                    checker_.checkStmt(*stmt, retTy);
                }

                FunctionBorrowSummary summary;
                summary.functionName = fn.name;
                summary.hasBody = true;
                summary.isExternal = false;
                summary.isBuiltin = false;
                summary.parameterEffects.resize(fn.params.size(), BorrowEffect::NoEscape);
                summary.receiverEffect = BorrowEffect::NoEscape;

                ProvenanceSet retOrigins;
                collectReturnProvenances(*fn.body, checker_, retOrigins);
                for (const auto& o : retOrigins.origins) {
                    if (o.kind == ProvenanceKind::ParameterBorrow) {
                        summary.returnBorrowsFromParam.push_back(static_cast<int>(o.identity));
                    }
                }
                checker_.registerFunctionSummary(fn.name, summary);

                // Non-void functions must return a value on all reachable paths
                if (!retTy->isVoid() && !returnsOnAllPaths(*fn.body)) {
                    diag_.report(fn.loc, Severity::Error,
                                 "control reaches end of non-void function '" + fn.name + "'");
                }
            } else if (fn.isExtern) {
                FunctionBorrowSummary summary;
                summary.functionName = fn.name;
                summary.hasBody = false;
                summary.isExternal = true;
                summary.isBuiltin = false;
                summary.parameterEffects.resize(fn.params.size(), BorrowEffect::Unknown);
                summary.receiverEffect = BorrowEffect::Unknown;
                checker_.registerFunctionSummary(fn.name, summary);
            } else if (!retTy->isVoid()) {
                diag_.report(fn.loc, Severity::Error,
                             "non-void function '" + fn.name + "' has no body");
            }

            checker_.popScope();
            syms_.popScope();
        }
    }

    // 2. Struct and Class member methods, constructors, and destructors
    for (size_t sIdx = 0; sIdx < prog.decls.size(); ++sIdx) {
        auto& decl = prog.decls[sIdx];
        if (decl->kind == DeclKind::Struct) {
            auto& sd = static_cast<StructDecl&>(*decl);
            if (!sd.typeParams.empty()) continue; // Skip unmonomorphized template
            auto recTy = tc_.findRecord(sd.name);
            if (!recTy) continue;

            // Constructors
            for (auto& c : sd.constructors) {
                if (!c.body) continue;
                checker_.setCurrentRecord(recTy, false);
                syms_.pushScope();
                checker_.pushScope();

                ProvenanceSet thisProv(ProvenanceKind::ThisBorrow, 0, "this");
                Symbol thisSym{
                    .name        = "this",
                    .type        = tc_.getReference(recTy, false),
                    .isConst     = false,
                    .isFunction  = false,
                    .isParameter = true,
                    .regionId    = 0,
                    .provenance  = thisProv,
                    .loc         = c.loc,
                };
                syms_.declare("this", std::move(thisSym));
                checker_.markAssigned("this");
                checker_.setVarProvenance("this", thisProv);

                for (size_t i = 0; i < c.params.size(); ++i) {
                    auto& p = c.params[i];
                    auto pTy = checker_.resolveType(*p.type);
                    if (p.isArray) pTy = tc_.getArrayView(pTy, p.isConst);
                    ProvenanceSet pProv;
                    if (pTy && (pTy->isReference() || pTy->isArrayView() || pTy->isRecord())) {
                        pProv = ProvenanceSet(ProvenanceKind::ParameterBorrow, i, p.name);
                    }
                    Symbol pSym{
                        .name        = p.name,
                        .type        = pTy,
                        .isConst     = p.isConst,
                        .isFunction  = false,
                        .isParameter = true,
                        .regionId    = 0,
                        .provenance  = pProv,
                        .loc         = p.loc,
                    };
                    syms_.declare(p.name, std::move(pSym));
                    checker_.markAssigned(p.name);
                    checker_.setVarProvenance(p.name, pProv);
                }

                for (auto& s : c.body->stmts) {
                    checker_.checkStmt(*s, tc_.getVoid());
                }

                checker_.popScope();
                syms_.popScope();
                checker_.setCurrentRecord(nullptr);
            }

            // Destructors
            if (sd.destructor && sd.destructor->body) {
                checker_.setCurrentRecord(recTy, false);
                syms_.pushScope();
                checker_.pushScope();

                ProvenanceSet thisProv(ProvenanceKind::ThisBorrow, 0, "this");
                Symbol thisSym{
                    .name        = "this",
                    .type        = tc_.getReference(recTy, false),
                    .isConst     = false,
                    .isFunction  = false,
                    .isParameter = true,
                    .regionId    = 0,
                    .provenance  = thisProv,
                    .loc         = sd.destructor->loc,
                };
                syms_.declare("this", std::move(thisSym));
                checker_.markAssigned("this");
                checker_.setVarProvenance("this", thisProv);

                for (auto& s : sd.destructor->body->stmts) {
                    checker_.checkStmt(*s, tc_.getVoid());
                }

                checker_.popScope();
                syms_.popScope();
                checker_.setCurrentRecord(nullptr);
            }

            // Member Methods
            for (auto& m : sd.methods) {
                if (!m.body) continue;
                std::string methodMangled = sd.name + "_" + m.name;
                checker_.setCurrentFunction(methodMangled);
                checker_.setCurrentRecord(recTy, m.isConst);
                syms_.pushScope();
                checker_.pushScope();

                ProvenanceSet thisProv(ProvenanceKind::ThisBorrow, 0, "this");
                Symbol thisSym{
                    .name        = "this",
                    .type        = tc_.getReference(recTy, m.isConst),
                    .isConst     = m.isConst,
                    .isFunction  = false,
                    .isParameter = true,
                    .regionId    = 0,
                    .provenance  = thisProv,
                    .loc         = m.loc,
                };
                syms_.declare("this", std::move(thisSym));
                checker_.markAssigned("this");
                checker_.setVarProvenance("this", thisProv);

                auto retTy = checker_.resolveType(*m.returnType);

                for (size_t i = 0; i < m.params.size(); ++i) {
                    auto& p = m.params[i];
                    auto pTy = checker_.resolveType(*p.type);
                    if (p.isArray) pTy = tc_.getArrayView(pTy, p.isConst);
                    ProvenanceSet pProv;
                    if (pTy && (pTy->isReference() || pTy->isArrayView() || pTy->isRecord())) {
                        pProv = ProvenanceSet(ProvenanceKind::ParameterBorrow, i, p.name);
                    }
                    Symbol pSym{
                        .name        = p.name,
                        .type        = pTy,
                        .isConst     = p.isConst,
                        .isFunction  = false,
                        .isParameter = true,
                        .regionId    = 0,
                        .provenance  = pProv,
                        .loc         = p.loc,
                    };
                    syms_.declare(p.name, std::move(pSym));
                    checker_.markAssigned(p.name);
                    checker_.setVarProvenance(p.name, pProv);
                }

                for (auto& s : m.body->stmts) {
                    checker_.checkStmt(*s, retTy);
                }

                FunctionBorrowSummary mSummary;
                mSummary.functionName = methodMangled;
                mSummary.hasBody = true;
                mSummary.isExternal = false;
                mSummary.isBuiltin = false;
                mSummary.parameterEffects.resize(m.params.size(), BorrowEffect::NoEscape);
                mSummary.receiverEffect = BorrowEffect::NoEscape;

                ProvenanceSet retOrigins;
                collectReturnProvenances(*m.body, checker_, retOrigins);
                for (const auto& o : retOrigins.origins) {
                    if (o.kind == ProvenanceKind::ParameterBorrow) {
                        mSummary.returnBorrowsFromParam.push_back(static_cast<int>(o.identity));
                    } else if (o.kind == ProvenanceKind::ThisBorrow) {
                        mSummary.returnBorrowsFromThis = true;
                    }
                }
                checker_.registerFunctionSummary(methodMangled, mSummary);
                checker_.registerFunctionSummary(m.name, mSummary);

                if (!retTy->isVoid() && !returnsOnAllPaths(*m.body)) {
                    diag_.report(m.loc, Severity::Error,
                                 "control reaches end of non-void method '" + m.name + "'");
                }

                checker_.popScope();
                syms_.popScope();
                checker_.setCurrentRecord(nullptr);
            }
        }
    }
}

void Sema::validateMain(const ast::Program& prog) {
    auto mainSym = syms_.lookup("main");
    if (!mainSym) {
        if (isLibraryMode_) {
            return;
        }
        for (const auto& decl : prog.decls) {
            if (decl->kind == DeclKind::Function) {
                auto& fn = static_cast<FunctionDecl&>(*decl);
                if (fn.isExported) {
                    return; // Exported module / shared library does not require main()
                }
            }
        }
        diag_.report(Severity::Error, "program requires a 'main' function");
        return;
    }

    if (!mainSym->isFunction) {
        diag_.report(mainSym->loc, Severity::Error, "'main' must be a function");
        return;
    }

    if (!mainSym->type->returnType->isInteger()) {
        diag_.report(mainSym->loc, Severity::Error,
                     "'main' must return 'int', got " +
                     mainSym->type->returnType->toString());
    }
}

} // namespace tamarin::sema

