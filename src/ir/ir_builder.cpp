// ============================================================================
// ir_builder.cpp - AST to Tamarin IR lowering implementation
// ============================================================================

#include "tamarin/ir/ir_builder.h"
#include "tamarin/driver/module_graph.h"
#include <unordered_set>

namespace tamarin::ir {

using namespace ast;

IRBuilder::IRBuilder(sema::TypeChecker& checker)
    : checker_(checker) {}

ValueId IRBuilder::nextValueId() {
    return nextValueId_++;
}

std::string IRBuilder::nextLabel(const std::string& prefix) {
    return prefix + "_" + std::to_string(nextLabelId_++);
}

void IRBuilder::setInsertBlock(BasicBlock* block) {
    currentBlock_ = block;
}

bool IRBuilder::isBlockTerminated(const BasicBlock* block) const noexcept {
    return block && block->hasTerminator();
}

bool IRBuilder::isCurrentBlockTerminated() const noexcept {
    return isBlockTerminated(currentBlock_);
}

int64_t IRBuilder::registerStringLiteral(const std::string& str) {
    if (!currentModule_) return 0;
    for (size_t i = 0; i < currentModule_->stringLiterals.size(); ++i) {
        if (currentModule_->stringLiterals[i] == str) {
            return static_cast<int64_t>(i);
        }
    }
    currentModule_->stringLiterals.push_back(str);
    return static_cast<int64_t>(currentModule_->stringLiterals.size() - 1);
}

Instruction& IRBuilder::emit(Opcode op, const sema::Type* type) {
    Instruction inst;
    inst.opcode = op;
    inst.result = nextValueId();
    inst.type   = type;
    return emit(std::move(inst));
}

Instruction& IRBuilder::emit(Instruction inst) {
    if (currentBlock_ && !currentBlock_->hasTerminator()) {
        currentBlock_->instructions.push_back(std::move(inst));
        return currentBlock_->instructions.back();
    }
    deadInst_ = std::move(inst);
    return deadInst_;
}

ValueId IRBuilder::emitIntConstant(int64_t val, const sema::Type* ty) {
    Instruction c;
    c.opcode = Opcode::IConst;
    c.result = nextValueId();
    c.intImm = val;
    c.type   = ty ? ty : checker_.context().getI32();
    emit(c);
    return c.result;
}

void IRBuilder::pushScope() {
    scopeStack_.emplace_back();
    cleanupScopes_.emplace_back();
}

void IRBuilder::emitCleanupsUpTo(size_t targetScopeDepth) {
    if (!currentBlock_ || isCurrentBlockTerminated()) return;
    for (size_t i = cleanupScopes_.size(); i > targetScopeDepth; --i) {
        auto& scope = cleanupScopes_[i - 1];
        for (auto it = scope.rbegin(); it != scope.rend(); ++it) {
            if (it->isMoved) {
                continue;
            }
            if (it->kind == CleanupKind::RegionDestroy) {
                Instruction rd;
                rd.opcode = Opcode::RegionDestroy;
                rd.op1 = it->ptr;
                emit(rd);
            } else if (it->kind == CleanupKind::SlabDestroy) {
                Instruction sd;
                sd.opcode = Opcode::SlabDestroy;
                sd.op1 = it->ptr;
                if (it->type && it->type->elementType && it->type->elementType->hasNonTrivialDestructor()) {
                    sd.strImm = it->type->elementType->destructorName();
                    ensureDestructor(it->type->elementType);
                }
                emit(sd);
            } else if (it->type && it->type->hasNonTrivialDestructor()) {
                std::string dtorName = it->type->destructorName();
                ensureDestructor(it->type);

                if (it->dropFlag != InvalidValue && currentFunction_) {
                    Instruction loadFlag;
                    loadFlag.opcode = Opcode::Load;
                    loadFlag.result = nextValueId();
                    loadFlag.op1    = it->dropFlag;
                    loadFlag.type   = checker_.context().getBool();
                    emit(loadFlag);

                    auto dropBlock = currentFunction_->createBlock(nextLabel("drop_alive"));
                    auto skipBlock = currentFunction_->createBlock(nextLabel("drop_skip"));

                    Instruction br;
                    br.opcode = Opcode::Branch;
                    br.op1 = loadFlag.result;
                    br.label1 = dropBlock->label;
                    br.label2 = skipBlock->label;
                    emit(br);

                    setInsertBlock(dropBlock);
                    Instruction call;
                    call.opcode = Opcode::Call;
                    call.type   = checker_.context().getVoid();
                    call.result = InvalidValue;
                    call.strImm = dtorName;
                    call.callArgs = { it->ptr };
                    emit(call);

                    Instruction iconst0;
                    iconst0.opcode = Opcode::IConst;
                    iconst0.result = nextValueId();
                    iconst0.type   = checker_.context().getBool();
                    iconst0.intImm = 0;
                    emit(iconst0);

                    Instruction dfStore;
                    dfStore.opcode = Opcode::Store;
                    dfStore.op1    = it->dropFlag;
                    dfStore.op2    = iconst0.result;
                    emit(dfStore);

                    Instruction jmp;
                    jmp.opcode = Opcode::Jump;
                    jmp.label1 = skipBlock->label;
                    emit(jmp);

                    setInsertBlock(skipBlock);
                } else {
                    Instruction call;
                    call.opcode = Opcode::Call;
                    call.type   = checker_.context().getVoid();
                    call.result = InvalidValue;
                    call.strImm = dtorName;
                    call.callArgs = { it->ptr };
                    emit(call);
                }
            }
        }
    }
}

void IRBuilder::popScope() {
    if (!cleanupScopes_.empty()) {
        emitCleanupsUpTo(cleanupScopes_.size() - 1);
        cleanupScopes_.pop_back();
    }
    if (!scopeStack_.empty()) {
        scopeStack_.pop_back();
    }
}

void IRBuilder::setBinding(const std::string& name, Binding binding) {
    if (!scopeStack_.empty()) {
        scopeStack_.back()[name] = std::move(binding);
    }
}

const Binding* IRBuilder::lookupBinding(const std::string& name) const {
    for (auto it = scopeStack_.rbegin(); it != scopeStack_.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) {
            return &found->second;
        }
    }
    return nullptr;
}

ValueId IRBuilder::getVar(const std::string& name) {
    auto* b = lookupBinding(name);
    return b ? b->ptr : InvalidValue;
}

const sema::Type* IRBuilder::getVarType(const std::string& name) {
    auto* b = lookupBinding(name);
    return b ? b->type : nullptr;
}

ValueId IRBuilder::emitCastIfNeeded(ValueId val, const sema::Type* fromTy, const sema::Type* toTy) {
    if (!fromTy || !toTy || *fromTy == *toTy) {
        return val;
    }
    if (fromTy->isArray() && toTy->isArrayView() && *fromTy->elementType == *toTy->elementType) {
        return val;
    }
    Instruction castInst;
    castInst.opcode = Opcode::Cast;
    castInst.result = nextValueId();
    castInst.op1    = val;
    castInst.type   = toTy;
    emit(castInst);
    return castInst.result;
}

// ── Module Building ─────────────────────────────────────────────────────────

std::unique_ptr<IRModule> IRBuilder::buildModule(Program& prog, const std::string& moduleName) {
    auto mod = std::make_unique<IRModule>();
    mod->name = moduleName;
    currentModule_ = mod.get();

    // 1. Collect all record types and enum types
    for (auto& decl : prog.decls) {
        if (decl->kind == DeclKind::Struct) {
            auto& sd = static_cast<StructDecl&>(*decl);
            if (!sd.typeParams.empty()) continue; // Skip unmonomorphized template
            auto recTy = checker_.context().findRecord(sd.name);
            if (recTy) {
                mod->recordTypes.push_back(recTy);
            }
        }
    }
    mod->enumTypes = checker_.context().getEnumTypes();

    // 2. Lower methods, constructors, and destructors
    for (auto& decl : prog.decls) {
        if (decl->kind == DeclKind::Struct) {
            auto& sd = static_cast<StructDecl&>(*decl);
            if (!sd.typeParams.empty()) continue; // Skip unmonomorphized template
            for (size_t i = 0; i < sd.constructors.size(); ++i) {
                lowerConstructor(sd, sd.constructors[i], i);
            }
            auto recTy = checker_.context().findRecord(sd.name);
            if (sd.destructor) {
                lowerDestructor(sd, sd.destructor.get());
            } else if (recTy && recTy->hasNonTrivialDestructor()) {
                lowerDestructor(sd, nullptr);
            }
            for (auto& m : sd.methods) {
                lowerMethod(sd, m);
            }
        }
    }

    // 3. Lower free functions
    for (auto& decl : prog.decls) {
        if (decl->kind == DeclKind::Function) {
            auto& fn = static_cast<FunctionDecl&>(*decl);
            if (!fn.typeParams.empty()) continue; // Skip unmonomorphized template
            lowerFunction(fn);
        }
    }

    // 4. Synthesize enum destructors for enums with non-trivial payloads
    for (const auto* eTy : mod->enumTypes) {
        if (eTy && eTy->hasNonTrivialDestructor()) {
            lowerEnumDestructor(eTy);
        }
    }

    currentModule_ = nullptr;
    return mod;
}

std::unique_ptr<IRModule> IRBuilder::buildProgram(
    const std::vector<std::shared_ptr<driver::Module>>& modules,
    const std::string& programName) {
    auto mod = std::make_unique<IRModule>();
    mod->name = programName;
    currentModule_ = mod.get();

    // 1. Collect all record types and enum types across all modules
    mod->recordTypes = checker_.context().getRecordTypes();
    mod->enumTypes = checker_.context().getEnumTypes();

    // 2. Lower methods, constructors, and destructors across all modules
    std::unordered_set<std::string> loweredStructs;
    for (const auto& m : modules) {
        if (!m || !m->ast) continue;
        checker_.setCurrentModuleScope(checker_.getModuleScope(m->id.logicalName));
        for (auto& decl : m->ast->decls) {
            if (decl->kind == DeclKind::Struct) {
                auto& sd = static_cast<StructDecl&>(*decl);
                if (!sd.typeParams.empty()) continue; // Skip unmonomorphized template
                if (!loweredStructs.insert(sd.name).second) continue;
                for (size_t i = 0; i < sd.constructors.size(); ++i) {
                    lowerConstructor(sd, sd.constructors[i], i);
                }
                auto recTy = checker_.context().findRecord(sd.name);
                if (sd.destructor) {
                    lowerDestructor(sd, sd.destructor.get());
                } else if (recTy && recTy->hasNonTrivialDestructor()) {
                    lowerDestructor(sd, nullptr);
                }
                for (auto& meth : sd.methods) {
                    lowerMethod(sd, meth);
                }
            }
        }
    }

    // 3. Lower free functions across all modules
    for (const auto& m : modules) {
        if (!m || !m->ast) continue;
        checker_.setCurrentModuleScope(checker_.getModuleScope(m->id.logicalName));
        for (auto& decl : m->ast->decls) {
            if (decl->kind == DeclKind::Function) {
                auto& fn = static_cast<FunctionDecl&>(*decl);
                if (!fn.typeParams.empty()) continue; // Skip unmonomorphized template
                lowerFunction(fn);
            }
        }
    }

    // 4. Synthesize enum destructors for enums with non-trivial payloads
    for (const auto* eTy : mod->enumTypes) {
        if (eTy && eTy->hasNonTrivialDestructor()) {
            lowerEnumDestructor(eTy);
        }
    }

    currentModule_ = nullptr;
    return mod;
}

// ── OOP Lowering: Methods, Constructors, Destructors ────────────────────────

void IRBuilder::lowerMethod(StructDecl& sd, MethodDecl& m) {
    varDropFlags_.clear();
    if (!m.body) return; // Pure virtual or declaration only

    auto recTy = checker_.context().findRecord(sd.name);
    auto retTy = checker_.resolveType(*m.returnType);
    std::string fnName = sema::mangleMethodName(sd.name, m.name, m.params.size());

    auto irFn = currentModule_->createFunction(fnName, retTy);
    irFn->isLinkOnceODR = (fnName.find("__") != std::string::npos);
    currentFunction_ = irFn;
    currentRecord_ = recTy;
    isCurrentMethodConst_ = m.isConst;

    pushScope();

    auto entryBlock = irFn->createBlock("entry");
    setInsertBlock(entryBlock);

    // Param 0 is 'this'
    auto thisTy = checker_.context().getReference(recTy, m.isConst);
    ValueId thisParamVal = nextValueId();
    irFn->params.push_back(IRParam{
        .name = "this",
        .type = thisTy,
        .valueId = thisParamVal,
    });

    Instruction thisAlloca;
    thisAlloca.opcode = Opcode::Alloca;
    thisAlloca.result = nextValueId();
    thisAlloca.type = thisTy;
    emit(thisAlloca);

    Instruction thisStore;
    thisStore.opcode = Opcode::Store;
    thisStore.op1 = thisAlloca.result;
    thisStore.op2 = thisParamVal;
    emit(thisStore);

    currentThisValue_ = thisParamVal;

    // Remaining parameters
    for (auto& p : m.params) {
        auto elemTy = checker_.resolveType(*p.type);
        const sema::Type* paramTy = elemTy;
        if (p.isArray) {
            paramTy = checker_.context().getArrayView(elemTy);
        }

        ValueId pVal = nextValueId();
        irFn->params.push_back(IRParam{
            .name = p.name,
            .type = paramTy,
            .valueId = pVal,
        });

        Instruction allocaInst;
        allocaInst.opcode = Opcode::Alloca;
        allocaInst.result = nextValueId();
        allocaInst.type = paramTy;
        emit(allocaInst);

        Instruction storeInst;
        storeInst.opcode = Opcode::Store;
        storeInst.op1 = allocaInst.result;
        storeInst.op2 = pVal;
        emit(storeInst);

        Binding b;
        b.ptr = allocaInst.result;
        b.type = paramTy;
        b.isConst = p.isConst;
        b.isParameter = true;

        if (p.isArray) {
            Instruction getLenInst;
            getLenInst.opcode = Opcode::GetLen;
            getLenInst.result = nextValueId();
            getLenInst.op1 = pVal;
            getLenInst.type = checker_.context().getUsize();
            emit(getLenInst);
            b.arrayLenValueId = getLenInst.result;
        }

        setBinding(p.name, std::move(b));
    }

    if (m.body) {
        lowerBlock(*m.body);
    }

    if (currentBlock_ && !isCurrentBlockTerminated()) {
        if (retTy->isVoid()) {
            Instruction retInst;
            retInst.opcode = Opcode::Return;
            retInst.op1 = InvalidValue;
            emit(retInst);
        }
    }

    popScope();
    currentThisValue_ = InvalidValue;
    currentRecord_ = nullptr;
    isCurrentMethodConst_ = false;
    currentFunction_ = nullptr;
}

bool IRBuilder::needsDefaultConstruction(const sema::Type* type) const {
    if (!type) return false;
    if (type->isRecord()) {
        if (type->isPolymorphic) return true;
        for (const auto& c : type->constructors) {
            if (c.paramTypes.empty()) return true;
        }
        if (type->baseClass && needsDefaultConstruction(type->baseClass)) return true;
        for (const auto& f : type->structFields) {
            if (f.name == "_vptr") continue;
            if (!f.type || f.type->isReference()) continue;
            if (needsDefaultConstruction(f.type)) return true;
        }
    } else if (type->isArray()) {
        return needsDefaultConstruction(type->elementType);
    }
    return false;
}

void IRBuilder::constructDefaultObject(ValueId objPtr, const sema::Type* type) {
    if (!type || !currentBlock_ || isCurrentBlockTerminated()) return;
    if (type->isRecord()) {
        // If the record has a user-defined default constructor, call it!
        bool calledUserCtor = false;
        for (const auto& c : type->constructors) {
            if (c.paramTypes.empty()) {
                std::string ctorName = type->constructors.size() == 1
                    ? (type->structName + "_" + type->structName)
                    : (type->structName + "_" + type->structName + "_0");
                Instruction callCtor;
                callCtor.opcode = Opcode::Call;
                callCtor.type   = checker_.context().getVoid();
                callCtor.strImm = ctorName;
                callCtor.callArgs = { objPtr };
                emit(callCtor);
                calledUserCtor = true;
                break;
            }
        }
        if (!calledUserCtor) {
            // Implicit aggregate default construction:
            // 1. Base class
            if (type->baseClass && needsDefaultConstruction(type->baseClass)) {
                constructDefaultObject(objPtr, type->baseClass);
            }
            // 2. Initialize _vptr if polymorphic
            if (type->isPolymorphic) {
                Instruction gv;
                gv.opcode = Opcode::GetVTablePtr;
                gv.result = nextValueId();
                gv.strImm = type->structName;
                gv.type   = checker_.context().getReference(checker_.context().getI8());
                emit(gv);

                Instruction gfp;
                gfp.opcode = Opcode::GetFieldPtr;
                gfp.result = nextValueId();
                gfp.op1    = objPtr;
                gfp.intImm = 0;
                gfp.type   = gv.type;
                gfp.strImm = type->structName;
                emit(gfp);

                Instruction st;
                st.opcode = Opcode::Store;
                st.op1    = gfp.result;
                st.op2    = gv.result;
                emit(st);
            }
            // 3. Member subobjects in declaration order
            size_t baseFieldCount = type->baseClass ? type->baseClass->structFields.size() : 0;
            for (size_t i = baseFieldCount; i < type->structFields.size(); ++i) {
                const auto& f = type->structFields[i];
                if (f.name == "_vptr") continue;
                if (!f.type || f.type->isReference()) continue;
                if (needsDefaultConstruction(f.type)) {
                    Instruction gfp;
                    gfp.opcode = Opcode::GetFieldPtr;
                    gfp.result = nextValueId();
                    gfp.op1    = objPtr;
                    gfp.intImm = static_cast<int64_t>(i);
                    gfp.type   = checker_.context().getReference(f.type);
                    gfp.strImm = type->structName;
                    emit(gfp);

                    constructDefaultObject(gfp.result, f.type);
                }
            }
        }
    } else if (type->isArray()) {
        constructArrayElements(objPtr, type->elementType, 0, type->arraySize);
    }
}

void IRBuilder::constructArrayElements(ValueId arrayBasePtr, const sema::Type* elemTy, int64_t startIndex, int64_t count) {
    if (!elemTy || count <= 0 || !currentBlock_ || isCurrentBlockTerminated()) return;
    for (int64_t i = startIndex; i < startIndex + count; ++i) {
        Instruction idxConst;
        idxConst.opcode = Opcode::IConst;
        idxConst.result = nextValueId();
        idxConst.type   = checker_.context().getUsize();
        idxConst.intImm = i;
        emit(idxConst);

        Instruction gep;
        gep.opcode = Opcode::GetArrayElemPtr;
        gep.result = nextValueId();
        gep.op1    = arrayBasePtr;
        gep.op2    = idxConst.result;
        gep.type   = elemTy;
        emit(gep);

        constructDefaultObject(gep.result, elemTy);
    }
}

void IRBuilder::lowerConstructor(StructDecl& sd, ConstructorDecl& c, size_t ctorIdx) {
    varDropFlags_.clear();
    (void)ctorIdx;
    if (!c.body) return;

    auto recTy = checker_.context().findRecord(sd.name);
    auto voidTy = checker_.context().getVoid();
    std::string fnName = sd.constructors.size() == 1
        ? (sd.name + "_" + sd.name)
        : (sd.name + "_" + sd.name + "_" + std::to_string(c.params.size()));

    auto irFn = currentModule_->createFunction(fnName, voidTy);
    irFn->isLinkOnceODR = (fnName.find("__") != std::string::npos);
    currentFunction_ = irFn;
    currentRecord_ = recTy;
    isCurrentMethodConst_ = false;

    pushScope();

    auto entryBlock = irFn->createBlock("entry");
    setInsertBlock(entryBlock);

    // Param 0 is 'this'
    auto thisTy = checker_.context().getReference(recTy, false);
    ValueId thisParamVal = nextValueId();
    irFn->params.push_back(IRParam{
        .name = "this",
        .type = thisTy,
        .valueId = thisParamVal,
    });

    Instruction thisAlloca;
    thisAlloca.opcode = Opcode::Alloca;
    thisAlloca.result = nextValueId();
    thisAlloca.type = thisTy;
    emit(thisAlloca);

    Instruction thisStore;
    thisStore.opcode = Opcode::Store;
    thisStore.op1 = thisAlloca.result;
    thisStore.op2 = thisParamVal;
    emit(thisStore);

    currentThisValue_ = thisParamVal;

    // Remaining parameters
    for (auto& p : c.params) {
        auto elemTy = checker_.resolveType(*p.type);
        const sema::Type* paramTy = elemTy;
        if (p.isArray) {
            paramTy = checker_.context().getArrayView(elemTy);
        }

        ValueId pVal = nextValueId();
        irFn->params.push_back(IRParam{
            .name = p.name,
            .type = paramTy,
            .valueId = pVal,
        });

        Instruction allocaInst;
        allocaInst.opcode = Opcode::Alloca;
        allocaInst.result = nextValueId();
        allocaInst.type = paramTy;
        emit(allocaInst);

        Instruction storeInst;
        storeInst.opcode = Opcode::Store;
        storeInst.op1 = allocaInst.result;
        storeInst.op2 = pVal;
        emit(storeInst);

        Binding b;
        b.ptr = allocaInst.result;
        b.type = paramTy;
        b.isConst = p.isConst;
        b.isParameter = true;
        setBinding(p.name, std::move(b));
    }

    // 1. If base class has a 0-parameter constructor, call it; otherwise perform implicit base construction
    if (recTy && recTy->baseClass) {
        bool calledBaseCtor = false;
        for (const auto& bc : recTy->baseClass->constructors) {
            if (bc.paramTypes.empty()) {
                std::string baseCtor = recTy->baseClass->constructors.size() == 1
                    ? (recTy->baseClass->structName + "_" + recTy->baseClass->structName)
                    : (recTy->baseClass->structName + "_" + recTy->baseClass->structName + "_0");
                Instruction callBase;
                callBase.opcode = Opcode::Call;
                callBase.strImm = baseCtor;
                callBase.callArgs = { thisParamVal };
                emit(callBase);
                calledBaseCtor = true;
                break;
            }
        }
        if (!calledBaseCtor && needsDefaultConstruction(recTy->baseClass)) {
            constructDefaultObject(thisParamVal, recTy->baseClass);
        }
    }

    // 2. If polymorphic, initialize _vptr to this class's vtable
    if (recTy && recTy->isPolymorphic) {
        Instruction gv;
        gv.opcode = Opcode::GetVTablePtr;
        gv.result = nextValueId();
        gv.strImm = sd.name;
        gv.type = checker_.context().getReference(checker_.context().getI8());
        emit(gv);

        Instruction gfp;
        gfp.opcode = Opcode::GetFieldPtr;
        gfp.result = nextValueId();
        gfp.op1 = thisParamVal;
        gfp.intImm = 0; // field 0 is _vptr
        gfp.type = gv.type;
        gfp.strImm = sd.name;
        emit(gfp);

        Instruction st;
        st.opcode = Opcode::Store;
        st.op1 = gfp.result;
        st.op2 = gv.result;
        emit(st);
    }

    // 3. Construct non-trivial member subobjects in declaration order
    if (recTy) {
        size_t baseFieldCount = recTy->baseClass ? recTy->baseClass->structFields.size() : 0;
        for (size_t i = baseFieldCount; i < recTy->structFields.size(); ++i) {
            const auto& f = recTy->structFields[i];
            if (f.name == "_vptr") continue;
            if (!f.type || f.type->isReference()) continue;
            if (needsDefaultConstruction(f.type)) {
                Instruction gfp;
                gfp.opcode = Opcode::GetFieldPtr;
                gfp.result = nextValueId();
                gfp.op1    = thisParamVal;
                gfp.intImm = static_cast<int64_t>(i);
                gfp.type   = checker_.context().getReference(f.type);
                gfp.strImm = recTy->structName;
                emit(gfp);

                constructDefaultObject(gfp.result, f.type);
            }
        }
    }

    // 4. Lower user constructor body
    if (c.body) {
        lowerBlock(*c.body);
    }

    if (currentBlock_ && !isCurrentBlockTerminated()) {
        Instruction retInst;
        retInst.opcode = Opcode::Return;
        retInst.op1 = InvalidValue;
        emit(retInst);
    }

    popScope();
    currentThisValue_ = InvalidValue;
    currentRecord_ = nullptr;
    currentFunction_ = nullptr;
}

void IRBuilder::lowerDestructor(StructDecl& sd, DestructorDecl* d) {
    varDropFlags_.clear();
    auto recTy = checker_.context().findRecord(sd.name);
    auto voidTy = checker_.context().getVoid();
    std::string fnName = sd.name + "_destructor";

    auto irFn = currentModule_->createFunction(fnName, voidTy);
    irFn->isLinkOnceODR = (fnName.find("__") != std::string::npos);
    currentFunction_ = irFn;
    currentRecord_ = recTy;
    isCurrentMethodConst_ = false;

    pushScope();

    auto entryBlock = irFn->createBlock("entry");
    setInsertBlock(entryBlock);

    // Param 0 is 'this'
    auto thisTy = checker_.context().getReference(recTy, false);
    ValueId thisParamVal = nextValueId();
    irFn->params.push_back(IRParam{
        .name = "this",
        .type = thisTy,
        .valueId = thisParamVal,
    });

    Instruction thisAlloca;
    thisAlloca.opcode = Opcode::Alloca;
    thisAlloca.result = nextValueId();
    thisAlloca.type = thisTy;
    emit(thisAlloca);

    Instruction thisStore;
    thisStore.opcode = Opcode::Store;
    thisStore.op1 = thisAlloca.result;
    thisStore.op2 = thisParamVal;
    emit(thisStore);

    currentThisValue_ = thisParamVal;

    // 1. Lower user destructor body, if present
    if (d && d->body) {
        lowerBlock(*d->body);
    }

    // 2. Destroy resource-owning members in reverse declaration order
    if (recTy) {
        size_t baseFieldCount = recTy->baseClass ? recTy->baseClass->structFields.size() : 0;
        for (size_t i = recTy->structFields.size(); i > baseFieldCount; --i) {
            size_t idx = i - 1;
            const auto& f = recTy->structFields[idx];
            if (f.name == "_vptr") continue;
            if (f.type && !f.type->isReference() && f.type->hasNonTrivialDestructor()) {
                Instruction gfp;
                gfp.opcode = Opcode::GetFieldPtr;
                gfp.result = nextValueId();
                gfp.op1 = thisParamVal;
                gfp.intImm = static_cast<int64_t>(idx);
                gfp.type = checker_.context().getReference(f.type);
                gfp.strImm = recTy->structName;
                emit(gfp);

                if (f.type->isArray()) {
                    auto arrElemTy = f.type->elementType;
                    if (arrElemTy && arrElemTy->hasNonTrivialDestructor()) {
                        for (int64_t k = f.type->arraySize - 1; k >= 0; --k) {
                            Instruction idxConst;
                            idxConst.opcode = Opcode::IConst;
                            idxConst.result = nextValueId();
                            idxConst.type   = checker_.context().getUsize();
                            idxConst.intImm = k;
                            emit(idxConst);

                            Instruction gep;
                            gep.opcode = Opcode::GetArrayElemPtr;
                            gep.result = nextValueId();
                            gep.op1    = gfp.result;
                            gep.op2    = idxConst.result;
                            gep.type   = arrElemTy;
                            emit(gep);

                            Instruction callMember;
                            callMember.opcode = Opcode::Call;
                            callMember.type   = voidTy;
                            callMember.result = InvalidValue;
                            callMember.strImm = arrElemTy->structName + "_destructor";
                            callMember.callArgs = { gep.result };
                            emit(callMember);
                        }
                    }
                } else {
                    Instruction callMember;
                    callMember.opcode = Opcode::Call;
                    callMember.type   = voidTy;
                    callMember.result = InvalidValue;
                    callMember.strImm = f.type->structName + "_destructor";
                    callMember.callArgs = { gfp.result };
                    emit(callMember);
                }
            }
        }
    }

    // 3. If base class has a non-trivial destructor, call it!
    if (recTy && recTy->baseClass && recTy->baseClass->hasNonTrivialDestructor()) {
        Instruction callBase;
        callBase.opcode = Opcode::Call;
        callBase.type   = voidTy;
        callBase.result = InvalidValue;
        callBase.strImm = recTy->baseClass->structName + "_destructor";
        callBase.callArgs = { thisParamVal };
        emit(callBase);
    }

    if (currentBlock_ && !isCurrentBlockTerminated()) {
        Instruction retInst;
        retInst.opcode = Opcode::Return;
        retInst.op1 = InvalidValue;
        emit(retInst);
    }

    popScope();
    currentThisValue_ = InvalidValue;
    currentRecord_ = nullptr;
    currentFunction_ = nullptr;
}

void IRBuilder::ensureDestructor(const sema::Type* ty) {
    if (!ty || !ty->hasNonTrivialDestructor()) return;
    if (ty->isEnum()) {
        lowerEnumDestructor(ty);
    }
}

void IRBuilder::lowerEnumDestructor(const sema::Type* eTy) {
    if (!eTy || !eTy->isEnum() || !eTy->hasNonTrivialDestructor() || !currentModule_) return;
    std::string fnName = eTy->destructorName();
    if (emittedEnumDestructors_.count(fnName)) return;
    emittedEnumDestructors_.insert(fnName);

    auto voidTy = checker_.context().getVoid();
    auto irFn = currentModule_->createFunction(fnName, voidTy);
    irFn->isLinkOnceODR = true;

    auto prevFn = currentFunction_;
    auto prevBlock = currentBlock_;
    auto prevThis = currentThisValue_;
    auto prevRec = currentRecord_;
    bool prevConst = isCurrentMethodConst_;

    currentFunction_ = irFn;
    currentRecord_ = nullptr;
    isCurrentMethodConst_ = false;

    pushScope();

    auto entryBlock = irFn->createBlock("entry");
    auto exitBlock = irFn->createBlock("exit");
    setInsertBlock(entryBlock);

    auto thisTy = checker_.context().getReference(eTy, false);
    ValueId thisParamVal = nextValueId();
    irFn->params.push_back(IRParam{
        .name = "this",
        .type = thisTy,
        .valueId = thisParamVal,
    });

    Instruction thisAlloca;
    thisAlloca.opcode = Opcode::Alloca;
    thisAlloca.result = nextValueId();
    thisAlloca.type = thisTy;
    emit(thisAlloca);

    Instruction thisStore;
    thisStore.opcode = Opcode::Store;
    thisStore.op1 = thisAlloca.result;
    thisStore.op2 = thisParamVal;
    emit(thisStore);

    currentThisValue_ = thisParamVal;

    // Read tag
    Instruction tagInst;
    tagInst.opcode = Opcode::TagGet;
    tagInst.result = nextValueId();
    tagInst.op1    = thisParamVal;
    tagInst.type   = checker_.context().getI32();
    emit(tagInst);
    ValueId tagVal = tagInst.result;

    std::vector<std::pair<int64_t, std::string>> switchCases;
    std::vector<std::pair<BasicBlock*, size_t>> variantBlocks;

    for (size_t i = 0; i < eTy->enumVariants.size(); ++i) {
        const auto& v = eTy->enumVariants[i];
        bool hasNonTrivialPayload = false;
        for (const auto* pTy : v.payloadTypes) {
            if (pTy && pTy->hasNonTrivialDestructor()) {
                hasNonTrivialPayload = true;
                break;
            }
        }
        if (hasNonTrivialPayload) {
            auto vb = irFn->createBlock("case_variant_" + std::to_string(i));
            switchCases.push_back({ static_cast<int64_t>(i), vb->label });
            variantBlocks.push_back({ vb, i });
        }
    }

    if (switchCases.empty()) {
        Instruction ret;
        ret.opcode = Opcode::Return;
        ret.op1 = InvalidValue;
        emit(ret);

        popScope();
        currentFunction_ = prevFn;
        currentBlock_ = prevBlock;
        currentThisValue_ = prevThis;
        currentRecord_ = prevRec;
        isCurrentMethodConst_ = prevConst;
        return;
    }

    Instruction sw;
    sw.opcode = Opcode::Switch;
    sw.op1 = tagVal;
    sw.label1 = exitBlock->label;
    sw.switchCases = std::move(switchCases);
    emit(sw);

    for (const auto& [vb, varIdx] : variantBlocks) {
        setInsertBlock(vb);
        const auto& v = eTy->enumVariants[varIdx];
        int64_t currentOffset = 0;
        for (size_t k = 0; k < v.payloadTypes.size(); ++k) {
            const auto* pTy = v.payloadTypes[k];
            if (!pTy) continue;
            size_t a = pTy->alignment();
            if (a == 0) a = 1;
            currentOffset = (currentOffset + a - 1) & ~(a - 1);

            if (pTy->hasNonTrivialDestructor()) {
                ensureDestructor(pTy);

                Instruction gfp;
                gfp.opcode = Opcode::VariantPayloadPtr;
                gfp.result = nextValueId();
                gfp.op1    = thisParamVal;
                gfp.intImm = static_cast<int64_t>(k);
                gfp.intImm2 = currentOffset;
                gfp.type   = eTy;
                emit(gfp);

                Instruction callMember;
                callMember.opcode = Opcode::Call;
                callMember.type   = voidTy;
                callMember.result = InvalidValue;
                callMember.strImm = pTy->destructorName();
                callMember.callArgs = { gfp.result };
                emit(callMember);
            }

            currentOffset += pTy->size();
        }

        Instruction jmp;
        jmp.opcode = Opcode::Jump;
        jmp.label1 = exitBlock->label;
        emit(jmp);
    }

    setInsertBlock(exitBlock);
    Instruction ret;
    ret.opcode = Opcode::Return;
    ret.op1 = InvalidValue;
    emit(ret);

    popScope();
    currentFunction_ = prevFn;
    currentBlock_ = prevBlock;
    currentThisValue_ = prevThis;
    currentRecord_ = prevRec;
    isCurrentMethodConst_ = prevConst;
}

void IRBuilder::markVariableMoved(const std::string& name) {
    if (name.empty()) return;
    auto it = varDropFlags_.find(name);
    if (it != varDropFlags_.end() && it->second != InvalidValue && currentBlock_ && !isCurrentBlockTerminated()) {
        Instruction iconst0;
        iconst0.opcode = Opcode::IConst;
        iconst0.result = nextValueId();
        iconst0.type   = checker_.context().getBool();
        iconst0.intImm = 0;
        emit(iconst0);

        Instruction dfStore;
        dfStore.opcode = Opcode::Store;
        dfStore.op1    = it->second;
        dfStore.op2    = iconst0.result;
        emit(dfStore);
    }
    for (auto& scope : cleanupScopes_) {
        for (auto& item : scope) {
            if (item.varName == name) {
                item.isMoved = true;
            }
        }
    }
}

void IRBuilder::markVariableReassigned(const std::string& name, ValueId ptr, const sema::Type* ty) {
    if (!ty || !ty->hasNonTrivialDestructor() || !currentBlock_ || isCurrentBlockTerminated()) return;
    ensureDestructor(ty);
    auto it = varDropFlags_.find(name);
    if (it != varDropFlags_.end() && it->second != InvalidValue) {
        Instruction loadFlag;
        loadFlag.opcode = Opcode::Load;
        loadFlag.result = nextValueId();
        loadFlag.op1    = it->second;
        loadFlag.type   = checker_.context().getBool();
        emit(loadFlag);

        auto dropBlock = currentFunction_->createBlock(nextLabel("reassign_drop"));
        auto skipBlock = currentFunction_->createBlock(nextLabel("reassign_skip"));

        Instruction br;
        br.opcode = Opcode::Branch;
        br.op1 = loadFlag.result;
        br.label1 = dropBlock->label;
        br.label2 = skipBlock->label;
        emit(br);

        setInsertBlock(dropBlock);
        Instruction call;
        call.opcode = Opcode::Call;
        call.type   = checker_.context().getVoid();
        call.result = InvalidValue;
        call.strImm = ty->destructorName();
        call.callArgs = { ptr };
        emit(call);

        Instruction jmp;
        jmp.opcode = Opcode::Jump;
        jmp.label1 = skipBlock->label;
        emit(jmp);

        setInsertBlock(skipBlock);

        Instruction iconst1;
        iconst1.opcode = Opcode::IConst;
        iconst1.result = nextValueId();
        iconst1.type   = checker_.context().getBool();
        iconst1.intImm = 1;
        emit(iconst1);

        Instruction dfStore;
        dfStore.opcode = Opcode::Store;
        dfStore.op1    = it->second;
        dfStore.op2    = iconst1.result;
        emit(dfStore);
    } else {
        Instruction call;
        call.opcode = Opcode::Call;
        call.type   = checker_.context().getVoid();
        call.result = InvalidValue;
        call.strImm = ty->destructorName();
        call.callArgs = { ptr };
        emit(call);
    }
    for (auto& scope : cleanupScopes_) {
        for (auto& item : scope) {
            if (item.varName == name) {
                item.isMoved = false;
            }
        }
    }
}

// ── Function Lowering ───────────────────────────────────────────────────────

void IRBuilder::lowerFunction(FunctionDecl& fn) {
    varDropFlags_.clear();
    auto retTy = checker_.resolveType(*fn.returnType);
    auto irFn = currentModule_->createFunction(fn.name, retTy);
    irFn->isNoInline = fn.isNoInline;
    irFn->isAlwaysInline = fn.isAlwaysInline;
    irFn->isPublic = fn.isPublic;
    irFn->isLinkOnceODR = (fn.name.find("__") != std::string::npos);
    irFn->isExtern = fn.isExtern;
    irFn->linkage = fn.linkage;
    irFn->isExported = fn.isExported;

    if (!fn.body) {
        for (auto& p : fn.params) {
            auto elemTy = checker_.resolveType(*p.type);
            const sema::Type* paramTy = elemTy;
            if (p.isArray) {
                paramTy = checker_.context().getArrayView(elemTy);
            }
            irFn->params.push_back(IRParam{
                .name    = p.name,
                .type    = paramTy,
                .valueId = InvalidValue,
            });
        }
        return;
    }

    if (fn.isAsync) {
        // Outline worker function: void __tamarin_async_worker_<name>(ptr %ctx, ptr %result)
        std::string workerName = "__tamarin_async_worker_" + fn.name;
        auto voidTy = checker_.context().getVoid();
        auto workerFn = currentModule_->createFunction(workerName, voidTy);
        workerFn->isLinkOnceODR = true;

        std::vector<const sema::Type*> paramTypes;
        for (auto& p : fn.params) {
            auto elemTy = checker_.resolveType(*p.type);
            const sema::Type* pTy = elemTy;
            if (p.isArray) pTy = checker_.context().getArrayView(elemTy);
            paramTypes.push_back(pTy);
        }
        const sema::Type* ctxTupleTy = checker_.context().getTuple(paramTypes);

        ValueId ctxVal = nextValueId();
        ValueId resVal = nextValueId();
        const sema::Type* ctxPtrTy = checker_.context().getReference(ctxTupleTy, false);
        const sema::Type* resPtrTy = retTy->isVoid()
            ? checker_.context().getReference(checker_.context().getI8(), false)
            : checker_.context().getReference(retTy, false);
        workerFn->params.push_back(IRParam{"__ctx", ctxPtrTy, ctxVal});
        workerFn->params.push_back(IRParam{"__result", resPtrTy, resVal});

        currentFunction_ = workerFn;
        pushScope();
        auto workerEntry = workerFn->createBlock("entry");
        setInsertBlock(workerEntry);

        // Unpack parameters from ctx tuple
        for (size_t i = 0; i < fn.params.size(); ++i) {
            auto& p = fn.params[i];
            const sema::Type* pTy = paramTypes[i];

            Instruction gfp;
            gfp.opcode = Opcode::GetFieldPtr;
            gfp.result = nextValueId();
            gfp.op1 = ctxVal;
            gfp.intImm = static_cast<int64_t>(i);
            gfp.type = pTy;
            emit(gfp);

            Instruction loadInst;
            loadInst.opcode = Opcode::Load;
            loadInst.result = nextValueId();
            loadInst.op1 = gfp.result;
            loadInst.type = pTy;
            emit(loadInst);

            Instruction allocaInst;
            allocaInst.opcode = Opcode::Alloca;
            allocaInst.result = nextValueId();
            allocaInst.type = pTy;
            emit(allocaInst);

            Instruction storeInst;
            storeInst.opcode = Opcode::Store;
            storeInst.op1 = allocaInst.result;
            storeInst.op2 = loadInst.result;
            emit(storeInst);

            Binding b;
            b.ptr = allocaInst.result;
            b.type = pTy;
            b.isConst = p.isConst;
            b.isParameter = true;

            if (p.isArray) {
                Instruction getLenInst;
                getLenInst.opcode = Opcode::GetLen;
                getLenInst.result = nextValueId();
                getLenInst.op1 = loadInst.result;
                getLenInst.type = checker_.context().getUsize();
                emit(getLenInst);
                b.arrayLenValueId = getLenInst.result;
            }

            setBinding(p.name, std::move(b));
        }

        bool oldAsync = isCurrentFunctionAsync_;
        ValueId oldResPtr = currentAsyncResultPtr_;
        const sema::Type* oldRetTy = currentAsyncUnderlyingRetTy_;

        isCurrentFunctionAsync_ = true;
        currentAsyncResultPtr_ = resVal;
        currentAsyncUnderlyingRetTy_ = retTy;

        lowerBlock(*fn.body);

        if (currentBlock_ && !isCurrentBlockTerminated()) {
            emitCleanupsUpTo(0);
            Instruction retInst;
            retInst.opcode = Opcode::Return;
            retInst.op1 = InvalidValue;
            emit(retInst);
        }

        popScope();

        isCurrentFunctionAsync_ = oldAsync;
        currentAsyncResultPtr_ = oldResPtr;
        currentAsyncUnderlyingRetTy_ = oldRetTy;

        // Now lower the main dispatcher function irFn (e.g. fn.name) which returns Task<retTy>
        currentFunction_ = irFn;
        irFn->returnType = checker_.context().getTask(retTy);
        pushScope();
        auto dispEntry = irFn->createBlock("entry");
        setInsertBlock(dispEntry);

        std::vector<ValueId> dispParamVals;
        for (size_t i = 0; i < fn.params.size(); ++i) {
            auto& p = fn.params[i];
            const sema::Type* pTy = paramTypes[i];
            ValueId pVal = nextValueId();
            irFn->params.push_back(IRParam{
                .name = p.name,
                .type = pTy,
                .valueId = pVal,
            });
            dispParamVals.push_back(pVal);
        }

        ValueId ctxAllocaVal = InvalidValue;
        int64_t ctxSize = 0;
        if (!fn.params.empty()) {
            ctxSize = static_cast<int64_t>(ctxTupleTy->size());
            Instruction allocaInst;
            allocaInst.opcode = Opcode::Alloca;
            allocaInst.result = nextValueId();
            allocaInst.type = ctxTupleTy;
            emit(allocaInst);
            ctxAllocaVal = allocaInst.result;

            for (size_t i = 0; i < fn.params.size(); ++i) {
                Instruction gfp;
                gfp.opcode = Opcode::GetFieldPtr;
                gfp.result = nextValueId();
                gfp.op1 = ctxAllocaVal;
                gfp.intImm = static_cast<int64_t>(i);
                gfp.type = paramTypes[i];
                emit(gfp);

                Instruction storeInst;
                storeInst.opcode = Opcode::Store;
                storeInst.op1 = gfp.result;
                storeInst.op2 = dispParamVals[i];
                emit(storeInst);
            }
        }

        int64_t resSize = retTy->isVoid() ? 0 : static_cast<int64_t>(retTy->size());

        Instruction ctxSzConst;
        ctxSzConst.opcode = Opcode::IConst;
        ctxSzConst.result = nextValueId();
        ctxSzConst.intImm = ctxSize;
        ctxSzConst.type = checker_.context().getI64();
        emit(ctxSzConst);

        Instruction resSzConst;
        resSzConst.opcode = Opcode::IConst;
        resSzConst.result = nextValueId();
        resSzConst.intImm = resSize;
        resSzConst.type = checker_.context().getI64();
        emit(resSzConst);

        Instruction createInst;
        createInst.opcode = Opcode::AsyncTaskCreate;
        createInst.result = nextValueId();
        createInst.op1 = ctxAllocaVal;
        createInst.op2 = ctxSzConst.result;
        createInst.op3 = resSzConst.result;
        createInst.strImm = workerName;
        createInst.type = irFn->returnType;
        emit(createInst);

        Instruction retInst;
        retInst.opcode = Opcode::Return;
        retInst.op1 = createInst.result;
        emit(retInst);

        popScope();
        currentFunction_ = nullptr;
        currentBlock_ = nullptr;
        return;
    }

    currentFunction_ = irFn;

    pushScope();

    auto entryBlock = irFn->createBlock("entry");
    setInsertBlock(entryBlock);

    // Set up parameters
    for (auto& p : fn.params) {
        auto elemTy = checker_.resolveType(*p.type);
        const sema::Type* paramTy = elemTy;

        if (p.isArray) {
            paramTy = checker_.context().getArrayView(elemTy);
        }

        ValueId paramVal = nextValueId();
        irFn->params.push_back(IRParam{
            .name    = p.name,
            .type    = paramTy,
            .valueId = paramVal,
        });

        // Allocate local variable on stack to hold parameter
        Instruction allocaInst;
        allocaInst.opcode = Opcode::Alloca;
        allocaInst.result = nextValueId();
        allocaInst.type   = paramTy;
        emit(allocaInst);

        // Store parameter value into local alloca
        Instruction storeInst;
        storeInst.opcode = Opcode::Store;
        storeInst.op1    = allocaInst.result; // ptr
        storeInst.op2    = paramVal;          // val
        emit(storeInst);

        Binding b;
        b.ptr = allocaInst.result;
        b.type = paramTy;
        b.isConst = p.isConst;
        b.isParameter = true;

        // If parameter is an array view, also record its length
        if (p.isArray) {
            Instruction getLenInst;
            getLenInst.opcode = Opcode::GetLen;
            getLenInst.result = nextValueId();
            getLenInst.op1    = paramVal;
            getLenInst.type   = checker_.context().getUsize();
            emit(getLenInst);
            b.arrayLenValueId = getLenInst.result;
        }

        setBinding(p.name, std::move(b));
    }

    if (fn.body) {
        lowerBlock(*fn.body);
    }

    // Ensure entry/exit block ends with return for void functions.
    // Never insert arbitrary zero-valued returns to conceal missing return paths.
    if (currentBlock_ && !isCurrentBlockTerminated()) {
        if (retTy->isVoid()) {
            Instruction retInst;
            retInst.opcode = Opcode::Return;
            retInst.op1    = InvalidValue;
            emit(retInst);
        }
    }

    popScope();
    currentFunction_ = nullptr;
}

// ── Statement Lowering ──────────────────────────────────────────────────────

void IRBuilder::lowerStmt(Stmt& stmt) {
    if (!currentBlock_ || isCurrentBlockTerminated()) {
        return;
    }

    switch (stmt.kind) {
        case StmtKind::Block:    lowerBlock(static_cast<BlockStmt&>(stmt)); break;
        case StmtKind::VarDecl:  lowerVarDecl(static_cast<VarDeclStmt&>(stmt)); break;
        case StmtKind::DestructureVarDecl: lowerDestructureVarDecl(static_cast<DestructureVarDeclStmt&>(stmt)); break;
        case StmtKind::Expr: {
            auto& e = static_cast<ExprStmt&>(stmt);
            if (e.expr) lowerExpr(*e.expr);
            break;
        }
        case StmtKind::Return:   lowerReturn(static_cast<ReturnStmt&>(stmt)); break;
        case StmtKind::If:       lowerIf(static_cast<IfStmt&>(stmt)); break;
        case StmtKind::While:    lowerWhile(static_cast<WhileStmt&>(stmt)); break;
        case StmtKind::For:      lowerFor(static_cast<ForStmt&>(stmt)); break;
        case StmtKind::Break: {
            if (!loopStack_.empty() && currentBlock_ && !isCurrentBlockTerminated()) {
                emitCleanupsUpTo(loopStack_.back().scopeDepth);
                Instruction jmp;
                jmp.opcode = Opcode::Jump;
                jmp.label1 = loopStack_.back().breakLabel;
                emit(jmp);
                currentBlock_ = nullptr;
            }
            break;
        }
        case StmtKind::Continue: {
            if (!loopStack_.empty() && currentBlock_ && !isCurrentBlockTerminated()) {
                emitCleanupsUpTo(loopStack_.back().scopeDepth);
                Instruction jmp;
                jmp.opcode = Opcode::Jump;
                jmp.label1 = loopStack_.back().continueLabel;
                emit(jmp);
                currentBlock_ = nullptr;
            }
            break;
        }
        case StmtKind::Switch:
            break;
        case StmtKind::Region:
            lowerRegion(static_cast<RegionStmt&>(stmt));
            break;
        case StmtKind::Match:
            lowerMatchStmt(static_cast<MatchStmt&>(stmt));
            break;
        case StmtKind::ParallelFor:
            lowerParallelFor(static_cast<ParallelForStmt&>(stmt));
            break;
        case StmtKind::TaskScope:
            lowerTaskScope(static_cast<TaskScopeStmt&>(stmt));
            break;
        case StmtKind::Spawn:
            lowerSpawn(static_cast<SpawnStmt&>(stmt));
            break;
        case StmtKind::Select:
            lowerSelect(static_cast<SelectStmt&>(stmt));
            break;
    }
}

void IRBuilder::lowerBlock(BlockStmt& block) {
    pushScope();
    for (auto& s : block.stmts) {
        if (!currentBlock_ || isCurrentBlockTerminated()) {
            break; // Remaining statements are unreachable dead code
        }
        lowerStmt(*s);
    }
    popScope();
}

void IRBuilder::lowerRegion(RegionStmt& stmt) {
    pushScope();

    // 1. Allocate region control block on stack
    Instruction allocaInst;
    allocaInst.opcode = Opcode::Alloca;
    allocaInst.result = nextValueId();
    allocaInst.strImm = "__tamarin_region";
    emit(allocaInst);

    // 2. Evaluate capacity (default 65536)
    int64_t capacity = 65536;
    if (stmt.capacityExpr) {
        auto eval = checker_.evalIntConstExpr(*stmt.capacityExpr);
        if (eval.isConst && !eval.overflow && eval.value > 0) {
            capacity = eval.value;
        }
    }

    // 3. Emit RegionInit
    Instruction initInst;
    initInst.opcode = Opcode::RegionInit;
    initInst.op1    = allocaInst.result;
    initInst.intImm = capacity;
    emit(initInst);

    // 4. Register cleanup item
    cleanupScopes_.back().push_back(CleanupItem{
        .kind = CleanupKind::RegionDestroy,
        .ptr  = allocaInst.result
    });

    // 5. Track active region slot
    activeRegionSlots_.push_back(allocaInst.result);

    // 6. Lower body statements
    if (stmt.body) {
        for (auto& s : stmt.body->stmts) {
            if (!currentBlock_ || isCurrentBlockTerminated()) break;
            lowerStmt(*s);
        }
    }

    // 7. Pop active region slot
    activeRegionSlots_.pop_back();

    // 8. Pop scope (this automatically emits RegionDestroy via emitCleanupsUpTo)
    popScope();
}

void IRBuilder::lowerMatchStmt(MatchStmt& stmt) {
    if (!currentBlock_ || isCurrentBlockTerminated() || !stmt.expr) {
        return;
    }

    auto targetTy = checker_.getExprType(stmt.expr.get());
    if (targetTy && targetTy->isReference()) {
        targetTy = targetTy->elementType;
    }
    if (!targetTy) return;

    ValueId targetVal = lowerExpr(*stmt.expr);
    if (stmt.expr->kind == ast::ExprKind::Identifier) {
        const auto& id = static_cast<const ast::IdentifierExpr&>(*stmt.expr);
        markVariableMoved(id.name);
    }

    ValueId tagVal = InvalidValue;
    ValueId enumSlotPtr = InvalidValue;

    if (targetTy->isEnum()) {
        if (targetTy->isNicheOptimized) {
            Instruction tagInst;
            tagInst.opcode = Opcode::TagGet;
            tagInst.result = nextValueId();
            tagInst.op1    = targetVal;
            tagInst.type   = checker_.context().getI32();
            emit(tagInst);
            tagVal = tagInst.result;
        } else {
            if (stmt.expr->kind == ast::ExprKind::Identifier) {
                const auto& id = static_cast<const ast::IdentifierExpr&>(*stmt.expr);
                auto* b = lookupBinding(id.name);
                if (b && b->ptr != InvalidValue) {
                    enumSlotPtr = b->ptr;
                }
            }
            if (enumSlotPtr == InvalidValue) {
                Instruction allocaInst;
                allocaInst.opcode = Opcode::Alloca;
                allocaInst.result = nextValueId();
                allocaInst.type   = targetTy;
                emit(allocaInst);
                enumSlotPtr = allocaInst.result;

                Instruction store;
                store.opcode = Opcode::Store;
                store.op1    = enumSlotPtr;
                store.op2    = targetVal;
                emit(store);
            }

            Instruction tagInst;
            tagInst.opcode = Opcode::TagGet;
            tagInst.result = nextValueId();
            tagInst.op1    = enumSlotPtr;
            tagInst.type   = checker_.context().getI32();
            emit(tagInst);
            tagVal = tagInst.result;
        }
    } else {
        // Integer match
        tagVal = targetVal;
    }

    auto mergeBlock = currentFunction_->createBlock(nextLabel("match_merge"));
    BasicBlock* defaultBlock = nullptr;
    std::vector<std::pair<int64_t, std::string>> switchCases;
    std::vector<BasicBlock*> caseBlocks;

    for (size_t i = 0; i < stmt.cases.size(); ++i) {
        auto cb = currentFunction_->createBlock(nextLabel("match_case"));
        caseBlocks.push_back(cb);
        const auto& pat = stmt.cases[i].pattern;

        if (pat.kind == MatchPatternKind::Wildcard || pat.kind == MatchPatternKind::Binding) {
            defaultBlock = cb;
        } else if (pat.kind == MatchPatternKind::Variant) {
            int vIdx = targetTy->isEnum() ? targetTy->getVariantIndex(pat.variantName) : -1;
            if (vIdx >= 0) {
                switchCases.push_back({ static_cast<int64_t>(vIdx), cb->label });
            }
        } else if (pat.kind == MatchPatternKind::Literal && pat.literalExpr) {
            if (pat.literalExpr->kind == ExprKind::IntLiteral) {
                auto& lit = static_cast<IntLiteralExpr&>(*pat.literalExpr);
                switchCases.push_back({ lit.value, cb->label });
            }
        }
    }

    bool synthesizedDefault = false;
    if (!defaultBlock) {
        defaultBlock = currentFunction_->createBlock(nextLabel("match_default"));
        synthesizedDefault = true;
    }

    Instruction sw;
    sw.opcode = Opcode::Switch;
    sw.op1 = tagVal;
    sw.label1 = defaultBlock->label;
    sw.switchCases = std::move(switchCases);
    emit(sw);

    bool anyCaseFallsThrough = false;

    for (size_t i = 0; i < stmt.cases.size(); ++i) {
        setInsertBlock(caseBlocks[i]);
        pushScope();

        const auto& pat = stmt.cases[i].pattern;
        if (pat.kind == MatchPatternKind::Binding && !pat.bindings.empty()) {
            const std::string& bName = pat.bindings[0];
            if (!bName.empty()) {
                Instruction bindSlot;
                bindSlot.opcode = Opcode::Alloca;
                bindSlot.result = nextValueId();
                bindSlot.type   = targetTy;
                emit(bindSlot);

                Instruction bindStore;
                bindStore.opcode = Opcode::Store;
                bindStore.op1    = bindSlot.result;
                bindStore.op2    = targetVal;
                emit(bindStore);

                setBinding(bName, Binding{ bindSlot.result, targetTy });
            }
        } else if (pat.kind == MatchPatternKind::Variant && targetTy->isEnum()) {
            int vIdx = targetTy->getVariantIndex(pat.variantName);
            if (vIdx >= 0 && !pat.bindings.empty()) {
                const auto& variant = targetTy->enumVariants[vIdx];
                for (size_t k = 0; k < pat.bindings.size(); ++k) {
                    const std::string& bName = pat.bindings[k];
                    const sema::Type* pTy = (k < variant.payloadTypes.size()) ? variant.payloadTypes[k] : nullptr;
                    ValueId extractedVal = InvalidValue;

                    if (targetTy->isNicheOptimized) {
                        extractedVal = targetVal;
                    } else {
                        int64_t currentOffset = 0;
                        for (size_t prev = 0; prev < k; ++prev) {
                            const auto* prevTy = variant.payloadTypes[prev];
                            size_t a = prevTy ? prevTy->alignment() : 1;
                            if (a == 0) a = 1;
                            currentOffset = (currentOffset + a - 1) & ~(a - 1);
                            if (prevTy) currentOffset += prevTy->size();
                        }
                        if (pTy) {
                            size_t a = pTy->alignment();
                            if (a == 0) a = 1;
                            currentOffset = (currentOffset + a - 1) & ~(a - 1);
                        }

                        Instruction gfp;
                        gfp.opcode = Opcode::VariantPayloadPtr;
                        gfp.result = nextValueId();
                        gfp.op1    = enumSlotPtr;
                        gfp.intImm = static_cast<int64_t>(k);
                        gfp.intImm2 = currentOffset;
                        gfp.type   = targetTy;
                        emit(gfp);

                        Instruction loadPayload;
                        loadPayload.opcode = Opcode::Load;
                        loadPayload.result = nextValueId();
                        loadPayload.op1    = gfp.result;
                        loadPayload.type   = pTy ? pTy : checker_.context().getI32();
                        emit(loadPayload);
                        extractedVal = loadPayload.result;
                    }

                    if (!bName.empty() && bName != "_") {
                        Instruction subSlot;
                        subSlot.opcode = Opcode::Alloca;
                        subSlot.result = nextValueId();
                        subSlot.type   = pTy;
                        emit(subSlot);

                        Instruction storeSub;
                        storeSub.opcode = Opcode::Store;
                        storeSub.op1    = subSlot.result;
                        storeSub.op2    = extractedVal;
                        emit(storeSub);

                        setBinding(bName, Binding{ subSlot.result, pTy });

                        if (pTy && pTy->hasNonTrivialDestructor() && !cleanupScopes_.empty()) {
                            ensureDestructor(pTy);
                            cleanupScopes_.back().push_back(CleanupItem{
                                .kind = CleanupKind::Destructor,
                                .ptr = subSlot.result,
                                .type = pTy,
                                .varName = bName,
                            });
                        }
                    }
                }
            }
        }

        if (stmt.cases[i].body) {
            lowerStmt(*stmt.cases[i].body);
        }

        popScope();

        if (currentBlock_ && !isCurrentBlockTerminated()) {
            anyCaseFallsThrough = true;
            Instruction jmp;
            jmp.opcode = Opcode::Jump;
            jmp.label1 = mergeBlock->label;
            emit(jmp);
        }
    }

    if (synthesizedDefault) {
        setInsertBlock(defaultBlock);
        if (anyCaseFallsThrough) {
            Instruction jmp;
            jmp.opcode = Opcode::Jump;
            jmp.label1 = mergeBlock->label;
            emit(jmp);
        } else {
            if (currentFunction_->returnType && !currentFunction_->returnType->isVoid()) {
                Instruction ret;
                ret.opcode = Opcode::Return;
                ret.op1    = emitIntConstant(0, currentFunction_->returnType);
                emit(ret);
            } else {
                Instruction ret;
                ret.opcode = Opcode::Return;
                ret.op1    = InvalidValue;
                emit(ret);
            }
        }
    }

    if (!anyCaseFallsThrough) {
        if (!currentFunction_->blocks.empty() && currentFunction_->blocks.back().get() == mergeBlock) {
            currentFunction_->blocks.pop_back();
        }
        currentBlock_ = nullptr;
    } else {
        setInsertBlock(mergeBlock);
    }
}

void IRBuilder::lowerVarDecl(VarDeclStmt& decl) {
    auto elemTy = checker_.resolveType(*decl.type);
    const sema::Type* varTy = elemTy;
    if (!varTy) {
        varTy = checker_.getVarDeclType(&decl);
        if (varTy) {
            elemTy = varTy->isReference() ? varTy->elementType : varTy;
        }
    }

    if (decl.isArray) {
        if (decl.initializer && decl.initializer->kind != ExprKind::ArrayInit) {
            auto valId = lowerExpr(*decl.initializer);
            auto initTy = checker_.getExprType(decl.initializer.get());
            if (initTy && initTy->isArrayView()) {
                Instruction allocaInst;
                allocaInst.opcode = Opcode::Alloca;
                allocaInst.result = nextValueId();
                allocaInst.type   = initTy;
                emit(allocaInst);

                Instruction storeInst;
                storeInst.opcode = Opcode::Store;
                storeInst.op1    = allocaInst.result;
                storeInst.op2    = valId;
                emit(storeInst);

                Instruction getLenInst;
                getLenInst.opcode = Opcode::GetLen;
                getLenInst.result = nextValueId();
                getLenInst.op1    = valId;
                getLenInst.type   = checker_.context().getUsize();
                emit(getLenInst);

                Binding b;
                b.ptr = allocaInst.result;
                b.type = initTy;
                b.arrayLenValueId = getLenInst.result;
                b.isConst = decl.isConst;
                setBinding(decl.name, std::move(b));
                return;
            }
        }

        int64_t size = decl.evaluatedArraySize;
        if (size < 0) {
            auto sym = checker_.symbols().lookup(decl.name);
            if (sym && sym->type && sym->type->isArray()) {
                size = sym->type->arraySize;
            }
        }
        if (size < 0 && decl.arraySize && decl.arraySize->kind == ExprKind::IntLiteral) {
            size = static_cast<IntLiteralExpr*>(decl.arraySize.get())->value;
        } else if (size < 0 && decl.initializer && decl.initializer->kind == ExprKind::ArrayInit) {
            size = static_cast<int64_t>(
                static_cast<ArrayInitExpr*>(decl.initializer.get())->elements.size());
        }
        if (size < 0) size = 0;
        varTy = checker_.context().getArray(elemTy, size);

        // Structure-of-Arrays (SoA) for @layout(scan) structs
        if (elemTy && elemTy->isRecord() && elemTy->layoutDirective == 2 /* Scan / SoA */) {
            if (!activeRegionSlots_.empty()) {
                // Region-backed SoA arrays are rejected in semantic analysis (v0.5.1 Option B).
                // Do not silently emit a stack alloca.
                return;
            }
            Instruction arrAlloc;
            arrAlloc.opcode = Opcode::SoAArrayAlloc;
            arrAlloc.result = nextValueId();
            arrAlloc.type   = varTy;
            arrAlloc.intImm = size;
            arrAlloc.strImm = elemTy->structName;
            emit(arrAlloc);

            Instruction lenConst;
            lenConst.opcode = Opcode::IConst;
            lenConst.result = nextValueId();
            lenConst.type   = checker_.context().getUsize();
            lenConst.intImm = size;
            emit(lenConst);

            Binding b;
            b.ptr = arrAlloc.result;
            b.type = varTy;
            b.arrayLenValueId = lenConst.result;
            b.constantArrayLen = size;
            b.isConst = decl.isConst;
            setBinding(decl.name, std::move(b));
            return;
        }

        ValueId arrayBasePtr = InvalidValue;
        bool isRegionAlloc = !activeRegionSlots_.empty();

        if (isRegionAlloc) {
            int64_t elemSize = elemTy ? elemTy->size() : 4;
            if (size < 0 || elemSize <= 0 || size > (INT64_MAX / elemSize)) {
                // Reject invalid or overflowing size requests
                return;
            }
            int64_t totalBytes = size * elemSize;
            Instruction regAlloc;
            regAlloc.opcode = Opcode::RegionAlloc;
            regAlloc.result = nextValueId();
            regAlloc.type   = varTy;
            regAlloc.op1    = activeRegionSlots_.back();
            regAlloc.intImm = totalBytes;
            regAlloc.strImm = "16";
            emit(regAlloc);
            arrayBasePtr = regAlloc.result;
        } else {
            // Allocate array with exact length
            Instruction arrAlloc;
            arrAlloc.opcode = Opcode::ArrayAlloc;
            arrAlloc.result = nextValueId();
            arrAlloc.type   = varTy;
            arrAlloc.intImm = size; // length
            emit(arrAlloc);
            arrayBasePtr = arrAlloc.result;
        }

        // Save length as a constant (usize / 64-bit size_t)
        Instruction lenConst;
        lenConst.opcode = Opcode::IConst;
        lenConst.result = nextValueId();
        lenConst.type   = checker_.context().getUsize();
        lenConst.intImm = size;
        emit(lenConst);

        Binding b;
        b.ptr = arrayBasePtr;
        b.type = varTy;
        b.arrayLenValueId = lenConst.result;
        b.constantArrayLen = size;
        b.isConst = decl.isConst;
        setBinding(decl.name, std::move(b));

        ValueId slotId = InvalidValue;
        if (isRegionAlloc && elemTy && elemTy->hasNonTrivialDestructor() && size > 0 && !activeRegionSlots_.empty()) {
            // Pre-reserve destructor slot BEFORE constructor execution
            Instruction reserveInst;
            reserveInst.opcode = Opcode::RegionReserveDestructor;
            reserveInst.result = nextValueId();
            reserveInst.type   = checker_.context().getReference(checker_.context().getI8());
            reserveInst.op1    = activeRegionSlots_.back();
            emit(reserveInst);
            slotId = reserveInst.result;
        }

        // Initialize elements if initializer present
        int64_t explicitCount = 0;
        if (decl.initializer && decl.initializer->kind == ExprKind::ArrayInit) {
            auto& aInit = static_cast<ArrayInitExpr&>(*decl.initializer);
            explicitCount = static_cast<int64_t>(aInit.elements.size());
            for (size_t i = 0; i < aInit.elements.size(); ++i) {
                Instruction idxConst;
                idxConst.opcode = Opcode::IConst;
                idxConst.result = nextValueId();
                idxConst.type   = checker_.context().getUsize();
                idxConst.intImm = static_cast<int64_t>(i);
                emit(idxConst);

                bool isCtorCall = (aInit.elements[i]->kind == ExprKind::Construct) ||
                    (aInit.elements[i]->kind == ExprKind::Call && elemTy->isRecord() &&
                     static_cast<CallExpr*>(aInit.elements[i].get())->callee == elemTy->structName);

                if (isCtorCall && elemTy->isRecord()) {
                    // In-place direct construction of element
                    Instruction gep;
                    gep.opcode = Opcode::GetArrayElemPtr;
                    gep.result = nextValueId();
                    gep.op1    = arrayBasePtr;
                    gep.op2    = idxConst.result;
                    gep.type   = elemTy;
                    emit(gep);

                    // Initialize _vptr if polymorphic
                    if (elemTy->isPolymorphic) {
                        Instruction gv;
                        gv.opcode = Opcode::GetVTablePtr;
                        gv.result = nextValueId();
                        gv.strImm = elemTy->structName;
                        gv.type   = checker_.context().getReference(checker_.context().getI8());
                        emit(gv);

                        Instruction gfp;
                        gfp.opcode = Opcode::GetFieldPtr;
                        gfp.result = nextValueId();
                        gfp.op1    = gep.result;
                        gfp.intImm = 0;
                        gfp.type   = gv.type;
                        gfp.strImm = elemTy->structName;
                        emit(gfp);

                        Instruction st;
                        st.opcode = Opcode::Store;
                        st.op1    = gfp.result;
                        st.op2    = gv.result;
                        emit(st);
                    }

                    size_t argCount = 0;
                    if (aInit.elements[i]->kind == ExprKind::Construct) {
                        argCount = static_cast<ConstructExpr&>(*aInit.elements[i]).args.size();
                    } else {
                        argCount = static_cast<CallExpr&>(*aInit.elements[i]).args.size();
                    }

                    std::string ctorName = elemTy->constructors.size() == 1
                        ? (elemTy->structName + "_" + elemTy->structName)
                        : (elemTy->structName + "_" + elemTy->structName + "_" + std::to_string(argCount));
                    std::vector<ValueId> cargs;
                    cargs.push_back(gep.result); // this
                    if (aInit.elements[i]->kind == ExprKind::Construct) {
                        auto& ce = static_cast<ConstructExpr&>(*aInit.elements[i]);
                        for (auto& arg : ce.args) {
                            cargs.push_back(lowerExpr(*arg));
                        }
                    } else {
                        auto& callE = static_cast<CallExpr&>(*aInit.elements[i]);
                        for (auto& arg : callE.args) {
                            cargs.push_back(lowerExpr(*arg));
                        }
                    }
                    Instruction callCtor;
                    callCtor.opcode = Opcode::Call;
                    callCtor.type   = checker_.context().getVoid();
                    callCtor.strImm = ctorName;
                    callCtor.callArgs = std::move(cargs);
                    emit(callCtor);
                } else {
                    auto valId = lowerExpr(*aInit.elements[i]);
                    auto initElemTy = checker_.getExprType(aInit.elements[i].get());
                    valId = emitCastIfNeeded(valId, initElemTy, elemTy);

                    Instruction store;
                    store.opcode = Opcode::ArrayStore;
                    store.op1    = arrayBasePtr; // array
                    store.op2    = idxConst.result; // index
                    store.op3    = valId;           // value
                    emit(store);
                }
            }
        }

        // Construct remaining elements in declaration order using default construction semantics
        if (size > explicitCount && needsDefaultConstruction(elemTy)) {
            constructArrayElements(arrayBasePtr, elemTy, explicitCount, size - explicitCount);
        }

        if (isRegionAlloc && elemTy && elemTy->hasNonTrivialDestructor() && size > 0 && !activeRegionSlots_.empty()) {
            Instruction actInst;
            actInst.opcode = Opcode::RegionActivateDestructor;
            actInst.op1    = activeRegionSlots_.back();
            actInst.op2    = slotId;
            actInst.op3    = arrayBasePtr;
            actInst.strImm = elemTy->structName + "_destructor";
            actInst.intImm = size;
            actInst.intImm2 = elemTy->size();
            emit(actInst);
        } else if (!isRegionAlloc && elemTy && elemTy->hasNonTrivialDestructor() && size > 0 && !cleanupScopes_.empty()) {
            for (int64_t i = 0; i < size; ++i) {
                Instruction idxConst;
                idxConst.opcode = Opcode::IConst;
                idxConst.result = nextValueId();
                idxConst.type   = checker_.context().getUsize();
                idxConst.intImm = i;
                emit(idxConst);

                Instruction gep;
                gep.opcode = Opcode::GetArrayElemPtr;
                gep.result = nextValueId();
                gep.op1    = arrayBasePtr;
                gep.op2    = idxConst.result;
                gep.type   = elemTy;
                emit(gep);

                cleanupScopes_.back().push_back(CleanupItem{
                    .kind = CleanupKind::Destructor,
                    .ptr  = gep.result,
                    .type = elemTy
                });
            }
        }
        return;
    }

    // Reference variable
    if (varTy && varTy->isReference()) {
        Instruction allocaInst;
        allocaInst.opcode = Opcode::Alloca;
        allocaInst.result = nextValueId();
        allocaInst.type   = varTy; // ptr
        emit(allocaInst);

        Binding b;
        b.ptr = allocaInst.result;
        b.type = varTy;
        b.isConst = decl.isConst;
        setBinding(decl.name, std::move(b));

        if (decl.initializer) {
            ValueId refVal = InvalidValue;
            if (decl.initializer->kind == ExprKind::Identifier ||
                decl.initializer->kind == ExprKind::Field ||
                decl.initializer->kind == ExprKind::This) {
                refVal = getStructPointer(*decl.initializer);
            } else {
                refVal = lowerExpr(*decl.initializer);
            }

            Instruction store;
            store.opcode = Opcode::Store;
            store.op1    = allocaInst.result;
            store.op2    = refVal;
            emit(store);
        }
        return;
    }

    // Slab variable
    if (varTy && varTy->isSlab()) {
        Instruction allocaInst;
        allocaInst.opcode = Opcode::Alloca;
        allocaInst.result = nextValueId();
        allocaInst.type   = varTy;
        emit(allocaInst);

        Binding b;
        b.ptr = allocaInst.result;
        b.type = varTy;
        b.isConst = decl.isConst;
        setBinding(decl.name, std::move(b));

        int64_t capacity = varTy->arraySize > 0 ? varTy->arraySize : 64;
        if (decl.initializer && decl.initializer->kind == ExprKind::Construct) {
            auto& ce = static_cast<ConstructExpr&>(*decl.initializer);
            if (!ce.args.empty()) {
                auto eval = checker_.evalIntConstExpr(*ce.args[0]);
                if (eval.isConst && !eval.overflow && eval.value > 0) {
                    capacity = eval.value;
                }
            }
        }
        int64_t elemSize = varTy->elementType ? static_cast<int64_t>(varTy->elementType->size()) : 4;
        int64_t elemAlign = varTy->elementType ? static_cast<int64_t>(varTy->elementType->alignment()) : 4;

        Instruction initInst;
        initInst.opcode = Opcode::SlabInit;
        initInst.op1    = allocaInst.result;
        initInst.type   = varTy;
        initInst.intImm = capacity;
        initInst.intImm2 = elemSize;
        initInst.strImm = std::to_string(elemAlign);
        emit(initInst);

        if (!cleanupScopes_.empty()) {
            cleanupScopes_.back().push_back(CleanupItem{
                .kind = CleanupKind::SlabDestroy,
                .ptr  = allocaInst.result,
                .type = varTy
            });
        }
        return;
    }

    // Struct / Class record variable
    if (varTy && varTy->isRecord()) {
        bool isRegionAlloc = !activeRegionSlots_.empty();

        Instruction allocaInst;
        if (isRegionAlloc) {
            allocaInst.opcode = Opcode::RegionAlloc;
            allocaInst.result = nextValueId();
            allocaInst.type   = varTy;
            allocaInst.op1    = activeRegionSlots_.back();
            allocaInst.intImm = varTy->size();
            allocaInst.strImm = "16";
            emit(allocaInst);
        } else {
            allocaInst.opcode = Opcode::Alloca;
            allocaInst.result = nextValueId();
            allocaInst.type   = varTy;
            emit(allocaInst);
        }

        Binding b;
        b.ptr = allocaInst.result;
        b.type = varTy;
        b.isConst = decl.isConst;
        setBinding(decl.name, std::move(b));

        ValueId slotId = InvalidValue;
        if (isRegionAlloc && varTy->hasNonTrivialDestructor() && !activeRegionSlots_.empty()) {
            Instruction reserveInst;
            reserveInst.opcode = Opcode::RegionReserveDestructor;
            reserveInst.result = nextValueId();
            reserveInst.type   = checker_.context().getReference(checker_.context().getI8());
            reserveInst.op1    = activeRegionSlots_.back();
            emit(reserveInst);
            slotId = reserveInst.result;
        }

        // If polymorphic and no constructor call, initialize _vptr
        if (varTy->isPolymorphic && (!decl.initializer || decl.initializer->kind != ExprKind::Construct)) {
            Instruction gv;
            gv.opcode = Opcode::GetVTablePtr;
            gv.result = nextValueId();
            gv.strImm = varTy->structName;
            gv.type   = checker_.context().getReference(checker_.context().getI8());
            emit(gv);

            Instruction gfp;
            gfp.opcode = Opcode::GetFieldPtr;
            gfp.result = nextValueId();
            gfp.op1    = allocaInst.result;
            gfp.intImm = 0; // _vptr is field 0
            gfp.type   = gv.type;
            gfp.strImm = varTy->structName;
            emit(gfp);

            Instruction store;
            store.opcode = Opcode::Store;
            store.op1    = gfp.result;
            store.op2    = gv.result;
            emit(store);
        }

        if (decl.initializer) {
            if (decl.initializer->kind == ExprKind::Construct) {
                auto& ce = static_cast<ConstructExpr&>(*decl.initializer);
                std::string ctorName = varTy->constructors.size() == 1
                    ? (varTy->structName + "_" + varTy->structName)
                    : (varTy->structName + "_" + varTy->structName + "_" + std::to_string(ce.args.size()));
                std::vector<ValueId> cargs;
                cargs.push_back(allocaInst.result); // this
                for (auto& arg : ce.args) {
                    cargs.push_back(lowerExpr(*arg));
                }
                Instruction callCtor;
                callCtor.opcode = Opcode::Call;
                callCtor.type   = checker_.context().getVoid();
                callCtor.strImm = ctorName;
                callCtor.callArgs = std::move(cargs);
                emit(callCtor);
            } else if (decl.initializer->kind == ExprKind::CompoundInit) {
                auto& cie = static_cast<CompoundInitExpr&>(*decl.initializer);
                std::unordered_set<int> initializedFields;
                for (size_t i = 0; i < cie.fields.size(); ++i) {
                    auto& f = cie.fields[i];
                    int fIdx = -1;
                    const sema::StructFieldType* sft = nullptr;
                    if (!f.fieldName.empty()) {
                        fIdx = varTy->getFieldIndex(f.fieldName);
                        sft = varTy->findField(f.fieldName);
                    } else {
                        int userOffset = varTy->isPolymorphic ? 1 : 0;
                        fIdx = static_cast<int>(i) + userOffset;
                        if (fIdx < static_cast<int>(varTy->structFields.size())) sft = &varTy->structFields[fIdx];
                    }
                    if (fIdx < 0 || !sft) continue;
                    initializedFields.insert(fIdx);

                    auto valId = lowerExpr(*f.value);
                    auto valTy = checker_.getExprType(f.value.get());
                    valId = emitCastIfNeeded(valId, valTy, sft->type);

                    Instruction gfp;
                    gfp.opcode = Opcode::GetFieldPtr;
                    gfp.result = nextValueId();
                    gfp.op1    = allocaInst.result;
                    gfp.intImm = fIdx;
                    gfp.type   = sft->type;
                    gfp.strImm = varTy->structName;
                    emit(gfp);

                    Instruction store;
                    store.opcode = Opcode::Store;
                    store.op1    = gfp.result;
                    store.op2    = valId;
                    emit(store);
                }

                // Construct omitted fields that require default construction
                for (size_t i = 0; i < varTy->structFields.size(); ++i) {
                    if (initializedFields.count(static_cast<int>(i))) continue;
                    const auto& f = varTy->structFields[i];
                    if (f.name == "_vptr") continue;
                    if (!f.type || f.type->isReference()) continue;
                    if (needsDefaultConstruction(f.type)) {
                        Instruction gfp;
                        gfp.opcode = Opcode::GetFieldPtr;
                        gfp.result = nextValueId();
                        gfp.op1    = allocaInst.result;
                        gfp.intImm = static_cast<int64_t>(i);
                        gfp.type   = checker_.context().getReference(f.type);
                        gfp.strImm = varTy->structName;
                        emit(gfp);

                        constructDefaultObject(gfp.result, f.type);
                    }
                }
            } else {
                auto valId = lowerExpr(*decl.initializer);
                auto initTy = checker_.getExprType(decl.initializer.get());
                valId = emitCastIfNeeded(valId, initTy, varTy);

                Instruction storeInst;
                storeInst.opcode = Opcode::Store;
                storeInst.op1    = allocaInst.result;
                storeInst.op2    = valId;
                emit(storeInst);
            }
        } else {
            // Default construction of the record variable
            constructDefaultObject(allocaInst.result, varTy);
        }

        ValueId dropFlagPtr = InvalidValue;
        if (!isRegionAlloc && varTy->hasNonTrivialDestructor()) {
            ensureDestructor(varTy);
            Instruction dfAlloca;
            dfAlloca.opcode = Opcode::Alloca;
            dfAlloca.result = nextValueId();
            dfAlloca.type   = checker_.context().getBool();
            emit(dfAlloca);
            dropFlagPtr = dfAlloca.result;

            Instruction iconst1;
            iconst1.opcode = Opcode::IConst;
            iconst1.result = nextValueId();
            iconst1.type   = checker_.context().getBool();
            iconst1.intImm = 1;
            emit(iconst1);

            Instruction dfStore;
            dfStore.opcode = Opcode::Store;
            dfStore.op1    = dropFlagPtr;
            dfStore.op2    = iconst1.result;
            emit(dfStore);

            varDropFlags_[decl.name] = dropFlagPtr;
        }

        // RAII cleanup registration
        if (!isRegionAlloc && varTy->hasNonTrivialDestructor() && !cleanupScopes_.empty()) {
            cleanupScopes_.back().push_back(CleanupItem{
                .kind = CleanupKind::Destructor,
                .ptr = allocaInst.result,
                .type = varTy,
                .varName = decl.name,
                .dropFlag = dropFlagPtr,
            });
        } else if (isRegionAlloc && varTy->hasNonTrivialDestructor() && !activeRegionSlots_.empty()) {
            Instruction actInst;
            actInst.opcode = Opcode::RegionActivateDestructor;
            actInst.op1 = activeRegionSlots_.back();
            actInst.op2 = slotId;
            actInst.op3 = allocaInst.result;
            actInst.strImm = varTy->structName + "_destructor";
            actInst.intImm = 1;
            actInst.intImm2 = varTy->size();
            emit(actInst);
        }
        return;
    }

    // Enum / Tagged Union variable
    if (varTy && varTy->isEnum()) {
        Instruction allocaInst;
        allocaInst.opcode = Opcode::Alloca;
        allocaInst.result = nextValueId();
        allocaInst.type   = varTy;
        emit(allocaInst);

        Binding b;
        b.ptr = allocaInst.result;
        b.type = varTy;
        b.isConst = decl.isConst;
        setBinding(decl.name, std::move(b));

        if (decl.initializer) {
            auto valId = lowerExpr(*decl.initializer);
            auto initTy = checker_.getExprType(decl.initializer.get());
            valId = emitCastIfNeeded(valId, initTy, varTy);

            Instruction storeInst;
            storeInst.opcode = Opcode::Store;
            storeInst.op1    = allocaInst.result;
            storeInst.op2    = valId;
            emit(storeInst);
        }

        if (varTy->hasNonTrivialDestructor()) {
            ensureDestructor(varTy);
            Instruction dfAlloca;
            dfAlloca.opcode = Opcode::Alloca;
            dfAlloca.result = nextValueId();
            dfAlloca.type   = checker_.context().getBool();
            emit(dfAlloca);
            ValueId dropFlagPtr = dfAlloca.result;

            Instruction iconst1;
            iconst1.opcode = Opcode::IConst;
            iconst1.result = nextValueId();
            iconst1.type   = checker_.context().getBool();
            iconst1.intImm = 1;
            emit(iconst1);

            Instruction dfStore;
            dfStore.opcode = Opcode::Store;
            dfStore.op1    = dropFlagPtr;
            dfStore.op2    = iconst1.result;
            emit(dfStore);

            varDropFlags_[decl.name] = dropFlagPtr;

            if (!cleanupScopes_.empty()) {
                cleanupScopes_.back().push_back(CleanupItem{
                    .kind = CleanupKind::Destructor,
                    .ptr = allocaInst.result,
                    .type = varTy,
                    .varName = decl.name,
                    .dropFlag = dropFlagPtr,
                });
            }
        }
        return;
    }

    // Scalar variable
    Instruction allocaInst;
    allocaInst.opcode = Opcode::Alloca;
    allocaInst.result = nextValueId();
    allocaInst.type   = varTy;
    emit(allocaInst);

    Binding b;
    b.ptr = allocaInst.result;
    b.type = varTy;
    b.isConst = decl.isConst;
    setBinding(decl.name, std::move(b));

    if (decl.initializer) {
        auto valId = lowerExpr(*decl.initializer);
        auto initTy = checker_.getExprType(decl.initializer.get());
        valId = emitCastIfNeeded(valId, initTy, varTy);

        Instruction storeInst;
        storeInst.opcode = Opcode::Store;
        storeInst.op1    = allocaInst.result;
        storeInst.op2    = valId;
        emit(storeInst);
    }
}

void IRBuilder::lowerDestructureVarDecl(DestructureVarDeclStmt& stmt) {
    if (!stmt.initializer) return;
    auto tupPtr = getStructPointer(*stmt.initializer);
    auto tupTy = checker_.getExprType(stmt.initializer.get());
    if (tupTy && tupTy->isReference()) tupTy = tupTy->elementType;
    if (!tupTy || !tupTy->isTuple()) return;

    const auto* elemTys = checker_.getDestructureTypes(&stmt);

    for (size_t i = 0; i < stmt.names.size(); ++i) {
        const auto* elemTy = (elemTys && i < elemTys->size()) ? (*elemTys)[i] : ((i < tupTy->tupleElements.size()) ? tupTy->tupleElements[i] : nullptr);
        if (!elemTy) continue;

        Instruction allocaInst;
        allocaInst.opcode = Opcode::Alloca;
        allocaInst.result = nextValueId();
        allocaInst.type   = elemTy;
        emit(allocaInst);

        Instruction gfp;
        gfp.opcode = Opcode::GetFieldPtr;
        gfp.result = nextValueId();
        gfp.op1    = tupPtr;
        gfp.intImm = static_cast<int64_t>(i);
        gfp.type   = elemTy;
        emit(gfp);

        Instruction loadElem;
        loadElem.opcode = Opcode::Load;
        loadElem.result = nextValueId();
        loadElem.op1    = gfp.result;
        loadElem.type   = elemTy;
        emit(loadElem);

        Instruction storeElem;
        storeElem.opcode = Opcode::Store;
        storeElem.op1    = allocaInst.result;
        storeElem.op2    = loadElem.result;
        emit(storeElem);

        Binding b;
        b.ptr = allocaInst.result;
        b.type = elemTy;
        b.isConst = stmt.isConst;
        setBinding(stmt.names[i], std::move(b));
    }
}

void IRBuilder::lowerReturn(ReturnStmt& stmt) {
    if (isCurrentFunctionAsync_) {
        if (stmt.value) {
            if (stmt.value->kind == ast::ExprKind::Identifier) {
                const auto& id = static_cast<const ast::IdentifierExpr&>(*stmt.value);
                markVariableMoved(id.name);
            }
            auto val = lowerExpr(*stmt.value);
            auto valTy = checker_.getExprType(stmt.value.get());
            if (currentAsyncUnderlyingRetTy_) {
                val = emitCastIfNeeded(val, valTy, currentAsyncUnderlyingRetTy_);
            }
            if (currentAsyncResultPtr_ != InvalidValue) {
                Instruction storeInst;
                storeInst.opcode = Opcode::Store;
                storeInst.op1    = currentAsyncResultPtr_;
                storeInst.op2    = val;
                emit(storeInst);
            }
        }
        emitCleanupsUpTo(0);
        Instruction retInst;
        retInst.opcode = Opcode::Return;
        retInst.op1    = InvalidValue;
        emit(retInst);
        currentBlock_ = nullptr;
        return;
    }

    Instruction retInst;
    retInst.opcode = Opcode::Return;
    if (stmt.value) {
        if (stmt.value->kind == ast::ExprKind::Identifier) {
            const auto& id = static_cast<const ast::IdentifierExpr&>(*stmt.value);
            markVariableMoved(id.name);
        }
        auto val = lowerExpr(*stmt.value);
        auto valTy = checker_.getExprType(stmt.value.get());
        auto retTy = currentFunction_->returnType;
        val = emitCastIfNeeded(val, valTy, retTy);
        retInst.op1 = val;
    } else {
        retInst.op1 = InvalidValue;
    }
    // Deterministic RAII cleanup before return
    emitCleanupsUpTo(0);
    emit(retInst);
    currentBlock_ = nullptr;
}

void IRBuilder::lowerIf(IfStmt& stmt) {
    auto condVal = lowerExpr(*stmt.condition);

    auto thenBlock = currentFunction_->createBlock(nextLabel("then"));
    auto elseBlock = stmt.elseBlock ? currentFunction_->createBlock(nextLabel("else")) : nullptr;

    if (elseBlock) {
        // Emit branch to then or else
        Instruction br;
        br.opcode = Opcode::Branch;
        br.op1    = condVal;
        br.label1 = thenBlock->label;
        br.label2 = elseBlock->label;
        emit(br);

        // Lower then branch
        setInsertBlock(thenBlock);
        lowerStmt(*stmt.thenBlock);
        bool thenTerminated = isCurrentBlockTerminated() || currentBlock_ == nullptr;
        auto thenEndBlock = currentBlock_;

        // Lower else branch
        setInsertBlock(elseBlock);
        lowerStmt(*stmt.elseBlock);
        bool elseTerminated = isCurrentBlockTerminated() || currentBlock_ == nullptr;
        auto elseEndBlock = currentBlock_;

        // If BOTH branches terminate, do NOT create an unnecessary merge block!
        if (thenTerminated && elseTerminated) {
            currentBlock_ = nullptr;
            return;
        }

        // At least one branch does not terminate: create mergeBlock
        auto mergeBlock = currentFunction_->createBlock(nextLabel("merge"));

        if (!thenTerminated && thenEndBlock) {
            setInsertBlock(thenEndBlock);
            Instruction jmp;
            jmp.opcode = Opcode::Jump;
            jmp.label1 = mergeBlock->label;
            emit(jmp);
        }

        if (!elseTerminated && elseEndBlock) {
            setInsertBlock(elseEndBlock);
            Instruction jmp;
            jmp.opcode = Opcode::Jump;
            jmp.label1 = mergeBlock->label;
            emit(jmp);
        }

        setInsertBlock(mergeBlock);
    } else {
        // No else block: if condition is false, jump to mergeBlock
        auto mergeBlock = currentFunction_->createBlock(nextLabel("merge"));

        Instruction br;
        br.opcode = Opcode::Branch;
        br.op1    = condVal;
        br.label1 = thenBlock->label;
        br.label2 = mergeBlock->label;
        emit(br);

        // Lower then branch
        setInsertBlock(thenBlock);
        lowerStmt(*stmt.thenBlock);
        bool thenTerminated = isCurrentBlockTerminated() || currentBlock_ == nullptr;
        auto thenEndBlock = currentBlock_;

        if (!thenTerminated && thenEndBlock) {
            setInsertBlock(thenEndBlock);
            Instruction jmp;
            jmp.opcode = Opcode::Jump;
            jmp.label1 = mergeBlock->label;
            emit(jmp);
        }

        setInsertBlock(mergeBlock);
    }
}

void IRBuilder::lowerWhile(WhileStmt& stmt) {
    auto condBlock = currentFunction_->createBlock(nextLabel("while_cond"));
    auto bodyBlock = currentFunction_->createBlock(nextLabel("while_body"));
    auto exitBlock = currentFunction_->createBlock(nextLabel("while_exit"));

    Instruction jmp;
    jmp.opcode = Opcode::Jump;
    jmp.label1 = condBlock->label;
    emit(jmp);

    // Cond
    setInsertBlock(condBlock);
    auto condVal = lowerExpr(*stmt.condition);
    Instruction br;
    br.opcode = Opcode::Branch;
    br.op1    = condVal;
    br.label1 = bodyBlock->label;
    br.label2 = exitBlock->label;
    br.unrollCount = stmt.unrollCount;
    br.vectorizeWidth = stmt.vectorizeWidth;
    br.interleaveCount = stmt.interleaveCount;
    br.vectorizeEnable = stmt.vectorizeEnable;
    emit(br);

    // Body
    setInsertBlock(bodyBlock);
    loopStack_.push_back(LoopTarget{exitBlock->label, condBlock->label, cleanupScopes_.size()});
    lowerStmt(*stmt.body);
    loopStack_.pop_back();

    if (currentBlock_ && !isCurrentBlockTerminated()) {
        Instruction loopJmp;
        loopJmp.opcode = Opcode::Jump;
        loopJmp.label1 = condBlock->label;
        loopJmp.unrollCount = stmt.unrollCount;
        loopJmp.vectorizeWidth = stmt.vectorizeWidth;
        loopJmp.interleaveCount = stmt.interleaveCount;
        loopJmp.vectorizeEnable = stmt.vectorizeEnable;
        emit(loopJmp);
    }

    setInsertBlock(exitBlock);
}

void IRBuilder::lowerFor(ForStmt& stmt) {
    pushScope();

    if (stmt.init) {
        lowerStmt(*stmt.init);
    }

    auto condBlock = currentFunction_->createBlock(nextLabel("for_cond"));
    auto bodyBlock = currentFunction_->createBlock(nextLabel("for_body"));
    auto incBlock  = currentFunction_->createBlock(nextLabel("for_inc"));
    auto exitBlock = currentFunction_->createBlock(nextLabel("for_exit"));

    Instruction jmp;
    jmp.opcode = Opcode::Jump;
    jmp.label1 = condBlock->label;
    emit(jmp);

    // Condition
    setInsertBlock(condBlock);
    if (stmt.condition) {
        auto condVal = lowerExpr(*stmt.condition);
        Instruction br;
        br.opcode = Opcode::Branch;
        br.op1    = condVal;
        br.label1 = bodyBlock->label;
        br.label2 = exitBlock->label;
        br.unrollCount = stmt.unrollCount;
        br.vectorizeWidth = stmt.vectorizeWidth;
        br.interleaveCount = stmt.interleaveCount;
        br.vectorizeEnable = stmt.vectorizeEnable;
        emit(br);
    } else {
        Instruction br;
        br.opcode = Opcode::Jump;
        br.label1 = bodyBlock->label;
        br.unrollCount = stmt.unrollCount;
        br.vectorizeWidth = stmt.vectorizeWidth;
        br.interleaveCount = stmt.interleaveCount;
        br.vectorizeEnable = stmt.vectorizeEnable;
        emit(br);
    }

    // Body
    setInsertBlock(bodyBlock);
    loopStack_.push_back(LoopTarget{exitBlock->label, incBlock->label, cleanupScopes_.size()});
    lowerStmt(*stmt.body);
    loopStack_.pop_back();

    if (currentBlock_ && !isCurrentBlockTerminated()) {
        Instruction toInc;
        toInc.opcode = Opcode::Jump;
        toInc.label1 = incBlock->label;
        emit(toInc);
    }

    // Increment
    setInsertBlock(incBlock);
    if (stmt.increment) {
        lowerExpr(*stmt.increment);
    }
    if (currentBlock_ && !isCurrentBlockTerminated()) {
        Instruction toCond;
        toCond.opcode = Opcode::Jump;
        toCond.label1 = condBlock->label;
        toCond.unrollCount = stmt.unrollCount;
        toCond.vectorizeWidth = stmt.vectorizeWidth;
        toCond.interleaveCount = stmt.interleaveCount;
        toCond.vectorizeEnable = stmt.vectorizeEnable;
        emit(toCond);
    }

    setInsertBlock(exitBlock);
    popScope();
}

static void collectReferencedIdentifiers(const ast::Stmt& stmt,
                                         std::unordered_set<std::string>& ids);

static void collectReferencedIdentifiers(const ast::Expr& expr,
                                         std::unordered_set<std::string>& ids) {
    switch (expr.kind) {
        case ast::ExprKind::Identifier: {
            auto& id = static_cast<const ast::IdentifierExpr&>(expr);
            ids.insert(id.name);
            break;
        }
        case ast::ExprKind::Binary: {
            auto& b = static_cast<const ast::BinaryExpr&>(expr);
            if (b.lhs) collectReferencedIdentifiers(*b.lhs, ids);
            if (b.rhs) collectReferencedIdentifiers(*b.rhs, ids);
            break;
        }
        case ast::ExprKind::Unary: {
            auto& u = static_cast<const ast::UnaryExpr&>(expr);
            if (u.operand) collectReferencedIdentifiers(*u.operand, ids);
            break;
        }
        case ast::ExprKind::Assign: {
            auto& a = static_cast<const ast::AssignExpr&>(expr);
            if (a.target) collectReferencedIdentifiers(*a.target, ids);
            if (a.value) collectReferencedIdentifiers(*a.value, ids);
            break;
        }
        case ast::ExprKind::Call: {
            auto& c = static_cast<const ast::CallExpr&>(expr);
            for (const auto& arg : c.args) {
                if (arg) collectReferencedIdentifiers(*arg, ids);
            }
            break;
        }
        case ast::ExprKind::Index: {
            auto& idx = static_cast<const ast::IndexExpr&>(expr);
            if (idx.array) collectReferencedIdentifiers(*idx.array, ids);
            if (idx.index) collectReferencedIdentifiers(*idx.index, ids);
            break;
        }
        case ast::ExprKind::Field: {
            auto& f = static_cast<const ast::FieldExpr&>(expr);
            if (f.object) collectReferencedIdentifiers(*f.object, ids);
            break;
        }
        case ast::ExprKind::Cast: {
            auto& c = static_cast<const ast::CastExpr&>(expr);
            if (c.operand) collectReferencedIdentifiers(*c.operand, ids);
            break;
        }
        case ast::ExprKind::MethodCall: {
            auto& mc = static_cast<const ast::MethodCallExpr&>(expr);
            if (mc.object) collectReferencedIdentifiers(*mc.object, ids);
            for (const auto& arg : mc.args) {
                if (arg) collectReferencedIdentifiers(*arg, ids);
            }
            break;
        }
        case ast::ExprKind::ArrayInit: {
            auto& ai = static_cast<const ast::ArrayInitExpr&>(expr);
            for (const auto& elem : ai.elements) {
                if (elem) collectReferencedIdentifiers(*elem, ids);
            }
            break;
        }
        case ast::ExprKind::CompoundInit: {
            auto& ci = static_cast<const ast::CompoundInitExpr&>(expr);
            for (const auto& f : ci.fields) {
                if (f.value) collectReferencedIdentifiers(*f.value, ids);
            }
            break;
        }
        case ast::ExprKind::Construct: {
            auto& cn = static_cast<const ast::ConstructExpr&>(expr);
            for (const auto& arg : cn.args) {
                if (arg) collectReferencedIdentifiers(*arg, ids);
            }
            break;
        }
        case ast::ExprKind::Match: {
            auto& m = static_cast<const ast::MatchExpr&>(expr);
            if (m.expr) collectReferencedIdentifiers(*m.expr, ids);
            for (const auto& mc : m.cases) {
                if (mc.guard) collectReferencedIdentifiers(*mc.guard, ids);
                if (mc.body) collectReferencedIdentifiers(*mc.body, ids);
            }
            break;
        }
        case ast::ExprKind::Try: {
            auto& tr = static_cast<const ast::TryExpr&>(expr);
            if (tr.operand) collectReferencedIdentifiers(*tr.operand, ids);
            break;
        }
        default:
            break;
    }
}

static void collectReferencedIdentifiers(const ast::Stmt& stmt,
                                         std::unordered_set<std::string>& ids) {
    switch (stmt.kind) {
        case ast::StmtKind::Expr: {
            auto& e = static_cast<const ast::ExprStmt&>(stmt);
            if (e.expr) collectReferencedIdentifiers(*e.expr, ids);
            break;
        }
        case ast::StmtKind::Block: {
            auto& b = static_cast<const ast::BlockStmt&>(stmt);
            for (const auto& s : b.stmts) {
                if (s) collectReferencedIdentifiers(*s, ids);
            }
            break;
        }
        case ast::StmtKind::If: {
            auto& i = static_cast<const ast::IfStmt&>(stmt);
            if (i.condition) collectReferencedIdentifiers(*i.condition, ids);
            if (i.thenBlock) collectReferencedIdentifiers(*i.thenBlock, ids);
            if (i.elseBlock) collectReferencedIdentifiers(*i.elseBlock, ids);
            break;
        }
        case ast::StmtKind::While: {
            auto& w = static_cast<const ast::WhileStmt&>(stmt);
            if (w.condition) collectReferencedIdentifiers(*w.condition, ids);
            if (w.body) collectReferencedIdentifiers(*w.body, ids);
            break;
        }
        case ast::StmtKind::For: {
            auto& f = static_cast<const ast::ForStmt&>(stmt);
            if (f.init) collectReferencedIdentifiers(*f.init, ids);
            if (f.condition) collectReferencedIdentifiers(*f.condition, ids);
            if (f.increment) collectReferencedIdentifiers(*f.increment, ids);
            if (f.body) collectReferencedIdentifiers(*f.body, ids);
            break;
        }
        case ast::StmtKind::VarDecl: {
            auto& v = static_cast<const ast::VarDeclStmt&>(stmt);
            if (v.initializer) collectReferencedIdentifiers(*v.initializer, ids);
            break;
        }
        case ast::StmtKind::Return: {
            auto& r = static_cast<const ast::ReturnStmt&>(stmt);
            if (r.value) collectReferencedIdentifiers(*r.value, ids);
            break;
        }
        case ast::StmtKind::ParallelFor: {
            auto& pf = static_cast<const ast::ParallelForStmt&>(stmt);
            if (pf.init) collectReferencedIdentifiers(*pf.init, ids);
            if (pf.condition) collectReferencedIdentifiers(*pf.condition, ids);
            if (pf.increment) collectReferencedIdentifiers(*pf.increment, ids);
            if (pf.body) collectReferencedIdentifiers(*pf.body, ids);
            break;
        }
        case ast::StmtKind::TaskScope: {
            auto& ts = static_cast<const ast::TaskScopeStmt&>(stmt);
            if (ts.body) collectReferencedIdentifiers(*ts.body, ids);
            break;
        }
        case ast::StmtKind::Spawn: {
            auto& sp = static_cast<const ast::SpawnStmt&>(stmt);
            if (sp.body) collectReferencedIdentifiers(*sp.body, ids);
            break;
        }
        case ast::StmtKind::Match: {
            auto& m = static_cast<const ast::MatchStmt&>(stmt);
            if (m.expr) collectReferencedIdentifiers(*m.expr, ids);
            for (const auto& mc : m.cases) {
                if (mc.guard) collectReferencedIdentifiers(*mc.guard, ids);
                if (mc.body) collectReferencedIdentifiers(*mc.body, ids);
            }
            break;
        }
        case ast::StmtKind::Region: {
            auto& r = static_cast<const ast::RegionStmt&>(stmt);
            if (r.capacityExpr) collectReferencedIdentifiers(*r.capacityExpr, ids);
            if (r.body) collectReferencedIdentifiers(*r.body, ids);
            break;
        }
        case ast::StmtKind::Select: {
            auto& sel = static_cast<const ast::SelectStmt&>(stmt);
            for (const auto& sc : sel.cases) {
                if (sc.condition) collectReferencedIdentifiers(*sc.condition, ids);
                for (const auto& s : sc.body) {
                    if (s) collectReferencedIdentifiers(*s, ids);
                }
            }
            for (const auto& s : sel.defaultBody) {
                if (s) collectReferencedIdentifiers(*s, ids);
            }
            break;
        }
        default:
            break;
    }
}

void IRBuilder::lowerParallelFor(ParallelForStmt& stmt) {
    if (!currentBlock_ || isCurrentBlockTerminated()) return;

    // 1. Identify induction variable and loop range
    std::string indVarName = "i";
    const sema::Type* indVarType = checker_.context().getI32();
    ValueId startVal = emitIntConstant(0, checker_.context().getI64());
    if (stmt.init) {
        if (stmt.init->kind == StmtKind::VarDecl) {
            auto& vd = static_cast<VarDeclStmt&>(*stmt.init);
            indVarName = vd.name;
            if (vd.type) {
                indVarType = checker_.resolveType(*vd.type);
            }
            if (vd.initializer) {
                startVal = lowerExpr(*vd.initializer);
                startVal = emitCastIfNeeded(startVal, checker_.getExprType(vd.initializer.get()), checker_.context().getI64());
            }
        } else if (stmt.init->kind == StmtKind::Expr) {
            auto& es = static_cast<ExprStmt&>(*stmt.init);
            if (es.expr) {
                startVal = lowerExpr(*es.expr);
                startVal = emitCastIfNeeded(startVal, checker_.getExprType(es.expr.get()), checker_.context().getI64());
            }
        }
    }

    ValueId endVal = emitIntConstant(0, checker_.context().getI64());
    if (stmt.condition) {
        if (stmt.condition->kind == ExprKind::Binary) {
            auto& bin = static_cast<BinaryExpr&>(*stmt.condition);
            endVal = lowerExpr(*bin.rhs);
            endVal = emitCastIfNeeded(endVal, checker_.getExprType(bin.rhs.get()), checker_.context().getI64());
            if (bin.op == BinaryOp::LtEq) {
                Instruction addOne;
                addOne.opcode = Opcode::Add;
                addOne.result = nextValueId();
                addOne.op1    = endVal;
                addOne.op2    = emitIntConstant(1, checker_.context().getI64());
                addOne.type   = checker_.context().getI64();
                emit(addOne);
                endVal = addOne.result;
            }
        } else {
            endVal = lowerExpr(*stmt.condition);
            endVal = emitCastIfNeeded(endVal, checker_.getExprType(stmt.condition.get()), checker_.context().getI64());
        }
    }

    ValueId grainVal = emitIntConstant(0, checker_.context().getI64());
    if (stmt.grainSize) {
        grainVal = lowerExpr(*stmt.grainSize);
        grainVal = emitCastIfNeeded(grainVal, checker_.getExprType(stmt.grainSize.get()), checker_.context().getI64());
    }

    // 2. Collect referenced outer identifiers
    std::unordered_set<std::string> refIds;
    if (stmt.body) {
        collectReferencedIdentifiers(*stmt.body, refIds);
    }

    std::unordered_map<std::string, ast::ReductionOp> redMap;
    for (const auto& r : stmt.reductions) {
        redMap[r.varName] = r.op;
        refIds.insert(r.varName);
    }

    std::vector<std::string> capturedNames;
    std::vector<Binding> capturedBindings;
    for (const auto& name : refIds) {
        if (name == indVarName) continue;
        const Binding* b = lookupBinding(name);
        if (b) {
            capturedNames.push_back(name);
            capturedBindings.push_back(*b);
        }
    }

    // 3. Allocate context array in parent function
    ValueId ctxVal = InvalidValue;
    auto ptrTy = checker_.context().getReference(checker_.context().getI8());
    if (!capturedNames.empty()) {
        size_t slots = capturedNames.size();
        Instruction allocaCtx;
        allocaCtx.opcode = Opcode::Alloca;
        allocaCtx.result = nextValueId();
        allocaCtx.type   = checker_.context().getArray(ptrTy, static_cast<int64_t>(slots));
        emit(allocaCtx);
        ctxVal = allocaCtx.result;

        for (size_t k = 0; k < capturedNames.size(); ++k) {
            Instruction gepPtr;
            gepPtr.opcode = Opcode::GetArrayElemPtr;
            gepPtr.result = nextValueId();
            gepPtr.op1    = allocaCtx.result;
            gepPtr.op2    = emitIntConstant(static_cast<int64_t>(k), checker_.context().getI64());
            gepPtr.type   = ptrTy;
            emit(gepPtr);

            Instruction storePtr;
            storePtr.opcode = Opcode::Store;
            storePtr.op1    = gepPtr.result;
            storePtr.op2    = capturedBindings[k].ptr;
            emit(storePtr);
        }
    }

    // 4. Create and lower worker function
    std::string workerName = nextLabel("__tamarin_pfor_worker");
    auto* parentFn = currentFunction_;
    auto* parentBlock = currentBlock_;
    auto savedScopeStack = std::move(scopeStack_);
    scopeStack_.clear();

    auto workerFn = currentModule_->createFunction(workerName, checker_.context().getVoid());
    ValueId pStart = nextValueId();
    ValueId pEnd   = nextValueId();
    ValueId pCtx   = nextValueId();
    workerFn->params.push_back(IRParam{"chunk_start", checker_.context().getI64(), pStart});
    workerFn->params.push_back(IRParam{"chunk_end", checker_.context().getI64(), pEnd});
    workerFn->params.push_back(IRParam{"ctx", ptrTy, pCtx});

    currentFunction_ = workerFn;
    auto entryBlock = workerFn->createBlock("entry");
    setInsertBlock(entryBlock);
    pushScope();

    struct WorkerReduction {
        ValueId outerPtr;
        ValueId localPtr;
        const sema::Type* type;
        ast::ReductionOp op;
    };
    std::vector<WorkerReduction> workerReductions;

    auto emitIdentityVal = [&](ast::ReductionOp op, const sema::Type* ty) -> ValueId {
        bool isF = ty && ty->isFloat();
        switch (op) {
            case ast::ReductionOp::Add:
            case ast::ReductionOp::BitOr:
            case ast::ReductionOp::BitXor: {
                if (isF) {
                    Instruction c;
                    c.opcode   = Opcode::FConst;
                    c.result   = nextValueId();
                    c.type     = ty;
                    c.floatImm = 0.0;
                    emit(c);
                    return c.result;
                }
                return emitIntConstant(0, ty);
            }
            case ast::ReductionOp::Mul: {
                if (isF) {
                    Instruction c;
                    c.opcode   = Opcode::FConst;
                    c.result   = nextValueId();
                    c.type     = ty;
                    c.floatImm = 1.0;
                    emit(c);
                    return c.result;
                }
                return emitIntConstant(1, ty);
            }
            case ast::ReductionOp::BitAnd: {
                return emitIntConstant(-1, ty);
            }
            case ast::ReductionOp::Min: {
                if (isF) {
                    Instruction c;
                    c.opcode   = Opcode::FConst;
                    c.result   = nextValueId();
                    c.type     = ty;
                    c.floatImm = std::numeric_limits<double>::infinity();
                    emit(c);
                    return c.result;
                }
                if (ty && ty->isUnsignedInteger()) {
                    return emitIntConstant(-1, ty);
                }
                int64_t maxVal = (ty && ty->kind == sema::TypeKind::I32) ? INT32_MAX : INT64_MAX;
                return emitIntConstant(maxVal, ty);
            }
            case ast::ReductionOp::Max: {
                if (isF) {
                    Instruction c;
                    c.opcode   = Opcode::FConst;
                    c.result   = nextValueId();
                    c.type     = ty;
                    c.floatImm = -std::numeric_limits<double>::infinity();
                    emit(c);
                    return c.result;
                }
                if (ty && ty->isUnsignedInteger()) {
                    return emitIntConstant(0, ty);
                }
                int64_t minVal = (ty && ty->kind == sema::TypeKind::I32) ? INT32_MIN : INT64_MIN;
                return emitIntConstant(minVal, ty);
            }
        }
        return emitIntConstant(0, ty);
    };

    // Unpack context in worker
    if (!capturedNames.empty()) {
        for (size_t k = 0; k < capturedNames.size(); ++k) {
            Instruction gepPtr;
            gepPtr.opcode = Opcode::GetArrayElemPtr;
            gepPtr.result = nextValueId();
            gepPtr.op1    = pCtx;
            gepPtr.op2    = emitIntConstant(static_cast<int64_t>(k), checker_.context().getI64());
            gepPtr.type   = ptrTy;
            emit(gepPtr);

            Instruction loadPtr;
            loadPtr.opcode = Opcode::Load;
            loadPtr.result = nextValueId();
            loadPtr.op1    = gepPtr.result;
            loadPtr.type   = ptrTy;
            emit(loadPtr);

            auto itRed = redMap.find(capturedNames[k]);
            if (itRed != redMap.end()) {
                auto redValTy = capturedBindings[k].type;
                Instruction allocaRed;
                allocaRed.opcode = Opcode::Alloca;
                allocaRed.result = nextValueId();
                allocaRed.type   = redValTy;
                emit(allocaRed);

                ValueId idVal = emitIdentityVal(itRed->second, redValTy);
                Instruction storeInit;
                storeInit.opcode = Opcode::Store;
                storeInit.op1    = allocaRed.result;
                storeInit.op2    = idVal;
                emit(storeInit);

                Binding b;
                b.ptr     = allocaRed.result;
                b.type    = redValTy;
                b.isConst = false;
                setBinding(capturedNames[k], std::move(b));

                workerReductions.push_back({loadPtr.result, allocaRed.result, redValTy, itRed->second});
            } else {
                Binding b;
                b.ptr = loadPtr.result;
                b.type = capturedBindings[k].type;
                b.isConst = capturedBindings[k].isConst;
                b.isParameter = capturedBindings[k].isParameter;
                b.constantArrayLen = capturedBindings[k].constantArrayLen;
                if (capturedBindings[k].constantArrayLen >= 0) {
                    b.arrayLenValueId = emitIntConstant(capturedBindings[k].constantArrayLen, checker_.context().getI64());
                } else if (capturedBindings[k].type && capturedBindings[k].type->isArray() && capturedBindings[k].type->arraySize > 0) {
                    b.arrayLenValueId = emitIntConstant(capturedBindings[k].type->arraySize, checker_.context().getI64());
                }
                setBinding(capturedNames[k], std::move(b));
            }
        }
    }

    // Allocate and initialize induction variable
    Instruction allocaInd;
    allocaInd.opcode = Opcode::Alloca;
    allocaInd.result = nextValueId();
    allocaInd.type   = indVarType;
    emit(allocaInd);

    ValueId initVal = emitCastIfNeeded(pStart, checker_.context().getI64(), indVarType);

    Instruction storeInit;
    storeInit.opcode = Opcode::Store;
    storeInit.op1    = allocaInd.result;
    storeInit.op2    = initVal;
    emit(storeInit);

    Binding indBinding;
    indBinding.ptr = allocaInd.result;
    indBinding.type = indVarType;
    setBinding(indVarName, std::move(indBinding));

    // Canonical loop inside worker
    auto condBlock = workerFn->createBlock("pfor_cond");
    auto bodyBlock = workerFn->createBlock("pfor_body");
    auto incBlock  = workerFn->createBlock("pfor_inc");
    auto exitBlock = workerFn->createBlock("pfor_exit");

    Instruction toCond;
    toCond.opcode = Opcode::Jump;
    toCond.label1 = condBlock->label;
    emit(toCond);

    // Condition
    setInsertBlock(condBlock);
    Instruction loadI;
    loadI.opcode = Opcode::Load;
    loadI.result = nextValueId();
    loadI.op1    = allocaInd.result;
    loadI.type   = indVarType;
    emit(loadI);

    ValueId loadI64 = emitCastIfNeeded(loadI.result, indVarType, checker_.context().getI64());

    Instruction cmp;
    cmp.opcode = Opcode::ICmpSLt;
    cmp.result = nextValueId();
    cmp.op1    = loadI64;
    cmp.op2    = pEnd;
    cmp.type   = checker_.context().getBool();
    emit(cmp);

    Instruction br;
    br.opcode = Opcode::Branch;
    br.op1    = cmp.result;
    br.label1 = bodyBlock->label;
    br.label2 = exitBlock->label;
    br.unrollCount = stmt.unrollCount;
    br.vectorizeWidth = stmt.vectorizeWidth;
    br.interleaveCount = stmt.interleaveCount;
    br.vectorizeEnable = stmt.vectorizeEnable;
    emit(br);

    // Body
    setInsertBlock(bodyBlock);
    loopStack_.push_back(LoopTarget{exitBlock->label, incBlock->label, cleanupScopes_.size()});
    if (stmt.body) {
        lowerStmt(*stmt.body);
    }
    loopStack_.pop_back();

    if (currentBlock_ && !isCurrentBlockTerminated()) {
        Instruction toInc;
        toInc.opcode = Opcode::Jump;
        toInc.label1 = incBlock->label;
        emit(toInc);
    }

    // Increment
    setInsertBlock(incBlock);
    Instruction loadInc;
    loadInc.opcode = Opcode::Load;
    loadInc.result = nextValueId();
    loadInc.op1    = allocaInd.result;
    loadInc.type   = indVarType;
    emit(loadInc);

    Instruction addInc;
    addInc.opcode = Opcode::Add;
    addInc.result = nextValueId();
    addInc.op1    = loadInc.result;
    addInc.op2    = emitIntConstant(1, indVarType);
    addInc.type   = indVarType;
    emit(addInc);

    Instruction storeInc;
    storeInc.opcode = Opcode::Store;
    storeInc.op1    = allocaInd.result;
    storeInc.op2    = addInc.result;
    emit(storeInc);

    Instruction loopBack;
    loopBack.opcode = Opcode::Jump;
    loopBack.label1 = condBlock->label;
    loopBack.unrollCount = stmt.unrollCount;
    loopBack.vectorizeWidth = stmt.vectorizeWidth;
    loopBack.interleaveCount = stmt.interleaveCount;
    loopBack.vectorizeEnable = stmt.vectorizeEnable;
    emit(loopBack);

    // Exit
    setInsertBlock(exitBlock);

    for (const auto& wr : workerReductions) {
        Instruction loadLocal;
        loadLocal.opcode = Opcode::Load;
        loadLocal.result = nextValueId();
        loadLocal.op1    = wr.localPtr;
        loadLocal.type   = wr.type;
        emit(loadLocal);

        if (wr.op == ast::ReductionOp::Mul) {
            auto casLoop = workerFn->createBlock("pfor_mul_cas");
            auto casCont = workerFn->createBlock("pfor_mul_cont");
            Instruction toCas;
            toCas.opcode = Opcode::Jump;
            toCas.label1 = casLoop->label;
            emit(toCas);

            setInsertBlock(casLoop);
            Instruction curLoad;
            curLoad.opcode = Opcode::AtomicLoad;
            curLoad.result = nextValueId();
            curLoad.op1    = wr.outerPtr;
            curLoad.type   = wr.type;
            emit(curLoad);

            Instruction mulInst;
            mulInst.opcode = (wr.type && wr.type->isFloat()) ? Opcode::FMul : Opcode::Mul;
            mulInst.result = nextValueId();
            mulInst.op1    = curLoad.result;
            mulInst.op2    = loadLocal.result;
            mulInst.type   = wr.type;
            emit(mulInst);

            Instruction cmpx;
            cmpx.opcode = Opcode::AtomicCmpXchg;
            cmpx.result = nextValueId();
            cmpx.op1    = wr.outerPtr;
            cmpx.op2    = curLoad.result;
            cmpx.op3    = mulInst.result;
            cmpx.type   = checker_.context().getBool();
            emit(cmpx);

            Instruction brCas;
            brCas.opcode = Opcode::Branch;
            brCas.op1    = cmpx.result;
            brCas.label1 = casCont->label;
            brCas.label2 = casLoop->label;
            emit(brCas);

            setInsertBlock(casCont);
            continue;
        }

        int64_t rmwOp = 0;
        switch (wr.op) {
            case ast::ReductionOp::Add:    rmwOp = 0; break;
            case ast::ReductionOp::Min:    rmwOp = 3; break;
            case ast::ReductionOp::Max:    rmwOp = 4; break;
            case ast::ReductionOp::BitAnd: rmwOp = 5; break;
            case ast::ReductionOp::BitOr:  rmwOp = 6; break;
            case ast::ReductionOp::BitXor: rmwOp = 7; break;
            default:                       rmwOp = 0; break;
        }

        Instruction atomRmw;
        atomRmw.opcode = Opcode::AtomicRMW;
        atomRmw.result = InvalidValue;
        atomRmw.op1    = wr.outerPtr;
        atomRmw.op2    = loadLocal.result;
        atomRmw.type   = wr.type;
        atomRmw.intImm = rmwOp;
        emit(atomRmw);
    }

    Instruction ret;
    ret.opcode = Opcode::Return;
    emit(ret);

    popScope();
    scopeStack_ = std::move(savedScopeStack);

    // 5. Restore parent function and emit ParallelFor
    currentFunction_ = parentFn;
    setInsertBlock(parentBlock);

    Instruction pfor;
    pfor.opcode = Opcode::ParallelFor;
    pfor.op1    = startVal;
    pfor.op2    = endVal;
    pfor.op3    = grainVal;
    if (ctxVal != InvalidValue) {
        pfor.callArgs.push_back(ctxVal);
    }
    pfor.strImm = workerName;
    emit(pfor);
}

void IRBuilder::lowerTaskScope(TaskScopeStmt& stmt) {
    if (!currentBlock_ || isCurrentBlockTerminated()) return;

    Instruction beginInst;
    beginInst.opcode = Opcode::TaskScopeBegin;
    beginInst.result = nextValueId();
    beginInst.type   = checker_.context().getI64();
    emit(beginInst);

    taskScopeStack_.push_back(beginInst.result);

    if (stmt.body) {
        lowerBlock(*stmt.body);
    }

    taskScopeStack_.pop_back();

    Instruction endInst;
    endInst.opcode = Opcode::TaskScopeEnd;
    endInst.op1    = beginInst.result;
    emit(endInst);
}

void IRBuilder::lowerSpawn(SpawnStmt& stmt) {
    if (!currentBlock_ || isCurrentBlockTerminated()) return;
    ValueId scopeHandle = !taskScopeStack_.empty() ? taskScopeStack_.back() : InvalidValue;

    // Collect captured identifiers
    std::unordered_set<std::string> refIds;
    if (stmt.body) {
        collectReferencedIdentifiers(*stmt.body, refIds);
    }

    std::vector<std::string> capturedNames;
    std::vector<Binding> capturedBindings;
    for (const auto& name : refIds) {
        const Binding* b = lookupBinding(name);
        if (b) {
            capturedNames.push_back(name);
            capturedBindings.push_back(*b);
        }
    }

    ValueId ctxVal = InvalidValue;
    auto ptrTy = checker_.context().getReference(checker_.context().getI8());
    if (!capturedNames.empty()) {
        size_t slots = capturedNames.size();
        Instruction allocaCtx;
        allocaCtx.opcode = Opcode::Alloca;
        allocaCtx.result = nextValueId();
        allocaCtx.type   = checker_.context().getArray(ptrTy, static_cast<int64_t>(slots));
        emit(allocaCtx);
        ctxVal = allocaCtx.result;

        for (size_t k = 0; k < capturedNames.size(); ++k) {
            Instruction gepPtr;
            gepPtr.opcode = Opcode::GetArrayElemPtr;
            gepPtr.result = nextValueId();
            gepPtr.op1    = allocaCtx.result;
            gepPtr.op2    = emitIntConstant(static_cast<int64_t>(k), checker_.context().getI64());
            gepPtr.type   = ptrTy;
            emit(gepPtr);

            Instruction storePtr;
            storePtr.opcode = Opcode::Store;
            storePtr.op1    = gepPtr.result;
            storePtr.op2    = capturedBindings[k].ptr;
            emit(storePtr);
        }
    }

    // Outlined task worker function
    std::string taskWorkerName = nextLabel("__tamarin_task_worker");
    auto* parentFn = currentFunction_;
    auto* parentBlock = currentBlock_;
    auto savedScopeStack = std::move(scopeStack_);
    scopeStack_.clear();

    auto taskWorkerFn = currentModule_->createFunction(taskWorkerName, checker_.context().getVoid());
    ValueId pCtx = nextValueId();
    taskWorkerFn->params.push_back(IRParam{"ctx", ptrTy, pCtx});

    currentFunction_ = taskWorkerFn;
    auto entryBlock = taskWorkerFn->createBlock("entry");
    setInsertBlock(entryBlock);
    pushScope();

    if (!capturedNames.empty()) {
        for (size_t k = 0; k < capturedNames.size(); ++k) {
            Instruction gepPtr;
            gepPtr.opcode = Opcode::GetArrayElemPtr;
            gepPtr.result = nextValueId();
            gepPtr.op1    = pCtx;
            gepPtr.op2    = emitIntConstant(static_cast<int64_t>(k), checker_.context().getI64());
            gepPtr.type   = ptrTy;
            emit(gepPtr);

            Instruction loadPtr;
            loadPtr.opcode = Opcode::Load;
            loadPtr.result = nextValueId();
            loadPtr.op1    = gepPtr.result;
            loadPtr.type   = ptrTy;
            emit(loadPtr);

            Binding b;
            b.ptr = loadPtr.result;
            b.type = capturedBindings[k].type;
            b.isConst = capturedBindings[k].isConst;
            b.isParameter = capturedBindings[k].isParameter;
            b.constantArrayLen = capturedBindings[k].constantArrayLen;
            if (capturedBindings[k].constantArrayLen >= 0) {
                b.arrayLenValueId = emitIntConstant(capturedBindings[k].constantArrayLen, checker_.context().getI64());
            } else if (capturedBindings[k].type && capturedBindings[k].type->isArray() && capturedBindings[k].type->arraySize > 0) {
                b.arrayLenValueId = emitIntConstant(capturedBindings[k].type->arraySize, checker_.context().getI64());
            }
            setBinding(capturedNames[k], std::move(b));
        }
    }

    if (stmt.body) {
        lowerStmt(*stmt.body);
    }

    if (currentBlock_ && !isCurrentBlockTerminated()) {
        Instruction ret;
        ret.opcode = Opcode::Return;
        emit(ret);
    }

    popScope();
    scopeStack_ = std::move(savedScopeStack);

    currentFunction_ = parentFn;
    setInsertBlock(parentBlock);

    Instruction spawnInst;
    spawnInst.opcode = Opcode::TaskSpawn;
    spawnInst.op1    = scopeHandle;
    spawnInst.op2    = ctxVal;
    spawnInst.strImm = taskWorkerName;
    emit(spawnInst);
}

void IRBuilder::lowerSelect(SelectStmt& stmt) {
    if (!currentBlock_ || isCurrentBlockTerminated()) return;

    auto* endBlock = currentFunction_->createBlock(nextLabel("select_end"));

    if (stmt.hasDefault) {
        // Non-blocking select: evaluate each case condition. If true, run body and jump to end.
        // If all conditions fail, run default body and jump to end.
        for (size_t i = 0; i < stmt.cases.size(); ++i) {
            auto& sc = stmt.cases[i];
            auto* nextCheck = currentFunction_->createBlock(nextLabel("select_next"));
            auto* caseBodyBlock = currentFunction_->createBlock(nextLabel("select_body"));

            ValueId condVal = lowerExpr(*sc.condition);
            auto condTy = checker_.getExprType(sc.condition.get());
            if (condTy && !condTy->isBool()) {
                condVal = emitCastIfNeeded(condVal, condTy, checker_.context().getBool());
            }

            Instruction br;
            br.opcode = Opcode::Branch;
            br.op1 = condVal;
            br.label1 = caseBodyBlock->label;
            br.label2 = nextCheck->label;
            emit(br);

            setInsertBlock(caseBodyBlock);
            pushScope();
            for (auto& s : sc.body) {
                if (!currentBlock_ || isCurrentBlockTerminated()) break;
                lowerStmt(*s);
            }
            popScope();
            if (currentBlock_ && !isCurrentBlockTerminated()) {
                Instruction jmp;
                jmp.opcode = Opcode::Jump;
                jmp.label1 = endBlock->label;
                emit(jmp);
            }

            setInsertBlock(nextCheck);
        }

        // Default body
        pushScope();
        for (auto& s : stmt.defaultBody) {
            if (!currentBlock_ || isCurrentBlockTerminated()) break;
            lowerStmt(*s);
        }
        popScope();
        if (currentBlock_ && !isCurrentBlockTerminated()) {
            Instruction jmp;
            jmp.opcode = Opcode::Jump;
            jmp.label1 = endBlock->label;
            emit(jmp);
        }
    } else {
        // Blocking select: loop until at least one case condition succeeds!
        auto* loopHeader = currentFunction_->createBlock(nextLabel("select_loop_header"));
        Instruction toHeader;
        toHeader.opcode = Opcode::Jump;
        toHeader.label1 = loopHeader->label;
        emit(toHeader);

        setInsertBlock(loopHeader);

        for (size_t i = 0; i < stmt.cases.size(); ++i) {
            auto& sc = stmt.cases[i];
            auto* nextCheck = currentFunction_->createBlock(nextLabel("select_next"));
            auto* caseBodyBlock = currentFunction_->createBlock(nextLabel("select_body"));

            ValueId condVal = lowerExpr(*sc.condition);
            auto condTy = checker_.getExprType(sc.condition.get());
            if (condTy && !condTy->isBool()) {
                condVal = emitCastIfNeeded(condVal, condTy, checker_.context().getBool());
            }

            Instruction br;
            br.opcode = Opcode::Branch;
            br.op1 = condVal;
            br.label1 = caseBodyBlock->label;
            br.label2 = nextCheck->label;
            emit(br);

            setInsertBlock(caseBodyBlock);
            pushScope();
            for (auto& s : sc.body) {
                if (!currentBlock_ || isCurrentBlockTerminated()) break;
                lowerStmt(*s);
            }
            popScope();
            if (currentBlock_ && !isCurrentBlockTerminated()) {
                Instruction jmp;
                jmp.opcode = Opcode::Jump;
                jmp.label1 = endBlock->label;
                emit(jmp);
            }

            setInsertBlock(nextCheck);
        }

        // If none matched, yield execution and loop back to header
        Instruction callYield;
        callYield.opcode = Opcode::Call;
        callYield.strImm = "tamarin_yield";
        callYield.type = checker_.context().getVoid();
        emit(callYield);

        Instruction jmpBack;
        jmpBack.opcode = Opcode::Jump;
        jmpBack.label1 = loopHeader->label;
        emit(jmpBack);
    }

    setInsertBlock(endBlock);
}

// ── Expression Lowering ─────────────────────────────────────────────────────

ValueId IRBuilder::lowerExpr(Expr& expr) {
    switch (expr.kind) {
        case ExprKind::IntLiteral: {
            auto& lit = static_cast<IntLiteralExpr&>(expr);
            Instruction inst;
            inst.opcode = Opcode::IConst;
            inst.result = nextValueId();
            inst.type   = (lit.value > INT32_MAX || lit.value < INT32_MIN)
                              ? checker_.context().getI64()
                              : checker_.context().getI32();
            inst.intImm = lit.value;
            emit(inst);
            return inst.result;
        }
        case ExprKind::FloatLiteral: {
            auto& lit = static_cast<FloatLiteralExpr&>(expr);
            Instruction inst;
            inst.opcode   = Opcode::FConst;
            inst.result   = nextValueId();
            inst.type     = lit.isSinglePrecision ? checker_.context().getF32() : checker_.context().getF64();
            inst.floatImm = lit.value;
            emit(inst);
            return inst.result;
        }
        case ExprKind::BoolLiteral: {
            auto& lit = static_cast<BoolLiteralExpr&>(expr);
            Instruction inst;
            inst.opcode = Opcode::BoolConst;
            inst.result = nextValueId();
            inst.type   = checker_.context().getBool();
            inst.intImm = lit.value ? 1 : 0;
            emit(inst);
            return inst.result;
        }
        case ExprKind::StringLiteral: {
            auto& strLit = static_cast<StringLiteralExpr&>(expr);
            int64_t strIdx = registerStringLiteral(strLit.value);
            Instruction inst;
            inst.opcode = Opcode::StringConst;
            inst.result = nextValueId();
            inst.intImm = strIdx;
            inst.op1    = static_cast<ValueId>(strLit.value.size()); // length
            inst.type   = checker_.context().getArrayView(checker_.context().getU8(), /*isConst=*/true);
            emit(inst);
            return inst.result;
        }
        case ExprKind::Identifier: {
            auto& id = static_cast<IdentifierExpr&>(expr);
            auto* b = lookupBinding(id.name);

            if (b) {
                auto ptr = b->ptr;
                auto ty  = b->type;

                // If it's a reference, load the reference pointer first
                if (ty && ty->isReference()) {
                    Instruction loadRef;
                    loadRef.opcode = Opcode::Load;
                    loadRef.result = nextValueId();
                    loadRef.op1    = ptr;
                    loadRef.type   = ty; // ptr
                    emit(loadRef);

                    if (!ty->isPointer) {
                        Instruction loadVal;
                        loadVal.opcode = Opcode::Load;
                        loadVal.result = nextValueId();
                        loadVal.op1    = loadRef.result;
                        loadVal.type   = ty->elementType ? ty->elementType : checker_.context().getI32();
                        emit(loadVal);
                        return loadVal.result;
                    }
                    return loadRef.result;
                }

                // If it's an array, we pass it as a view {ptr, len}
                if (ty && ty->isArray()) {
                    auto lenVal = b->arrayLenValueId.value_or(InvalidValue);
                    Instruction mkView;
                    mkView.opcode = Opcode::MakeView;
                    mkView.result = nextValueId();
                    mkView.op1    = ptr;
                    mkView.op2    = lenVal;
                    mkView.type   = checker_.context().getArrayView(ty->elementType);
                    emit(mkView);
                    return mkView.result;
                }

                // Normal scalar load
                Instruction load;
                load.opcode = Opcode::Load;
                load.result = nextValueId();
                load.op1    = ptr;
                load.type   = ty;
                emit(load);
                return load.result;
            }

            // Unqualified member access on 'this'
            if (currentRecord_ && currentRecord_->findField(id.name)) {
                int fIdx = currentRecord_->getFieldIndex(id.name);
                auto f = currentRecord_->findField(id.name);
                auto fTy = f ? f->type : checker_.context().getI32();

                Instruction gfp;
                gfp.opcode = Opcode::GetFieldPtr;
                gfp.result = nextValueId();
                gfp.op1    = currentThisValue_;
                gfp.intImm = fIdx;
                gfp.type   = fTy;
                gfp.strImm = currentRecord_->structName;
                emit(gfp);

                if (fTy && fTy->isArray()) {
                    auto lenVal = emitIntConstant(fTy->arraySize, checker_.context().getI32());
                    Instruction mkView;
                    mkView.opcode = Opcode::MakeView;
                    mkView.result = nextValueId();
                    mkView.op1    = gfp.result;
                    mkView.op2    = lenVal;
                    mkView.type   = checker_.context().getArrayView(fTy->elementType);
                    emit(mkView);
                    return mkView.result;
                }

                Instruction load;
                load.opcode = Opcode::Load;
                load.result = nextValueId();
                load.op1    = gfp.result;
                load.type   = fTy;
                emit(load);
                return load.result;
            }

            // Unit enum variant literal (e.g. Color::Red, Shape::Point, Option::None)
            if (id.name.find("::") != std::string::npos) {
                size_t colon = id.name.find("::");
                std::string eName = id.name.substr(0, colon);
                std::string vName = id.name.substr(colon + 2);
                auto exprTy = checker_.getExprType(&expr);
                if (exprTy && exprTy->isEnum()) {
                    if (exprTy->isNicheOptimized) {
                        Instruction iconst;
                        iconst.opcode = Opcode::IConst;
                        iconst.result = nextValueId();
                        iconst.type   = exprTy;
                        iconst.intImm = 0;
                        emit(iconst);
                        return iconst.result;
                    }
                    int varIdx = exprTy->getVariantIndex(vName);
                    if (varIdx >= 0) {
                        Instruction allocaInst;
                        allocaInst.opcode = Opcode::Alloca;
                        allocaInst.result = nextValueId();
                        allocaInst.type   = exprTy;
                        emit(allocaInst);

                        Instruction tagSet;
                        tagSet.opcode = Opcode::TagSet;
                        tagSet.op1    = allocaInst.result;
                        tagSet.type   = exprTy;
                        tagSet.intImm = varIdx;
                        emit(tagSet);

                        Instruction load;
                        load.opcode = Opcode::Load;
                        load.result = nextValueId();
                        load.op1    = allocaInst.result;
                        load.type   = exprTy;
                        emit(load);
                        return load.result;
                    }
                }
            }

            auto exprTy = checker_.getExprType(&expr);
            if (exprTy && exprTy->isFunction()) {
                std::string targetFn = id.name;
                std::string thunkName = "__tamarin_thunk_" + id.name;
                if (createdThunks_.insert(thunkName).second) {
                    auto* parentFn = currentFunction_;
                    auto* parentBlock = currentBlock_;
                    auto savedScopeStack = std::move(scopeStack_);
                    auto savedCleanupScopes = std::move(cleanupScopes_);
                    scopeStack_.clear();
                    cleanupScopes_.clear();

                    const sema::Type* retTy = exprTy->returnType ? exprTy->returnType : checker_.context().getVoid();
                    auto thunkFn = currentModule_->createFunction(thunkName, retTy);

                    // Param 0: hidden __env (ptr)
                    ValueId envVal = nextValueId();
                    const sema::Type* envPtrTy = checker_.context().getReference(checker_.context().getI8(), false);
                    thunkFn->params.push_back(IRParam{"__env", envPtrTy, envVal});

                    // Forward parameters
                    std::vector<ValueId> forwardArgs;
                    for (size_t i = 0; i < exprTy->paramTypes.size(); ++i) {
                        ValueId pVal = nextValueId();
                        auto pTy = exprTy->paramTypes[i];
                        thunkFn->params.push_back(IRParam{"p" + std::to_string(i), pTy, pVal});
                        forwardArgs.push_back(pVal);
                    }

                    currentFunction_ = thunkFn;
                    auto entryBlock = thunkFn->createBlock("entry");
                    setInsertBlock(entryBlock);

                    Instruction callInst;
                    callInst.opcode = Opcode::Call;
                    callInst.result = (retTy && !retTy->isVoid()) ? nextValueId() : InvalidValue;
                    callInst.type = retTy;
                    callInst.strImm = targetFn;
                    callInst.callArgs = std::move(forwardArgs);
                    emit(callInst);

                    Instruction retInst;
                    retInst.opcode = Opcode::Return;
                    retInst.op1 = callInst.result;
                    emit(retInst);

                    currentFunction_ = parentFn;
                    setInsertBlock(parentBlock);
                    scopeStack_ = std::move(savedScopeStack);
                    cleanupScopes_ = std::move(savedCleanupScopes);
                }

                Instruction fnInst;
                fnInst.opcode = Opcode::FunctionPtr;
                fnInst.result = nextValueId();
                fnInst.type   = exprTy;
                fnInst.strImm = thunkName;
                emit(fnInst);
                return fnInst.result;
            }

            return InvalidValue;
        }
        case ExprKind::Binary: {
            auto& bin = static_cast<BinaryExpr&>(expr);

            if (!bin.overloadedMethod.empty()) {
                auto objPtr = getStructPointer(*bin.lhs);
                auto lhsTy = checker_.getExprType(bin.lhs.get());
                if (lhsTy && lhsTy->isReference()) lhsTy = lhsTy->elementType;
                auto retTy = checker_.getExprType(&expr);
                const sema::MethodType* m = lhsTy ? lhsTy->findMethod(bin.overloadedMethod) : nullptr;

                std::vector<ValueId> args;
                args.push_back(objPtr); // this
                ValueId rhsVal = InvalidValue;
                const sema::Type* expectedParamTy = (m && !m->paramTypes.empty()) ? m->paramTypes[0] : nullptr;
                if (expectedParamTy && expectedParamTy->isReference()) {
                    rhsVal = getStructPointer(*bin.rhs);
                } else {
                    rhsVal = lowerExpr(*bin.rhs);
                    auto rhsTy = checker_.getExprType(bin.rhs.get());
                    if (expectedParamTy) rhsVal = emitCastIfNeeded(rhsVal, rhsTy, expectedParamTy);
                }
                args.push_back(rhsVal);

                std::string recName = (m && !m->definingClass.empty()) ? m->definingClass : (lhsTy ? lhsTy->structName : "");
                Instruction call;
                call.opcode = Opcode::Call;
                call.result = (retTy && !retTy->isVoid()) ? nextValueId() : InvalidValue;
                call.type   = retTy;
                call.strImm = sema::mangleMethodName(recName, bin.overloadedMethod);
                call.callArgs = std::move(args);
                emit(call);
                return call.result;
            }

            // Short-circuit for logical AND (&&)
            if (bin.op == BinaryOp::And) {
                Instruction resultAlloca;
                resultAlloca.opcode = Opcode::Alloca;
                resultAlloca.result = nextValueId();
                resultAlloca.type   = checker_.context().getBool();
                emit(resultAlloca);

                auto lhsVal = lowerExpr(*bin.lhs);
                Instruction storeLhs;
                storeLhs.opcode = Opcode::Store;
                storeLhs.op1    = resultAlloca.result;
                storeLhs.op2    = lhsVal;
                emit(storeLhs);

                auto rhsBlock = currentFunction_->createBlock(nextLabel("land_rhs"));
                auto mergeBlock = currentFunction_->createBlock(nextLabel("land_end"));

                Instruction br;
                br.opcode = Opcode::Branch;
                br.op1    = lhsVal;
                br.label1 = rhsBlock->label;
                br.label2 = mergeBlock->label;
                emit(br);

                setInsertBlock(rhsBlock);
                auto rhsVal = lowerExpr(*bin.rhs);
                Instruction storeRhs;
                storeRhs.opcode = Opcode::Store;
                storeRhs.op1    = resultAlloca.result;
                storeRhs.op2    = rhsVal;
                emit(storeRhs);
                if (currentBlock_ && !isCurrentBlockTerminated()) {
                    Instruction jmp;
                    jmp.opcode = Opcode::Jump;
                    jmp.label1 = mergeBlock->label;
                    emit(jmp);
                }

                setInsertBlock(mergeBlock);
                Instruction loadResult;
                loadResult.opcode = Opcode::Load;
                loadResult.result = nextValueId();
                loadResult.op1    = resultAlloca.result;
                loadResult.type   = checker_.context().getBool();
                emit(loadResult);
                return loadResult.result;
            }

            // Short-circuit for logical OR (||)
            if (bin.op == BinaryOp::Or) {
                Instruction resultAlloca;
                resultAlloca.opcode = Opcode::Alloca;
                resultAlloca.result = nextValueId();
                resultAlloca.type   = checker_.context().getBool();
                emit(resultAlloca);

                auto lhsVal = lowerExpr(*bin.lhs);
                Instruction storeLhs;
                storeLhs.opcode = Opcode::Store;
                storeLhs.op1    = resultAlloca.result;
                storeLhs.op2    = lhsVal;
                emit(storeLhs);

                auto rhsBlock = currentFunction_->createBlock(nextLabel("lor_rhs"));
                auto mergeBlock = currentFunction_->createBlock(nextLabel("lor_end"));

                Instruction br;
                br.opcode = Opcode::Branch;
                br.op1    = lhsVal;
                br.label1 = mergeBlock->label;
                br.label2 = rhsBlock->label;
                emit(br);

                setInsertBlock(rhsBlock);
                auto rhsVal = lowerExpr(*bin.rhs);
                Instruction storeRhs;
                storeRhs.opcode = Opcode::Store;
                storeRhs.op1    = resultAlloca.result;
                storeRhs.op2    = rhsVal;
                emit(storeRhs);
                if (currentBlock_ && !isCurrentBlockTerminated()) {
                    Instruction jmp;
                    jmp.opcode = Opcode::Jump;
                    jmp.label1 = mergeBlock->label;
                    emit(jmp);
                }

                setInsertBlock(mergeBlock);
                Instruction loadResult;
                loadResult.opcode = Opcode::Load;
                loadResult.result = nextValueId();
                loadResult.op1    = resultAlloca.result;
                loadResult.type   = checker_.context().getBool();
                emit(loadResult);
                return loadResult.result;
            }

            // Non-short-circuit binary operations
            auto lhs = lowerExpr(*bin.lhs);
            auto rhs = lowerExpr(*bin.rhs);
            auto lhsTy = checker_.getExprType(bin.lhs.get());
            auto rhsTy = checker_.getExprType(bin.rhs.get());
            if (lhsTy && lhsTy->isReference()) lhsTy = lhsTy->elementType;
            if (rhsTy && rhsTy->isReference()) rhsTy = rhsTy->elementType;
            auto ty = checker_.getExprType(&expr);
            if (ty && ty->isReference()) ty = ty->elementType;

            if (bin.op == BinaryOp::Lt || bin.op == BinaryOp::LtEq ||
                bin.op == BinaryOp::Gt || bin.op == BinaryOp::GtEq ||
                bin.op == BinaryOp::EqEq || bin.op == BinaryOp::NotEq) {
                const sema::Type* commonTy = lhsTy;
                if (lhsTy && rhsTy && *lhsTy != *rhsTy) {
                    commonTy = checker_.context().getCommonArithmeticType(lhsTy, rhsTy);
                    lhs = emitCastIfNeeded(lhs, lhsTy, commonTy);
                    rhs = emitCastIfNeeded(rhs, rhsTy, commonTy);
                    lhsTy = commonTy;
                    rhsTy = commonTy;
                }
            } else {
                if (lhsTy && lhsTy->isSIMDVector() && rhsTy && rhsTy->isNumeric()) {
                    Instruction splat;
                    splat.opcode = Opcode::VecSplat;
                    splat.result = nextValueId();
                    splat.op1    = rhs;
                    splat.type   = lhsTy;
                    emit(splat);
                    rhs = splat.result;
                    rhsTy = lhsTy;
                } else if (rhsTy && rhsTy->isSIMDVector() && lhsTy && lhsTy->isNumeric()) {
                    Instruction splat;
                    splat.opcode = Opcode::VecSplat;
                    splat.result = nextValueId();
                    splat.op1    = lhs;
                    splat.type   = rhsTy;
                    emit(splat);
                    lhs = splat.result;
                    lhsTy = rhsTy;
                } else if (ty && !ty->isSIMDVector()) {
                    lhs = emitCastIfNeeded(lhs, lhsTy, ty);
                    rhs = emitCastIfNeeded(rhs, rhsTy, ty);
                    lhsTy = ty;
                    rhsTy = ty;
                }
            }

            bool isFloat = (lhsTy && lhsTy->isSIMDVector() && lhsTy->elementType && lhsTy->elementType->isFloat()) ||
                           (lhsTy && lhsTy->isFloat());
            bool isUnsigned = lhsTy && lhsTy->isUnsignedInteger();

            Opcode op = Opcode::Add;
            switch (bin.op) {
                case BinaryOp::Add:    op = isFloat ? Opcode::FAdd : Opcode::Add; break;
                case BinaryOp::Sub:    op = isFloat ? Opcode::FSub : Opcode::Sub; break;
                case BinaryOp::Mul:    op = isFloat ? Opcode::FMul : Opcode::Mul; break;
                case BinaryOp::Div:    op = isFloat ? Opcode::FDiv : (isUnsigned ? Opcode::UDiv : Opcode::SDiv); break;
                case BinaryOp::Mod:    op = isUnsigned ? Opcode::URem : Opcode::SRem; break;
                case BinaryOp::Lt:     op = isFloat ? Opcode::FCmpLt : (isUnsigned ? Opcode::ICmpULt : Opcode::ICmpSLt); break;
                case BinaryOp::Gt:     op = isFloat ? Opcode::FCmpGt : (isUnsigned ? Opcode::ICmpUGt : Opcode::ICmpSGt); break;
                case BinaryOp::LtEq:   op = isFloat ? Opcode::FCmpLe : (isUnsigned ? Opcode::ICmpULe : Opcode::ICmpSLe); break;
                case BinaryOp::GtEq:   op = isFloat ? Opcode::FCmpGe : (isUnsigned ? Opcode::ICmpUGe : Opcode::ICmpSGe); break;
                case BinaryOp::EqEq:   op = isFloat ? Opcode::FCmpEq : Opcode::ICmpEq; break;
                case BinaryOp::NotEq:  op = isFloat ? Opcode::FCmpNe : Opcode::ICmpNe; break;
                case BinaryOp::BitAnd: op = Opcode::And; break;
                case BinaryOp::BitOr:  op = Opcode::Or; break;
                case BinaryOp::BitXor: op = Opcode::Xor; break;
                case BinaryOp::Shl:    op = Opcode::Shl; break;
                case BinaryOp::Shr:    op = isUnsigned ? Opcode::LShr : Opcode::AShr; break;
                default: break;
            }

            Instruction inst;
            inst.opcode = op;
            inst.result = nextValueId();
            inst.op1    = lhs;
            inst.op2    = rhs;
            inst.type   = ty ? ty : checker_.context().getI32();
            emit(inst);
            return inst.result;
        }
        case ExprKind::Unary: {
            auto& un = static_cast<UnaryExpr&>(expr);

            if (!un.overloadedMethod.empty()) {
                auto objPtr = getStructPointer(*un.operand);
                auto opTy = checker_.getExprType(un.operand.get());
                if (opTy && opTy->isReference()) opTy = opTy->elementType;
                auto retTy = checker_.getExprType(&expr);
                const sema::MethodType* m = opTy ? opTy->findMethod(un.overloadedMethod) : nullptr;

                std::vector<ValueId> args;
                args.push_back(objPtr); // this

                std::string recName = (m && !m->definingClass.empty()) ? m->definingClass : (opTy ? opTy->structName : "");
                Instruction call;
                call.opcode = Opcode::Call;
                call.result = (retTy && !retTy->isVoid()) ? nextValueId() : InvalidValue;
                call.type   = retTy;
                call.strImm = sema::mangleMethodName(recName, un.overloadedMethod, 0);
                call.callArgs = std::move(args);
                emit(call);
                return call.result;
            }

            auto opTy = checker_.getExprType(un.operand.get());

            switch (un.op) {
                case UnaryOp::Neg: {
                    auto operand = lowerExpr(*un.operand);
                    if (opTy && opTy->isFloat()) {
                        Instruction zero;
                        zero.opcode   = Opcode::FConst;
                        zero.result   = nextValueId();
                        zero.floatImm = 0.0;
                        zero.type     = opTy;
                        emit(zero);

                        Instruction sub;
                        sub.opcode = Opcode::FSub;
                        sub.result = nextValueId();
                        sub.op1    = zero.result;
                        sub.op2    = operand;
                        sub.type   = opTy;
                        emit(sub);
                        return sub.result;
                    } else {
                        Instruction zero;
                        zero.opcode = Opcode::IConst;
                        zero.result = nextValueId();
                        zero.intImm = 0;
                        zero.type   = opTy ? opTy : checker_.context().getI32();
                        emit(zero);

                        Instruction sub;
                        sub.opcode = Opcode::Sub;
                        sub.result = nextValueId();
                        sub.op1    = zero.result;
                        sub.op2    = operand;
                        sub.type   = zero.type;
                        emit(sub);
                        return sub.result;
                    }
                }
                case UnaryOp::PreInc:
                case UnaryOp::PostInc: {
                    if (un.operand->kind == ExprKind::Identifier) {
                        auto& id = static_cast<IdentifierExpr&>(*un.operand);
                        auto* b = lookupBinding(id.name);
                        auto ptr = b ? b->ptr : InvalidValue;
                        auto ty  = b ? b->type : nullptr;

                        Instruction load;
                        load.opcode = Opcode::Load;
                        load.result = nextValueId();
                        load.op1    = ptr;
                        load.type   = ty;
                        emit(load);

                        Instruction one;
                        one.opcode = Opcode::IConst;
                        one.result = nextValueId();
                        one.intImm = 1;
                        one.type   = ty;
                        emit(one);

                        Instruction add;
                        add.opcode = (ty && ty->isFloat()) ? Opcode::FAdd : Opcode::Add;
                        add.result = nextValueId();
                        add.op1    = load.result;
                        add.op2    = one.result;
                        add.type   = ty;
                        emit(add);

                        Instruction store;
                        store.opcode = Opcode::Store;
                        store.op1    = ptr;
                        store.op2    = add.result;
                        emit(store);

                        return (un.op == UnaryOp::PreInc) ? add.result : load.result;
                    }
                    return InvalidValue;
                }
                case UnaryOp::PreDec:
                case UnaryOp::PostDec: {
                    if (un.operand->kind == ExprKind::Identifier) {
                        auto& id = static_cast<IdentifierExpr&>(*un.operand);
                        auto* b = lookupBinding(id.name);
                        auto ptr = b ? b->ptr : InvalidValue;
                        auto ty  = b ? b->type : nullptr;

                        Instruction load;
                        load.opcode = Opcode::Load;
                        load.result = nextValueId();
                        load.op1    = ptr;
                        load.type   = ty;
                        emit(load);

                        Instruction one;
                        one.opcode = Opcode::IConst;
                        one.result = nextValueId();
                        one.intImm = 1;
                        one.type   = ty;
                        emit(one);

                        Instruction sub;
                        sub.opcode = (ty && ty->isFloat()) ? Opcode::FSub : Opcode::Sub;
                        sub.result = nextValueId();
                        sub.op1    = load.result;
                        sub.op2    = one.result;
                        sub.type   = ty;
                        emit(sub);

                        Instruction store;
                        store.opcode = Opcode::Store;
                        store.op1    = ptr;
                        store.op2    = sub.result;
                        emit(store);

                        return (un.op == UnaryOp::PreDec) ? sub.result : load.result;
                    }
                    return InvalidValue;
                }
                case UnaryOp::Not: {
                    auto operand = lowerExpr(*un.operand);
                    Instruction inst;
                    inst.opcode = Opcode::Xor;
                    inst.result = nextValueId();
                    inst.op1    = operand;

                    Instruction one;
                    one.opcode = Opcode::BoolConst;
                    one.result = nextValueId();
                    one.intImm = 1;
                    one.type   = checker_.context().getBool();
                    emit(one);

                    inst.op2  = one.result;
                    inst.type = checker_.context().getBool();
                    emit(inst);
                    return inst.result;
                }
                case UnaryOp::BitNot: {
                    auto operand = lowerExpr(*un.operand);
                    Instruction minusOne;
                    minusOne.opcode = Opcode::IConst;
                    minusOne.result = nextValueId();
                    minusOne.intImm = -1;
                    minusOne.type   = opTy ? opTy : checker_.context().getI32();
                    emit(minusOne);

                    Instruction inst;
                    inst.opcode = Opcode::Xor;
                    inst.result = nextValueId();
                    inst.op1    = operand;
                    inst.op2    = minusOne.result;
                    inst.type   = minusOne.type;
                    emit(inst);
                    return inst.result;
                }
                case UnaryOp::AddrOf: {
                    if (un.operand->kind == ExprKind::Identifier) {
                        auto& id = static_cast<IdentifierExpr&>(*un.operand);
                        auto* b = lookupBinding(id.name);
                        if (b) {
                            if (b->type && b->type->isReference()) {
                                Instruction load;
                                load.opcode = Opcode::Load;
                                load.result = nextValueId();
                                load.op1    = b->ptr;
                                load.type   = b->type;
                                emit(load);
                                return load.result;
                            }
                            return b->ptr;
                        }
                    } else if (un.operand->kind == ExprKind::Field) {
                        return getStructPointer(*un.operand);
                    } else if (un.operand->kind == ExprKind::Index) {
                        return getStructPointer(*un.operand);
                    }
                    return InvalidValue;
                }
                case UnaryOp::Deref: {
                    auto ptrVal = lowerExpr(*un.operand);
                    auto elemTy = checker_.getExprType(&expr);
                    Instruction load;
                    load.opcode = Opcode::Load;
                    load.result = nextValueId();
                    load.op1    = ptrVal;
                    load.type   = elemTy ? elemTy : checker_.context().getI32();
                    emit(load);
                    return load.result;
                }
                default:
                    return lowerExpr(*un.operand);
            }
        }
        case ExprKind::Assign: {
            auto& a = static_cast<AssignExpr&>(expr);
            auto val = lowerExpr(*a.value);
            auto valTy = checker_.getExprType(a.value.get());

            if (a.target->kind == ExprKind::Identifier) {
                auto& id = static_cast<IdentifierExpr&>(*a.target);
                auto* b = lookupBinding(id.name);
                ValueId ptr = InvalidValue;
                const sema::Type* ty = nullptr;

                if (b) {
                    ptr = b->ptr;
                    ty  = b->type;
                    if (ty && ty->isReference()) {
                        if (!ty->isPointer) {
                            Instruction loadRef;
                            loadRef.opcode = Opcode::Load;
                            loadRef.result = nextValueId();
                            loadRef.op1    = ptr;
                            loadRef.type   = ty;
                            emit(loadRef);
                            ptr = loadRef.result;
                            ty = ty->elementType;
                        }
                    }
                } else if (currentRecord_ && currentRecord_->findField(id.name)) {
                    int fIdx = currentRecord_->getFieldIndex(id.name);
                    auto f = currentRecord_->findField(id.name);
                    ty = f ? f->type : checker_.context().getI32();

                    Instruction gfp;
                    gfp.opcode = Opcode::GetFieldPtr;
                    gfp.result = nextValueId();
                    gfp.op1    = currentThisValue_;
                    gfp.intImm = fIdx;
                    gfp.type   = ty;
                    gfp.strImm = currentRecord_->structName;
                    emit(gfp);
                    ptr = gfp.result;
                }

                val = emitCastIfNeeded(val, valTy, ty);

                if (a.op == AssignOp::Assign) {
                    if (ty && ty->hasNonTrivialDestructor() && ptr != InvalidValue) {
                        markVariableReassigned(id.name, ptr, ty);
                    }
                    Instruction store;
                    store.opcode = Opcode::Store;
                    store.op1    = ptr;
                    store.op2    = val;
                    emit(store);
                    return val;
                } else {
                    Instruction load;
                    load.opcode = Opcode::Load;
                    load.result = nextValueId();
                    load.op1    = ptr;
                    load.type   = ty;
                    emit(load);

                    bool isFloat = ty && ty->isFloat();
                    bool isUnsigned = ty && ty->isUnsignedInteger();

                    Opcode binOp = Opcode::Add;
                    switch (a.op) {
                        case AssignOp::AddAssign: binOp = isFloat ? Opcode::FAdd : Opcode::Add; break;
                        case AssignOp::SubAssign: binOp = isFloat ? Opcode::FSub : Opcode::Sub; break;
                        case AssignOp::MulAssign: binOp = isFloat ? Opcode::FMul : Opcode::Mul; break;
                        case AssignOp::DivAssign: binOp = isFloat ? Opcode::FDiv : (isUnsigned ? Opcode::UDiv : Opcode::SDiv); break;
                        case AssignOp::ModAssign: binOp = isUnsigned ? Opcode::URem : Opcode::SRem; break;
                        case AssignOp::AndAssign: binOp = Opcode::And; break;
                        case AssignOp::OrAssign:  binOp = Opcode::Or; break;
                        case AssignOp::XorAssign: binOp = Opcode::Xor; break;
                        case AssignOp::ShlAssign: binOp = Opcode::Shl; break;
                        case AssignOp::ShrAssign: binOp = isUnsigned ? Opcode::LShr : Opcode::AShr; break;
                        default: break;
                    }

                    Instruction bin;
                    bin.opcode = binOp;
                    bin.result = nextValueId();
                    bin.op1    = load.result;
                    bin.op2    = val;
                    bin.type   = ty;
                    emit(bin);

                    Instruction store;
                    store.opcode = Opcode::Store;
                    store.op1    = ptr;
                    store.op2    = bin.result;
                    emit(store);
                    return bin.result;
                }
            } else if (a.target->kind == ExprKind::Field) {
                auto& fe = static_cast<FieldExpr&>(*a.target);

                // Structure-of-Arrays (SoA) field store
                if (fe.object->kind == ExprKind::Index) {
                    auto& idx = static_cast<IndexExpr&>(*fe.object);
                    auto arrTy = checker_.getExprType(idx.array.get());
                    if (arrTy && arrTy->isArray() && arrTy->elementType &&
                        arrTy->elementType->isRecord() && arrTy->elementType->layoutDirective == 2 /* SoA */) {
                        auto elemRecTy = arrTy->elementType;
                        int fieldIdx = elemRecTy->getFieldIndex(fe.fieldName);
                        auto f = elemRecTy->findField(fe.fieldName);
                        auto fieldTy = f ? f->type : checker_.context().getI32();

                        auto bundlePtr = getStructPointer(*idx.array);
                        auto idxVal = lowerExpr(*idx.index);

                        Instruction soaGfp;
                        soaGfp.opcode = Opcode::SoAGetFieldPtr;
                        soaGfp.result = nextValueId();
                        soaGfp.op1    = bundlePtr;
                        soaGfp.op2    = idxVal;
                        soaGfp.intImm = fieldIdx;
                        soaGfp.strImm = elemRecTy->structName;
                        soaGfp.type   = fieldTy;
                        emit(soaGfp);

                        val = emitCastIfNeeded(val, valTy, fieldTy);

                        if (a.op == AssignOp::Assign) {
                            Instruction store;
                            store.opcode = Opcode::Store;
                            store.op1    = soaGfp.result;
                            store.op2    = val;
                            emit(store);
                            return val;
                        } else {
                            Instruction load;
                            load.opcode = Opcode::Load;
                            load.result = nextValueId();
                            load.op1    = soaGfp.result;
                            load.type   = fieldTy;
                            emit(load);

                            bool isFloat = fieldTy && fieldTy->isFloat();
                            bool isUnsigned = fieldTy && fieldTy->isUnsignedInteger();
                            Opcode binOp = isFloat ? Opcode::FAdd : Opcode::Add;
                            switch (a.op) {
                                case AssignOp::AddAssign: binOp = isFloat ? Opcode::FAdd : Opcode::Add; break;
                                case AssignOp::SubAssign: binOp = isFloat ? Opcode::FSub : Opcode::Sub; break;
                                case AssignOp::MulAssign: binOp = isFloat ? Opcode::FMul : Opcode::Mul; break;
                                case AssignOp::DivAssign: binOp = isFloat ? Opcode::FDiv : (isUnsigned ? Opcode::UDiv : Opcode::SDiv); break;
                                case AssignOp::ModAssign: binOp = isUnsigned ? Opcode::URem : Opcode::SRem; break;
                                case AssignOp::AndAssign: binOp = Opcode::And; break;
                                case AssignOp::OrAssign:  binOp = Opcode::Or; break;
                                case AssignOp::XorAssign: binOp = Opcode::Xor; break;
                                case AssignOp::ShlAssign: binOp = Opcode::Shl; break;
                                case AssignOp::ShrAssign: binOp = isUnsigned ? Opcode::LShr : Opcode::AShr; break;
                                default: break;
                            }
                            Instruction bin;
                            bin.opcode = binOp;
                            bin.result = nextValueId();
                            bin.op1    = load.result;
                            bin.op2    = val;
                            bin.type   = fieldTy;
                            emit(bin);

                            Instruction store;
                            store.opcode = Opcode::Store;
                            store.op1    = soaGfp.result;
                            store.op2    = bin.result;
                            emit(store);
                            return bin.result;
                        }
                    }
                }

                auto structPtr = getStructPointer(*fe.object);
                auto baseTy = checker_.getExprType(fe.object.get());
                if (baseTy && baseTy->isReference()) baseTy = baseTy->elementType;
                int fieldIdx = baseTy ? baseTy->getFieldIndex(fe.fieldName) : 0;
                auto f = baseTy ? baseTy->findField(fe.fieldName) : nullptr;
                auto fieldTy = f ? f->type : checker_.context().getI32();

                Instruction gfp;
                gfp.opcode = Opcode::GetFieldPtr;
                gfp.result = nextValueId();
                gfp.op1    = structPtr;
                gfp.intImm = fieldIdx;
                gfp.type   = fieldTy;
                gfp.strImm = baseTy ? baseTy->structName : "";
                emit(gfp);

                val = emitCastIfNeeded(val, valTy, fieldTy);

                if (a.op == AssignOp::Assign) {
                    Instruction store;
                    store.opcode = Opcode::Store;
                    store.op1    = gfp.result;
                    store.op2    = val;
                    emit(store);
                    return val;
                } else {
                    Instruction load;
                    load.opcode = Opcode::Load;
                    load.result = nextValueId();
                    load.op1    = gfp.result;
                    load.type   = fieldTy;
                    emit(load);

                    bool isFloat = fieldTy && fieldTy->isFloat();
                    bool isUnsigned = fieldTy && fieldTy->isUnsignedInteger();

                    Opcode binOp = Opcode::Add;
                    switch (a.op) {
                        case AssignOp::AddAssign: binOp = isFloat ? Opcode::FAdd : Opcode::Add; break;
                        case AssignOp::SubAssign: binOp = isFloat ? Opcode::FSub : Opcode::Sub; break;
                        case AssignOp::MulAssign: binOp = isFloat ? Opcode::FMul : Opcode::Mul; break;
                        case AssignOp::DivAssign: binOp = isFloat ? Opcode::FDiv : (isUnsigned ? Opcode::UDiv : Opcode::SDiv); break;
                        case AssignOp::ModAssign: binOp = isUnsigned ? Opcode::URem : Opcode::SRem; break;
                        case AssignOp::AndAssign: binOp = Opcode::And; break;
                        case AssignOp::OrAssign:  binOp = Opcode::Or; break;
                        case AssignOp::XorAssign: binOp = Opcode::Xor; break;
                        case AssignOp::ShlAssign: binOp = Opcode::Shl; break;
                        case AssignOp::ShrAssign: binOp = isUnsigned ? Opcode::LShr : Opcode::AShr; break;
                        default: break;
                    }

                    Instruction bin;
                    bin.opcode = binOp;
                    bin.result = nextValueId();
                    bin.op1    = load.result;
                    bin.op2    = val;
                    bin.type   = fieldTy;
                    emit(bin);

                    Instruction store;
                    store.opcode = Opcode::Store;
                    store.op1    = gfp.result;
                    store.op2    = bin.result;
                    emit(store);
                    return bin.result;
                }
            } else if (a.target->kind == ExprKind::Index) {
                auto& idxExpr = static_cast<IndexExpr&>(*a.target);
                auto arrTy = checker_.getExprType(idxExpr.array.get());
                if (arrTy && arrTy->isReference()) arrTy = arrTy->elementType;
                if (arrTy && arrTy->isRecord()) {
                    auto objPtr = getStructPointer(*idxExpr.array);
                    auto idxVal = lowerExpr(*idxExpr.index);
                    std::string recName = arrTy->structName;
                    Instruction callInst;
                    callInst.opcode = Opcode::Call;
                    callInst.result = InvalidValue;
                    callInst.type   = checker_.context().getVoid();
                    callInst.strImm = recName + "_set";
                    callInst.callArgs = { objPtr, idxVal, val };
                    emit(callInst);
                    return val;
                }
                if (arrTy && arrTy->isSlab()) {
                    auto slabPtr = getStructPointer(*idxExpr.array);
                    auto hVal = lowerExpr(*idxExpr.index);
                    auto elemTy = checker_.getExprType(&expr);
                    Instruction getInst;
                    getInst.opcode = Opcode::SlabGet;
                    getInst.result = nextValueId();
                    getInst.type   = checker_.context().getReference(elemTy);
                    getInst.op1    = slabPtr;
                    getInst.op2    = hVal;
                    emit(getInst);

                    val = emitCastIfNeeded(val, valTy, elemTy);

                    if (a.op != AssignOp::Assign) {
                        Instruction load;
                        load.opcode = Opcode::Load;
                        load.result = nextValueId();
                        load.op1    = getInst.result;
                        load.type   = elemTy;
                        emit(load);

                        bool isFloat = elemTy && elemTy->isFloat();
                        bool isUnsigned = elemTy && elemTy->isUnsignedInteger();
                        Opcode binOp = isFloat ? Opcode::FAdd : Opcode::Add;
                        switch (a.op) {
                            case AssignOp::AddAssign: binOp = isFloat ? Opcode::FAdd : Opcode::Add; break;
                            case AssignOp::SubAssign: binOp = isFloat ? Opcode::FSub : Opcode::Sub; break;
                            case AssignOp::MulAssign: binOp = isFloat ? Opcode::FMul : Opcode::Mul; break;
                            case AssignOp::DivAssign: binOp = isFloat ? Opcode::FDiv : (isUnsigned ? Opcode::UDiv : Opcode::SDiv); break;
                            case AssignOp::ModAssign: binOp = isUnsigned ? Opcode::URem : Opcode::SRem; break;
                            case AssignOp::AndAssign: binOp = Opcode::And; break;
                            case AssignOp::OrAssign:  binOp = Opcode::Or; break;
                            case AssignOp::XorAssign: binOp = Opcode::Xor; break;
                            case AssignOp::ShlAssign: binOp = Opcode::Shl; break;
                            case AssignOp::ShrAssign: binOp = isUnsigned ? Opcode::LShr : Opcode::AShr; break;
                            default: break;
                        }
                        Instruction bin;
                        bin.opcode = binOp;
                        bin.result = nextValueId();
                        bin.type   = elemTy;
                        bin.op1    = load.result;
                        bin.op2    = val;
                        emit(bin);
                        val = bin.result;
                    }

                    Instruction store;
                    store.opcode = Opcode::Store;
                    store.op1    = getInst.result;
                    store.op2    = val;
                    emit(store);
                    return val;
                }

                auto arrVal = lowerExpr(*idxExpr.array);
                auto idxVal = lowerExpr(*idxExpr.index);
                auto elemTy = checker_.getExprType(&expr);

                val = emitCastIfNeeded(val, valTy, elemTy);

                // Insert bounds check if known size or view
                if (arrTy && (arrTy->isArrayView() || arrTy->arraySize >= 0)) {
                    Instruction chk;
                    chk.opcode = Opcode::BoundsCheck;
                    chk.op1    = idxVal;
                    chk.op2    = arrVal;
                    emit(chk);
                }

                if (a.op != AssignOp::Assign) {
                    Instruction load;
                    load.opcode = Opcode::ArrayLoad;
                    load.result = nextValueId();
                    load.op1    = arrVal;
                    load.op2    = idxVal;
                    load.type   = elemTy;
                    emit(load);

                    bool isFloat = elemTy && elemTy->isFloat();
                    bool isUnsigned = elemTy && elemTy->isUnsignedInteger();
                    Opcode binOp = isFloat ? Opcode::FAdd : Opcode::Add;
                    switch (a.op) {
                        case AssignOp::AddAssign: binOp = isFloat ? Opcode::FAdd : Opcode::Add; break;
                        case AssignOp::SubAssign: binOp = isFloat ? Opcode::FSub : Opcode::Sub; break;
                        case AssignOp::MulAssign: binOp = isFloat ? Opcode::FMul : Opcode::Mul; break;
                        case AssignOp::DivAssign: binOp = isFloat ? Opcode::FDiv : (isUnsigned ? Opcode::UDiv : Opcode::SDiv); break;
                        case AssignOp::ModAssign: binOp = isUnsigned ? Opcode::URem : Opcode::SRem; break;
                        case AssignOp::AndAssign: binOp = Opcode::And; break;
                        case AssignOp::OrAssign:  binOp = Opcode::Or; break;
                        case AssignOp::XorAssign: binOp = Opcode::Xor; break;
                        case AssignOp::ShlAssign: binOp = Opcode::Shl; break;
                        case AssignOp::ShrAssign: binOp = isUnsigned ? Opcode::LShr : Opcode::AShr; break;
                        default: break;
                    }

                    Instruction bin;
                    bin.opcode = binOp;
                    bin.result = nextValueId();
                    bin.op1    = load.result;
                    bin.op2    = val;
                    bin.type   = elemTy;
                    emit(bin);
                    val = bin.result;
                }

                Instruction store;
                store.opcode = Opcode::ArrayStore;
                store.op1    = arrVal;
                store.op2    = idxVal;
                store.op3    = val;
                emit(store);
                return val;
            } else if (a.target->kind == ExprKind::Unary) {
                auto& un = static_cast<UnaryExpr&>(*a.target);
                if (un.op == UnaryOp::Deref) {
                    auto ptrVal = lowerExpr(*un.operand);
                    auto elemTy = checker_.getExprType(&expr);
                    val = emitCastIfNeeded(val, valTy, elemTy);
                    Instruction store;
                    store.opcode = Opcode::Store;
                    store.op1    = ptrVal;
                    store.op2    = val;
                    emit(store);
                    return val;
                }
            }
            return val;
        }
        case ExprKind::Call: {
            auto& call = static_cast<CallExpr&>(expr);

            if (call.callee == "move" && call.args.size() == 1) {
                if (call.args[0]->kind == ast::ExprKind::Identifier) {
                    const auto& id = static_cast<const ast::IdentifierExpr&>(*call.args[0]);
                    markVariableMoved(id.name);
                    auto* b = lookupBinding(id.name);
                    if (b && b->ptr != InvalidValue) {
                        Instruction load;
                        load.opcode = Opcode::Load;
                        load.result = nextValueId();
                        load.op1    = b->ptr;
                        load.type   = b->type;
                        emit(load);
                        return load.result;
                    }
                }
            }

            // Enum variant constructor with payload (e.g. Shape::Circle(r), Option::Some(x), Result::Ok(x))
            if (call.callee.find("::") != std::string::npos) {
                size_t colon = call.callee.find("::");
                std::string eName = call.callee.substr(0, colon);
                std::string vName = call.callee.substr(colon + 2);
                auto exprTy = checker_.getExprType(&expr);
                if (exprTy && exprTy->isEnum()) {
                    if (exprTy->isNicheOptimized) {
                        if (!call.args.empty()) {
                            return lowerExpr(*call.args[0]);
                        }
                    }
                    int varIdx = exprTy->getVariantIndex(vName);
                    if (varIdx >= 0) {
                        Instruction allocaInst;
                        allocaInst.opcode = Opcode::Alloca;
                        allocaInst.result = nextValueId();
                        allocaInst.type   = exprTy;
                        emit(allocaInst);

                        Instruction tagSet;
                        tagSet.opcode = Opcode::TagSet;
                        tagSet.op1    = allocaInst.result;
                        tagSet.type   = exprTy;
                        tagSet.intImm = varIdx;
                        emit(tagSet);

                        const auto& variant = exprTy->enumVariants[varIdx];
                        int64_t currentOffset = 0;
                        for (size_t i = 0; i < call.args.size(); ++i) {
                            auto val = lowerExpr(*call.args[i]);
                            auto argTy = checker_.getExprType(call.args[i].get());
                            const sema::Type* pTy = (i < variant.payloadTypes.size()) ? variant.payloadTypes[i] : argTy;
                            if (pTy) {
                                val = emitCastIfNeeded(val, argTy, pTy);
                                size_t a = pTy->alignment();
                                if (a == 0) a = 1;
                                currentOffset = (currentOffset + a - 1) & ~(a - 1);
                            }

                            Instruction gfp;
                            gfp.opcode = Opcode::VariantPayloadPtr;
                            gfp.result = nextValueId();
                            gfp.op1    = allocaInst.result;
                            gfp.intImm = static_cast<int64_t>(i);
                            gfp.intImm2 = currentOffset;
                            gfp.type   = exprTy;
                            emit(gfp);

                            Instruction store;
                            store.opcode = Opcode::Store;
                            store.op1    = gfp.result;
                            store.op2    = val;
                            emit(store);

                            if (pTy) {
                                currentOffset += pTy->size();
                            }
                        }

                        Instruction load;
                        load.opcode = Opcode::Load;
                        load.result = nextValueId();
                        load.op1    = allocaInst.result;
                        load.type   = exprTy;
                        emit(load);
                        return load.result;
                    }
                }
            }

            // SIMD vector builtins: reduce_add, dot_product
            if (call.callee == "reduce_add" || call.callee == "__builtin_reduce_add") {
                auto argVal = lowerExpr(*call.args[0]);
                auto retTy = checker_.getExprType(&expr);
                Instruction red;
                red.opcode = Opcode::VecReduceAdd;
                red.result = nextValueId();
                red.op1    = argVal;
                red.type   = retTy;
                emit(red);
                return red.result;
            }

            if (call.callee == "dot_product" || call.callee == "__builtin_dot_product") {
                auto a1Val = lowerExpr(*call.args[0]);
                auto a2Val = lowerExpr(*call.args[1]);
                auto retTy = checker_.getExprType(&expr);
                Instruction dot;
                dot.opcode = Opcode::VecDot;
                dot.result = nextValueId();
                dot.op1    = a1Val;
                dot.op2    = a2Val;
                dot.type   = retTy;
                emit(dot);
                return dot.result;
            }

            // Builtin __benchmark_start() and __benchmark_elapsed_ms()
            if (call.callee == "__benchmark_start") {
                Instruction ci;
                ci.opcode = Opcode::Call;
                ci.result = InvalidValue;
                ci.type   = checker_.context().getVoid();
                ci.strImm = "__tamarin_benchmark_start";
                emit(ci);
                return InvalidValue;
            }
            if (call.callee == "__benchmark_elapsed_ms" || call.callee == "__benchmark_stop") {
                Instruction ci;
                ci.opcode = Opcode::Call;
                ci.result = nextValueId();
                ci.type   = checker_.context().getF64();
                ci.strImm = "__tamarin_benchmark_elapsed_ms";
                emit(ci);
                return ci.result;
            }
            if (call.callee == "__f64_to_bits" || call.callee == "__double_to_bits") {
                auto arg = lowerExpr(*call.args[0]);
                Instruction ci;
                ci.opcode = Opcode::Call;
                ci.result = nextValueId();
                ci.type   = checker_.context().getU64();
                ci.strImm = "__tamarin_f64_to_bits";
                ci.callArgs.push_back(arg);
                emit(ci);
                return ci.result;
            }

            if (call.callee == "ctz" || call.callee == "__builtin_ctz" ||
                call.callee == "clz" || call.callee == "__builtin_clz" ||
                call.callee == "popcount" || call.callee == "__builtin_popcount") {
                auto arg = lowerExpr(*call.args[0]);
                auto argTy = checker_.getExprType(call.args[0].get());
                bool is64 = (argTy && argTy->size() == 8);
                std::string calleeName;
                if (call.callee.find("ctz") != std::string::npos) {
                    calleeName = is64 ? "__tamarin_ctz64" : "__tamarin_ctz32";
                } else if (call.callee.find("clz") != std::string::npos) {
                    calleeName = is64 ? "__tamarin_clz64" : "__tamarin_clz32";
                } else {
                    calleeName = is64 ? "__tamarin_popcount64" : "__tamarin_popcount32";
                }
                Instruction ci;
                ci.opcode = Opcode::Call;
                ci.result = nextValueId();
                ci.type   = checker_.context().getI32();
                ci.strImm = calleeName;
                ci.callArgs.push_back(arg);
                emit(ci);
                return ci.result;
            }

            if (call.callee == "sqrtf" || call.callee == "__builtin_sqrtf" ||
                call.callee == "sqrt" || call.callee == "__builtin_sqrt" ||
                call.callee == "std::math::sqrtf" || call.callee == "math::sqrtf") {
                auto arg = lowerExpr(*call.args[0]);
                auto argTy = checker_.getExprType(call.args[0].get());
                bool is64 = (call.callee == "sqrt" || call.callee == "__builtin_sqrt" || (argTy && argTy->kind == sema::TypeKind::F64));
                std::string calleeName = is64 ? "__tamarin_sqrt" : "__tamarin_sqrtf";
                Instruction ci;
                ci.opcode = Opcode::Call;
                ci.result = nextValueId();
                ci.type   = is64 ? checker_.context().getF64() : checker_.context().getF32();
                ci.strImm = calleeName;
                ci.callArgs.push_back(arg);
                emit(ci);
                return ci.result;
            }

            // Builtin len()
            if (call.callee == "len") {
                auto arg = lowerExpr(*call.args[0]);
                Instruction getLen;
                getLen.opcode = Opcode::GetLen;
                getLen.result = nextValueId();
                getLen.op1    = arg;
                getLen.type   = checker_.context().getUsize();
                emit(getLen);
                return getLen.result;
            }

            // Builtin println()
            if (call.callee == "println" || call.callee == "io::println" || call.callee == "__builtin_println") {
                if (call.args.empty()) {
                    Instruction ci;
                    ci.opcode = Opcode::Call;
                    ci.result = InvalidValue;
                    ci.type   = checker_.context().getVoid();
                    ci.strImm = "__tamarin_println_void";
                    emit(ci);
                    return InvalidValue;
                }
                for (auto& arg : call.args) {
                    auto argVal = lowerExpr(*arg);
                    auto argTy = checker_.getExprType(arg.get());
                    Instruction ci;
                    ci.opcode = Opcode::Call;
                    ci.result = InvalidValue;
                    ci.type   = checker_.context().getVoid();
                    if (!argTy || argTy->isArrayView() || argTy->isArray()) {
                        ci.strImm = "__tamarin_println_str";
                        ci.callArgs.push_back(argVal);
                    } else if (argTy->isBool()) {
                        ci.strImm = "__tamarin_println_bool";
                        ci.callArgs.push_back(argVal);
                    } else if (argTy->isFloat()) {
                        if (argTy->bitWidth() == 32) {
                            Instruction castInst;
                            castInst.opcode = Opcode::Cast;
                            castInst.result = nextValueId();
                            castInst.type   = checker_.context().getF64();
                            castInst.op1    = argVal;
                            emit(castInst);
                            ci.callArgs.push_back(castInst.result);
                        } else {
                            ci.callArgs.push_back(argVal);
                        }
                        ci.strImm = "__tamarin_println_float";
                    } else if (argTy->isInteger() && argTy->bitWidth() == 64) {
                        ci.strImm = "__tamarin_println_i64";
                        ci.callArgs.push_back(argVal);
                    } else {
                        if (argTy->isInteger() && argTy->bitWidth() < 32) {
                            Instruction castInst;
                            castInst.opcode = Opcode::Cast;
                            castInst.result = nextValueId();
                            castInst.type   = checker_.context().getI32();
                            castInst.op1    = argVal;
                            emit(castInst);
                            ci.callArgs.push_back(castInst.result);
                        } else {
                            ci.callArgs.push_back(argVal);
                        }
                        ci.strImm = "__tamarin_println_int";
                    }
                    emit(ci);
                }
                return InvalidValue;
            }

            // Builtin print()
            if (call.callee == "print" || call.callee == "io::print" || call.callee == "__builtin_print") {
                for (auto& arg : call.args) {
                    auto argVal = lowerExpr(*arg);
                    auto argTy = checker_.getExprType(arg.get());
                    Instruction ci;
                    ci.opcode = Opcode::Call;
                    ci.result = InvalidValue;
                    ci.type   = checker_.context().getVoid();
                    if (!argTy || argTy->isArrayView() || argTy->isArray()) {
                        ci.strImm = "__tamarin_print_str";
                        ci.callArgs.push_back(argVal);
                    } else if (argTy->isBool()) {
                        ci.strImm = "__tamarin_print_bool";
                        ci.callArgs.push_back(argVal);
                    } else if (argTy->isFloat()) {
                        if (argTy->bitWidth() == 32) {
                            Instruction castInst;
                            castInst.opcode = Opcode::Cast;
                            castInst.result = nextValueId();
                            castInst.type   = checker_.context().getF64();
                            castInst.op1    = argVal;
                            emit(castInst);
                            ci.callArgs.push_back(castInst.result);
                        } else {
                            ci.callArgs.push_back(argVal);
                        }
                        ci.strImm = "__tamarin_print_float";
                    } else if (argTy->isInteger() && argTy->bitWidth() == 64) {
                        ci.strImm = "__tamarin_print_i64";
                        ci.callArgs.push_back(argVal);
                    } else {
                        if (argTy->isInteger() && argTy->bitWidth() < 32) {
                            Instruction castInst;
                            castInst.opcode = Opcode::Cast;
                            castInst.result = nextValueId();
                            castInst.type   = checker_.context().getI32();
                            castInst.op1    = argVal;
                            emit(castInst);
                            ci.callArgs.push_back(castInst.result);
                        } else {
                            ci.callArgs.push_back(argVal);
                        }
                        ci.strImm = "__tamarin_print_int";
                    }
                    emit(ci);
                }
                return InvalidValue;
            }

            // Builtin getInput() and readLine()
            if (call.callee == "getInput" || call.callee == "io::getInput" || call.callee == "__builtin_get_input" ||
                call.callee == "readLine" || call.callee == "io::readLine") {
                if (call.args.empty()) {
                    Instruction ci;
                    ci.opcode = Opcode::Call;
                    ci.result = nextValueId();
                    ci.type   = checker_.context().getArrayView(checker_.context().getU8(), /*isConst=*/true);
                    ci.strImm = "__tamarin_get_input";
                    emit(ci);
                    return ci.result;
                }
                auto bVal = lowerExpr(*call.args[0]);
                ValueId maxBytesVal = InvalidValue;
                if (call.args.size() >= 2) {
                    maxBytesVal = lowerExpr(*call.args[1]);
                } else {
                    Instruction getLenInst;
                    getLenInst.opcode = Opcode::GetLen;
                    getLenInst.result = nextValueId();
                    getLenInst.op1    = bVal;
                    getLenInst.type   = checker_.context().getUsize();
                    emit(getLenInst);
                    maxBytesVal = getLenInst.result;
                }
                Instruction ci;
                ci.opcode = Opcode::Call;
                ci.result = nextValueId();
                ci.type   = checker_.context().getI32();
                ci.strImm = "__tamarin_get_input_buf";
                ci.callArgs = {bVal, maxBytesVal};
                emit(ci);
                return ci.result;
            }

            // Builtin file operations
            if (call.callee == "openFile" || call.callee == "io::openFile" || call.callee == "file::open" || call.callee == "__builtin_file_open") {
                auto pVal = lowerExpr(*call.args[0]);
                auto mVal = lowerExpr(*call.args[1]);
                Instruction ci;
                ci.opcode = Opcode::Call;
                ci.result = nextValueId();
                ci.type   = checker_.context().getI64();
                ci.strImm = "__tamarin_file_open";
                ci.callArgs = {pVal, mVal};
                emit(ci);
                return ci.result;
            }
            if (call.callee == "closeFile" || call.callee == "io::closeFile" || call.callee == "file::close" || call.callee == "__builtin_file_close") {
                auto hVal = lowerExpr(*call.args[0]);
                Instruction ci;
                ci.opcode = Opcode::Call;
                ci.result = nextValueId();
                ci.type   = checker_.context().getI32();
                ci.strImm = "__tamarin_file_close";
                ci.callArgs = {hVal};
                emit(ci);
                return ci.result;
            }
            if (call.callee == "readFile" || call.callee == "io::readFile" || call.callee == "file::read" || call.callee == "__builtin_read_file") {
                auto pVal = lowerExpr(*call.args[0]);
                auto bVal = lowerExpr(*call.args[1]);
                auto mVal = lowerExpr(*call.args[2]);
                Instruction ci;
                ci.opcode = Opcode::Call;
                ci.result = nextValueId();
                ci.type   = checker_.context().getI32();
                ci.strImm = "__tamarin_read_file";
                ci.callArgs = {pVal, bVal, mVal};
                emit(ci);
                return ci.result;
            }
            if (call.callee == "writeFile" || call.callee == "io::writeFile" || call.callee == "file::write" || call.callee == "__builtin_write_file") {
                auto pVal = lowerExpr(*call.args[0]);
                auto tVal = lowerExpr(*call.args[1]);
                Instruction ci;
                ci.opcode = Opcode::Call;
                ci.result = nextValueId();
                ci.type   = checker_.context().getI32();
                ci.strImm = "__tamarin_write_file";
                ci.callArgs = {pVal, tVal};
                emit(ci);
                return ci.result;
            }
            if (call.callee == "appendFile" || call.callee == "io::appendFile" || call.callee == "file::append" || call.callee == "__builtin_append_file") {
                auto pVal = lowerExpr(*call.args[0]);
                auto tVal = lowerExpr(*call.args[1]);
                Instruction ci;
                ci.opcode = Opcode::Call;
                ci.result = nextValueId();
                ci.type   = checker_.context().getI32();
                ci.strImm = "__tamarin_append_file";
                ci.callArgs = {pVal, tVal};
                emit(ci);
                return ci.result;
            }
            if (call.callee == "fileExists" || call.callee == "io::fileExists" || call.callee == "file::exists" || call.callee == "__builtin_file_exists") {
                auto pVal = lowerExpr(*call.args[0]);
                Instruction ci;
                ci.opcode = Opcode::Call;
                ci.result = nextValueId();
                ci.type   = checker_.context().getBool();
                ci.strImm = "__tamarin_file_exists";
                ci.callArgs = {pVal};
                emit(ci);
                return ci.result;
            }
            if (call.callee == "removeFile" || call.callee == "io::removeFile" || call.callee == "file::remove" || call.callee == "__builtin_file_remove") {
                auto pVal = lowerExpr(*call.args[0]);
                Instruction ci;
                ci.opcode = Opcode::Call;
                ci.result = nextValueId();
                ci.type   = checker_.context().getI32();
                ci.strImm = "__tamarin_file_remove";
                ci.callArgs = {pVal};
                emit(ci);
                return ci.result;
            }
            if (call.callee == "fileRead" || call.callee == "__builtin_file_read") {
                auto hVal = lowerExpr(*call.args[0]);
                auto bVal = lowerExpr(*call.args[1]);
                auto mVal = lowerExpr(*call.args[2]);
                Instruction ci;
                ci.opcode = Opcode::Call;
                ci.result = nextValueId();
                ci.type   = checker_.context().getI32();
                ci.strImm = "__tamarin_file_read";
                ci.callArgs = {hVal, bVal, mVal};
                emit(ci);
                return ci.result;
            }
            if (call.callee == "fileWrite" || call.callee == "__builtin_file_write") {
                auto hVal = lowerExpr(*call.args[0]);
                auto tVal = lowerExpr(*call.args[1]);
                Instruction ci;
                ci.opcode = Opcode::Call;
                ci.result = nextValueId();
                ci.type   = checker_.context().getI32();
                ci.strImm = "__tamarin_file_write";
                ci.callArgs = {hVal, tVal};
                emit(ci);
                return ci.result;
            }
            if (call.callee == "fileFlush" || call.callee == "__builtin_file_flush") {
                auto hVal = lowerExpr(*call.args[0]);
                Instruction ci;
                ci.opcode = Opcode::Call;
                ci.result = InvalidValue;
                ci.type   = checker_.context().getVoid();
                ci.strImm = "__tamarin_file_flush";
                ci.callArgs = {hVal};
                emit(ci);
                return InvalidValue;
            }

            // Dynamic Heap & Memory Builtins
            if (call.callee == "__builtin_alloc" || call.callee == "mem::alloc") {
                auto cntVal = lowerExpr(*call.args[0]);
                auto resTy = checker_.getExprType(&expr);
                Instruction mi;
                mi.opcode = Opcode::MemAlloc;
                mi.result = nextValueId();
                mi.type   = resTy;
                mi.op1    = cntVal;
                emit(mi);
                return mi.result;
            }
            if (call.callee == "__builtin_realloc" || call.callee == "mem::realloc") {
                auto sliceVal = lowerExpr(*call.args[0]);
                auto cntVal = lowerExpr(*call.args[1]);
                auto resTy = checker_.getExprType(&expr);
                Instruction mi;
                mi.opcode = Opcode::MemRealloc;
                mi.result = nextValueId();
                mi.type   = resTy;
                mi.op1    = sliceVal;
                mi.op2    = cntVal;
                emit(mi);
                return mi.result;
            }
            if (call.callee == "__builtin_free" || call.callee == "mem::free") {
                auto sliceVal = lowerExpr(*call.args[0]);
                Instruction mi;
                mi.opcode = Opcode::MemFree;
                mi.op1    = sliceVal;
                emit(mi);
                return InvalidValue;
            }
            if (call.callee == "__builtin_memcpy" || call.callee == "mem::copy") {
                auto dstVal = lowerExpr(*call.args[0]);
                auto dstOff = lowerExpr(*call.args[1]);
                auto srcVal = lowerExpr(*call.args[2]);
                auto srcOff = lowerExpr(*call.args[3]);
                auto cntVal = lowerExpr(*call.args[4]);
                auto dstTy = checker_.getExprType(call.args[0].get());
                const sema::Type* elemTy = (dstTy && dstTy->elementType) ? dstTy->elementType : checker_.context().getI8();
                Instruction mi;
                mi.opcode = Opcode::MemCopy;
                mi.type   = elemTy;
                mi.callArgs = { dstVal, dstOff, srcVal, srcOff, cntVal };
                emit(mi);
                return InvalidValue;
            }
            if (call.callee == "__builtin_memmove" || call.callee == "mem::move") {
                auto dstVal = lowerExpr(*call.args[0]);
                auto dstOff = lowerExpr(*call.args[1]);
                auto srcVal = lowerExpr(*call.args[2]);
                auto srcOff = lowerExpr(*call.args[3]);
                auto cntVal = lowerExpr(*call.args[4]);
                auto dstTy = checker_.getExprType(call.args[0].get());
                const sema::Type* elemTy = (dstTy && dstTy->elementType) ? dstTy->elementType : checker_.context().getI8();
                Instruction mi;
                mi.opcode = Opcode::MemMove;
                mi.type   = elemTy;
                mi.callArgs = { dstVal, dstOff, srcVal, srcOff, cntVal };
                emit(mi);
                return InvalidValue;
            }
            if (call.callee == "__builtin_memset" || call.callee == "mem::set") {
                auto dstVal = lowerExpr(*call.args[0]);
                auto dstOff = lowerExpr(*call.args[1]);
                auto valVal = lowerExpr(*call.args[2]);
                auto cntVal = lowerExpr(*call.args[3]);
                auto dstTy = checker_.getExprType(call.args[0].get());
                const sema::Type* elemTy = (dstTy && dstTy->elementType) ? dstTy->elementType : checker_.context().getI8();
                Instruction mi;
                mi.opcode = Opcode::MemSet;
                mi.type   = elemTy;
                mi.callArgs = { dstVal, dstOff, valVal, cntVal };
                emit(mi);
                return InvalidValue;
            }
            if (call.callee == "__builtin_get_unchecked" || call.callee == "mem::get_unchecked") {
                auto sVal = lowerExpr(*call.args[0]);
                auto idxVal = lowerExpr(*call.args[1]);
                auto resTy = checker_.getExprType(&expr);
                Instruction li;
                li.opcode = Opcode::ArrayLoad;
                li.result = nextValueId();
                li.type   = resTy;
                li.op1    = sVal;
                li.op2    = idxVal;
                emit(li);
                return li.result;
            }
            if (call.callee == "__builtin_set_unchecked" || call.callee == "mem::set_unchecked") {
                auto sVal = lowerExpr(*call.args[0]);
                auto idxVal = lowerExpr(*call.args[1]);
                auto valVal = lowerExpr(*call.args[2]);
                Instruction si;
                si.opcode = Opcode::ArrayStore;
                si.result = InvalidValue;
                si.type   = checker_.context().getVoid();
                si.op1    = sVal;
                si.op2    = idxVal;
                si.op3    = valVal;
                emit(si);
                return InvalidValue;
            }
            if (call.callee == "__builtin_atomic_load") {
                auto ptrVal = getStructPointer(*call.args[0]);
                auto valTy = checker_.getExprType(call.args[0].get());
                if (valTy && valTy->isReference()) valTy = valTy->elementType;
                if (!call.typeArgs.empty()) {
                    valTy = checker_.resolveType(*call.typeArgs[0]);
                }
                Instruction loadInst;
                loadInst.opcode = Opcode::AtomicLoad;
                loadInst.result = nextValueId();
                loadInst.op1    = ptrVal;
                loadInst.type   = valTy ? valTy : checker_.context().getI32();
                emit(loadInst);
                return loadInst.result;
            }
            if (call.callee == "__builtin_atomic_store") {
                auto ptrVal = getStructPointer(*call.args[0]);
                auto valId = lowerExpr(*call.args[1]);
                auto valTy = checker_.getExprType(call.args[1].get());
                Instruction storeInst;
                storeInst.opcode = Opcode::AtomicStore;
                storeInst.op1    = ptrVal;
                storeInst.op2    = valId;
                storeInst.type   = valTy;
                emit(storeInst);
                return InvalidValue;
            }
            if (call.callee == "__builtin_atomic_add" || call.callee == "__builtin_atomic_sub") {
                auto ptrVal = getStructPointer(*call.args[0]);
                auto valId = lowerExpr(*call.args[1]);
                auto valTy = checker_.getExprType(call.args[0].get());
                if (valTy && valTy->isReference()) valTy = valTy->elementType;
                if (!call.typeArgs.empty()) {
                    valTy = checker_.resolveType(*call.typeArgs[0]);
                }
                Instruction rmwInst;
                rmwInst.opcode = Opcode::AtomicRMW;
                rmwInst.result = nextValueId();
                rmwInst.op1    = ptrVal;
                rmwInst.op2    = valId;
                rmwInst.type   = valTy;
                rmwInst.intImm = (call.callee == "__builtin_atomic_sub") ? 1 : 0;
                emit(rmwInst);
                return rmwInst.result;
            }
            if (call.callee == "__builtin_atomic_cmpxchg") {
                auto ptrVal = getStructPointer(*call.args[0]);
                auto expVal = lowerExpr(*call.args[1]);
                auto desVal = lowerExpr(*call.args[2]);
                Instruction cmpxInst;
                cmpxInst.opcode = Opcode::AtomicCmpXchg;
                cmpxInst.result = nextValueId();
                cmpxInst.op1    = ptrVal;
                cmpxInst.op2    = expVal;
                cmpxInst.op3    = desVal;
                cmpxInst.type   = checker_.context().getBool();
                emit(cmpxInst);
                return cmpxInst.result;
            }

            auto sym = checker_.symbols().lookup(call.callee);
            const sema::Type* fnTy = sym ? sym->type : checker_.findGlobalFunction(call.callee);

            // If not a global function, check if it's an unqualified method call on 'this'
            const sema::MethodType* thisMethod = nullptr;
            if (!fnTy && currentRecord_) {
                thisMethod = currentRecord_->findMethod(call.callee);
            }

            std::vector<ValueId> args;
            if (thisMethod) {
                args.push_back(currentThisValue_); // this receiver
                for (size_t i = 0; i < call.args.size(); ++i) {
                    const sema::Type* expectedParamTy = (i < thisMethod->paramTypes.size()) ? thisMethod->paramTypes[i] : nullptr;
                    ValueId argVal = InvalidValue;
                    auto argTy = checker_.getExprType(call.args[i].get());
                    if (expectedParamTy && expectedParamTy->isReference()) {
                        if (argTy && (argTy->isArrayView() || argTy->isArray())) {
                            ValueId baseVal = lowerExpr(*call.args[i]);
                            Instruction zeroConst;
                            zeroConst.opcode = Opcode::IConst;
                            zeroConst.result = nextValueId();
                            zeroConst.intImm = 0;
                            zeroConst.type   = checker_.context().getI64();
                            emit(zeroConst);

                            Instruction gep;
                            gep.opcode = Opcode::GetArrayElemPtr;
                            gep.result = nextValueId();
                            gep.op1    = baseVal;
                            gep.op2    = zeroConst.result;
                            gep.type   = (argTy->elementType) ? argTy->elementType : checker_.context().getU8();
                            emit(gep);
                            argVal = gep.result;
                        } else {
                            argVal = getStructPointer(*call.args[i]);
                        }
                    } else {
                        argVal = lowerExpr(*call.args[i]);
                        if (expectedParamTy) {
                            argVal = emitCastIfNeeded(argVal, argTy, expectedParamTy);
                        }
                    }
                    args.push_back(argVal);
                }

                auto retTy = checker_.getExprType(&expr);
                if (thisMethod->isVirtual) {
                    Instruction cv;
                    cv.opcode   = Opcode::CallVirtual;
                    cv.result   = (retTy && !retTy->isVoid()) ? nextValueId() : InvalidValue;
                    cv.type     = retTy;
                    cv.op1      = currentThisValue_;
                    cv.intImm   = thisMethod->vtableSlot;
                    cv.strImm   = call.callee;
                    cv.callArgs = std::move(args);
                    emit(cv);
                    return cv.result;
                } else {
                    std::string recName = (thisMethod && !thisMethod->definingClass.empty()) ? thisMethod->definingClass : currentRecord_->structName;
                    Instruction callInst;
                    callInst.opcode   = Opcode::Call;
                    callInst.result   = (retTy && !retTy->isVoid()) ? nextValueId() : InvalidValue;
                    callInst.type     = retTy;
                    callInst.strImm   = recName + "_" + call.callee;
                    callInst.callArgs = std::move(args);
                    emit(callInst);
                    return callInst.result;
                }
            } else {
                ValueId indirectFnPtr = InvalidValue;
                if (call.calleeExpr) {
                    indirectFnPtr = lowerExpr(*call.calleeExpr);
                } else {
                    auto* b = lookupBinding(call.callee);
                    if (b && b->type && b->type->isFunction()) {
                        Instruction loadFn;
                        loadFn.opcode = Opcode::Load;
                        loadFn.result = nextValueId();
                        loadFn.op1    = b->ptr;
                        loadFn.type   = b->type;
                        emit(loadFn);
                        indirectFnPtr = loadFn.result;
                    }
                }

                for (size_t i = 0; i < call.args.size(); ++i) {
                    const sema::Type* expectedParamTy = (fnTy && i < fnTy->paramTypes.size()) ? fnTy->paramTypes[i] : nullptr;
                    ValueId argVal = InvalidValue;
                    auto argTy = checker_.getExprType(call.args[i].get());
                    if (expectedParamTy && expectedParamTy->isReference()) {
                        if (argTy && (argTy->isArrayView() || argTy->isArray())) {
                            ValueId baseVal = lowerExpr(*call.args[i]);
                            Instruction zeroConst;
                            zeroConst.opcode = Opcode::IConst;
                            zeroConst.result = nextValueId();
                            zeroConst.intImm = 0;
                            zeroConst.type   = checker_.context().getI64();
                            emit(zeroConst);

                            Instruction gep;
                            gep.opcode = Opcode::GetArrayElemPtr;
                            gep.result = nextValueId();
                            gep.op1    = baseVal;
                            gep.op2    = zeroConst.result;
                            gep.type   = (argTy->elementType) ? argTy->elementType : checker_.context().getU8();
                            emit(gep);
                            argVal = gep.result;
                        } else {
                            argVal = getStructPointer(*call.args[i]);
                        }
                    } else {
                        argVal = lowerExpr(*call.args[i]);
                        if (expectedParamTy) {
                            argVal = emitCastIfNeeded(argVal, argTy, expectedParamTy);
                        }
                    }
                    args.push_back(argVal);
                }

                auto retTy = checker_.getExprType(&expr);
                Instruction callInst;
                callInst.opcode   = Opcode::Call;
                callInst.result   = (retTy && !retTy->isVoid()) ? nextValueId() : InvalidValue;
                callInst.type     = retTy;
                if (indirectFnPtr != InvalidValue) {
                    callInst.op1  = indirectFnPtr;
                } else {
                    callInst.strImm = call.callee;
                }
                callInst.callArgs = std::move(args);
                emit(callInst);
                return callInst.result;
            }
        }
        case ExprKind::Index: {
            auto& idx = static_cast<IndexExpr&>(expr);
            auto arrTy = checker_.getExprType(idx.array.get());
            if (arrTy && arrTy->isReference()) arrTy = arrTy->elementType;

            if (idx.index && idx.index->kind == ExprKind::Range) {
                auto& range = static_cast<RangeExpr&>(*idx.index);
                auto elemTy = (arrTy && arrTy->elementType) ? arrTy->elementType : checker_.context().getI32();
                bool isView = arrTy && arrTy->isArrayView();

                ValueId arrVal = lowerExpr(*idx.array);

                // Determine base length
                ValueId lenVal = InvalidValue;
                if (isView) {
                    Instruction getLen;
                    getLen.opcode = Opcode::GetLen;
                    getLen.result = nextValueId();
                    getLen.op1    = arrVal;
                    getLen.type   = checker_.context().getI64();
                    emit(getLen);
                    lenVal = getLen.result;
                } else if (arrTy && arrTy->arraySize >= 0) {
                    lenVal = emitIntConstant(arrTy->arraySize, checker_.context().getI64());
                } else {
                    lenVal = emitIntConstant(0, checker_.context().getI64());
                }

                // Start offset
                ValueId startVal = InvalidValue;
                if (range.start) {
                    startVal = lowerExpr(*range.start);
                    startVal = emitCastIfNeeded(startVal, checker_.getExprType(range.start.get()), checker_.context().getI64());
                } else {
                    startVal = emitIntConstant(0, checker_.context().getI64());
                }

                // End offset
                ValueId endVal = InvalidValue;
                if (range.end) {
                    endVal = lowerExpr(*range.end);
                    endVal = emitCastIfNeeded(endVal, checker_.getExprType(range.end.get()), checker_.context().getI64());
                } else {
                    endVal = lenVal;
                }

                // sliceLen = end - start
                Instruction subInst;
                subInst.opcode = Opcode::Sub;
                subInst.result = nextValueId();
                subInst.op1    = endVal;
                subInst.op2    = startVal;
                subInst.type   = checker_.context().getI64();
                emit(subInst);
                ValueId sliceLen = subInst.result;

                // slicePtr = GetArrayElemPtr(arrVal, startVal)
                Instruction gep;
                gep.opcode = Opcode::GetArrayElemPtr;
                gep.result = nextValueId();
                gep.op1    = arrVal;
                gep.op2    = startVal;
                gep.type   = elemTy;
                emit(gep);

                // MakeView(slicePtr, sliceLen)
                Instruction mkView;
                mkView.opcode = Opcode::MakeView;
                mkView.result = nextValueId();
                mkView.op1    = gep.result;
                mkView.op2    = sliceLen;
                mkView.type   = checker_.context().getArrayView(elemTy, arrTy ? arrTy->isConst : false);
                emit(mkView);

                return mkView.result;
            }
            if (arrTy && arrTy->isSIMDVector()) {
                auto vecVal = lowerExpr(*idx.array);
                auto idxVal = lowerExpr(*idx.index);
                Instruction ext;
                ext.opcode = Opcode::VecExtract;
                ext.result = nextValueId();
                ext.op1    = vecVal;
                ext.op2    = idxVal;
                ext.type   = arrTy->elementType;
                emit(ext);
                return ext.result;
            }
            if (arrTy && arrTy->isRecord()) {
                auto objPtr = getStructPointer(*idx.array);
                auto idxVal = lowerExpr(*idx.index);
                auto retTy = checker_.getExprType(&expr);
                std::string recName = arrTy->structName;
                std::string methodName = !idx.overloadedMethod.empty() ? idx.overloadedMethod : "get";
                Instruction callInst;
                callInst.opcode = Opcode::Call;
                callInst.result = (retTy && !retTy->isVoid()) ? nextValueId() : InvalidValue;
                callInst.type   = retTy;
                callInst.strImm = sema::mangleMethodName(recName, methodName);
                callInst.callArgs = { objPtr, idxVal };
                emit(callInst);
                return callInst.result;
            }
            if (arrTy && arrTy->isSlab()) {
                auto slabPtr = getStructPointer(*idx.array);
                auto hVal = lowerExpr(*idx.index);
                auto elemTy = checker_.getExprType(&expr);
                Instruction getInst;
                getInst.opcode = Opcode::SlabGet;
                getInst.result = nextValueId();
                getInst.type   = checker_.context().getReference(elemTy);
                getInst.op1    = slabPtr;
                getInst.op2    = hVal;
                emit(getInst);

                if (elemTy && (elemTy->isRecord() || elemTy->isArray())) {
                    return getInst.result;
                }
                Instruction load;
                load.opcode = Opcode::Load;
                load.result = nextValueId();
                load.op1    = getInst.result;
                load.type   = elemTy;
                emit(load);
                return load.result;
            }

            auto arrVal = lowerExpr(*idx.array);
            auto idxVal = lowerExpr(*idx.index);
            auto elemTy = checker_.getExprType(&expr);

            // Explicit bounds check instruction if known size or view
            if (arrTy && (arrTy->isArrayView() || arrTy->arraySize >= 0)) {
                Instruction chk;
                chk.opcode = Opcode::BoundsCheck;
                chk.op1    = idxVal;
                chk.op2    = arrVal;
                emit(chk);
            }

            if (elemTy && elemTy->isArray()) {
                Instruction gep;
                gep.opcode = Opcode::GetArrayElemPtr;
                gep.result = nextValueId();
                gep.op1    = arrVal;
                gep.op2    = idxVal;
                gep.type   = elemTy;
                emit(gep);
                return gep.result;
            }

            Instruction load;
            load.opcode = Opcode::ArrayLoad;
            load.result = nextValueId();
            load.op1    = arrVal;
            load.op2    = idxVal;
            load.type   = elemTy ? elemTy : checker_.context().getI32();
            emit(load);
            return load.result;
        }
        case ExprKind::Cast: {
            auto& c = static_cast<CastExpr&>(expr);
            auto targetTy = checker_.resolveType(*c.targetType);
            auto operand = lowerExpr(*c.operand);
            auto opTy = checker_.getExprType(c.operand.get());
            return emitCastIfNeeded(operand, opTy, targetTy);
        }
        case ExprKind::Field: {
            auto& fe = static_cast<FieldExpr&>(expr);

            // Structure-of-Arrays (SoA) field load
            if (fe.object->kind == ExprKind::Index) {
                auto& idx = static_cast<IndexExpr&>(*fe.object);
                auto arrTy = checker_.getExprType(idx.array.get());
                if (arrTy && arrTy->isArray() && arrTy->elementType &&
                    arrTy->elementType->isRecord() && arrTy->elementType->layoutDirective == 2 /* SoA */) {
                    auto elemRecTy = arrTy->elementType;
                    int fieldIdx = elemRecTy->getFieldIndex(fe.fieldName);
                    auto f = elemRecTy->findField(fe.fieldName);
                    auto fieldTy = f ? f->type : checker_.context().getI32();

                    auto bundlePtr = getStructPointer(*idx.array);
                    auto idxVal = lowerExpr(*idx.index);

                    Instruction soaGfp;
                    soaGfp.opcode = Opcode::SoAGetFieldPtr;
                    soaGfp.result = nextValueId();
                    soaGfp.op1    = bundlePtr;
                    soaGfp.op2    = idxVal;
                    soaGfp.intImm = fieldIdx;
                    soaGfp.strImm = elemRecTy->structName;
                    soaGfp.type   = fieldTy;
                    emit(soaGfp);

                    Instruction load;
                    load.opcode = Opcode::Load;
                    load.result = nextValueId();
                    load.op1    = soaGfp.result;
                    load.type   = fieldTy;
                    emit(load);
                    return load.result;
                }
            }

            auto baseTy = checker_.getExprType(fe.object.get());
            if (baseTy && baseTy->isReference()) baseTy = baseTy->elementType;
            if (baseTy && baseTy->isSIMDVector()) {
                auto vecVal = lowerExpr(*fe.object);
                uint32_t lane = 0;
                if (fe.fieldName == "x" || fe.fieldName == "r") lane = 0;
                else if (fe.fieldName == "y" || fe.fieldName == "g") lane = 1;
                else if (fe.fieldName == "z" || fe.fieldName == "b") lane = 2;
                else if (fe.fieldName == "w" || fe.fieldName == "a") lane = 3;

                Instruction ext;
                ext.opcode = Opcode::VecExtract;
                ext.result = nextValueId();
                ext.op1    = vecVal;
                ext.intImm = static_cast<int64_t>(lane);
                ext.type   = baseTy->elementType;
                emit(ext);
                return ext.result;
            }
            if (baseTy && baseTy->isHandle()) {
                auto hVal = lowerExpr(*fe.object);
                auto retTy = checker_.getExprType(&expr);
                if (fe.fieldName == "slot" || fe.fieldName == "slot_index") {
                    Instruction tr;
                    tr.opcode = Opcode::Cast;
                    tr.result = nextValueId();
                    tr.type   = retTy;
                    tr.op1    = hVal;
                    emit(tr);
                    return tr.result;
                }
                if (fe.fieldName == "generation" || fe.fieldName == "gen") {
                    Instruction shr;
                    shr.opcode = Opcode::LShr;
                    shr.result = nextValueId();
                    shr.type   = checker_.context().getU64();
                    shr.op1    = hVal;
                    shr.op2    = emitIntConstant(32, checker_.context().getI32());
                    emit(shr);

                    Instruction tr;
                    tr.opcode = Opcode::Cast;
                    tr.result = nextValueId();
                    tr.type   = retTy;
                    tr.op1    = shr.result;
                    emit(tr);
                    return tr.result;
                }
            }

            auto structPtr = getStructPointer(*fe.object);
            if (!baseTy || !baseTy->isRecord()) return InvalidValue;
            int fieldIdx = baseTy->getFieldIndex(fe.fieldName);
            auto f = baseTy->findField(fe.fieldName);
            auto fieldTy = f ? f->type : checker_.context().getI32();

            Instruction gfp;
            gfp.opcode = Opcode::GetFieldPtr;
            gfp.result = nextValueId();
            gfp.op1    = structPtr;
            gfp.intImm = fieldIdx;
            gfp.type   = fieldTy;
            gfp.strImm = baseTy->structName;
            emit(gfp);

            if (fieldTy && fieldTy->isArray()) {
                auto lenVal = emitIntConstant(fieldTy->arraySize, checker_.context().getI32());
                Instruction mkView;
                mkView.opcode = Opcode::MakeView;
                mkView.result = nextValueId();
                mkView.op1    = gfp.result;
                mkView.op2    = lenVal;
                mkView.type   = checker_.context().getArrayView(fieldTy->elementType);
                emit(mkView);
                return mkView.result;
            }

            Instruction load;
            load.opcode = Opcode::Load;
            load.result = nextValueId();
            load.op1    = gfp.result;
            load.type   = fieldTy;
            emit(load);
            return load.result;
        }
        case ExprKind::CompoundInit: {
            auto& cie = static_cast<CompoundInitExpr&>(expr);
            auto structTy = checker_.getExprType(&expr);
            if (!structTy || !structTy->isRecord()) return InvalidValue;

            Instruction allocaInst;
            allocaInst.opcode = Opcode::Alloca;
            allocaInst.result = nextValueId();
            allocaInst.type   = structTy;
            emit(allocaInst);

            // Initialize _vptr if polymorphic
            if (structTy->isPolymorphic) {
                Instruction gv;
                gv.opcode = Opcode::GetVTablePtr;
                gv.result = nextValueId();
                gv.strImm = structTy->structName;
                gv.type   = checker_.context().getReference(checker_.context().getI8());
                emit(gv);

                Instruction gfp;
                gfp.opcode = Opcode::GetFieldPtr;
                gfp.result = nextValueId();
                gfp.op1    = allocaInst.result;
                gfp.intImm = 0;
                gfp.type   = gv.type;
                gfp.strImm = structTy->structName;
                emit(gfp);

                Instruction st;
                st.opcode = Opcode::Store;
                st.op1    = gfp.result;
                st.op2    = gv.result;
                emit(st);
            }

            for (size_t i = 0; i < cie.fields.size(); ++i) {
                auto& f = cie.fields[i];
                int fIdx = -1;
                const sema::StructFieldType* sft = nullptr;
                if (!f.fieldName.empty()) {
                    fIdx = structTy->getFieldIndex(f.fieldName);
                    sft = structTy->findField(f.fieldName);
                } else {
                    int userOffset = structTy->isPolymorphic ? 1 : 0;
                    fIdx = static_cast<int>(i) + userOffset;
                    if (fIdx < structTy->structFields.size()) sft = &structTy->structFields[fIdx];
                }
                if (fIdx < 0 || !sft) continue;

                auto valId = lowerExpr(*f.value);
                auto valTy = checker_.getExprType(f.value.get());
                valId = emitCastIfNeeded(valId, valTy, sft->type);

                Instruction gfp;
                gfp.opcode = Opcode::GetFieldPtr;
                gfp.result = nextValueId();
                gfp.op1    = allocaInst.result;
                gfp.intImm = fIdx;
                gfp.type   = sft->type;
                gfp.strImm = structTy->structName;
                emit(gfp);

                Instruction store;
                store.opcode = Opcode::Store;
                store.op1    = gfp.result;
                store.op2    = valId;
                emit(store);
            }

            Instruction load;
            load.opcode = Opcode::Load;
            load.result = nextValueId();
            load.op1    = allocaInst.result;
            load.type   = structTy;
            emit(load);
            return load.result;
        }
        case ExprKind::ArrayInit:
            return InvalidValue;

        case ExprKind::This: {
            return currentThisValue_;
        }

        case ExprKind::Construct: {
            auto& ce = static_cast<ConstructExpr&>(expr);
            if (ce.typeName == "float4" || ce.typeName == "float8" ||
                ce.typeName == "int4" || ce.typeName == "int8" || ce.typeName == "int16") {
                const sema::Type* simdTy = nullptr;
                if (ce.typeName == "float4") simdTy = checker_.context().getFloat4();
                else if (ce.typeName == "float8") simdTy = checker_.context().getFloat8();
                else if (ce.typeName == "int4") simdTy = checker_.context().getInt4();
                else if (ce.typeName == "int8") simdTy = checker_.context().getInt8();
                else if (ce.typeName == "int16") simdTy = checker_.context().getInt16();

                if (ce.args.size() == 1) {
                    auto scalarVal = lowerExpr(*ce.args[0]);
                    Instruction splat;
                    splat.opcode = Opcode::VecSplat;
                    splat.result = nextValueId();
                    splat.op1    = scalarVal;
                    splat.type   = simdTy;
                    emit(splat);
                    return splat.result;
                }

                ValueId currentVec = InvalidValue;
                for (size_t i = 0; i < ce.args.size(); ++i) {
                    auto compVal = lowerExpr(*ce.args[i]);
                    if (i == 0) {
                        Instruction splat;
                        splat.opcode = Opcode::VecSplat;
                        splat.result = nextValueId();
                        splat.op1    = compVal;
                        splat.type   = simdTy;
                        emit(splat);
                        currentVec = splat.result;
                    } else {
                        Instruction ins;
                        ins.opcode = Opcode::VecInsert;
                        ins.result = nextValueId();
                        ins.op1    = currentVec;
                        ins.op2    = compVal;
                        ins.intImm = static_cast<int64_t>(i);
                        ins.type   = simdTy;
                        emit(ins);
                        currentVec = ins.result;
                    }
                }
                return currentVec;
            }

            auto recTy = checker_.context().findRecord(ce.typeName);
            if (!recTy) return InvalidValue;

            Instruction allocaInst;
            allocaInst.opcode = Opcode::Alloca;
            allocaInst.result = nextValueId();
            allocaInst.type   = recTy;
            emit(allocaInst);

            // Initialize _vptr if polymorphic
            if (recTy->isPolymorphic) {
                Instruction gv;
                gv.opcode = Opcode::GetVTablePtr;
                gv.result = nextValueId();
                gv.strImm = recTy->structName;
                gv.type   = checker_.context().getReference(checker_.context().getI8());
                emit(gv);

                Instruction gfp;
                gfp.opcode = Opcode::GetFieldPtr;
                gfp.result = nextValueId();
                gfp.op1    = allocaInst.result;
                gfp.intImm = 0;
                gfp.type   = gv.type;
                gfp.strImm = recTy->structName;
                emit(gfp);

                Instruction st;
                st.opcode = Opcode::Store;
                st.op1    = gfp.result;
                st.op2    = gv.result;
                emit(st);
            }

            std::string ctorName = recTy->constructors.size() == 1
                ? (recTy->structName + "_" + recTy->structName)
                : (recTy->structName + "_" + recTy->structName + "_" + std::to_string(ce.args.size()));
            std::vector<ValueId> cargs;
            cargs.push_back(allocaInst.result); // this
            for (auto& arg : ce.args) {
                cargs.push_back(lowerExpr(*arg));
            }
            Instruction callCtor;
            callCtor.opcode = Opcode::Call;
            callCtor.type   = checker_.context().getVoid();
            callCtor.strImm = ctorName;
            callCtor.callArgs = std::move(cargs);
            emit(callCtor);

            Instruction load;
            load.opcode = Opcode::Load;
            load.result = nextValueId();
            load.op1    = allocaInst.result;
            load.type   = recTy;
            emit(load);
            return load.result;
        }

        case ExprKind::MethodCall: {
            auto& mc = static_cast<MethodCallExpr&>(expr);
            auto objTy = checker_.getExprType(mc.object.get());
            if (objTy && objTy->isReference()) objTy = objTy->elementType;
            if (!objTy) return InvalidValue;

            if (objTy->isSlab()) {
                auto slabPtr = getStructPointer(*mc.object);
                auto retTy = checker_.getExprType(&expr);
                const std::string& method = mc.methodName;

                if (method == "insert") {
                    ValueId valPtr = InvalidValue;
                    auto argTy = checker_.getExprType(mc.args[0].get());
                    if (argTy && (argTy->isRecord() || argTy->isArray())) {
                        valPtr = getStructPointer(*mc.args[0]);
                    } else {
                        auto val = lowerExpr(*mc.args[0]);
                        Instruction alc;
                        alc.opcode = Opcode::Alloca;
                        alc.result = nextValueId();
                        alc.type   = argTy;
                        emit(alc);

                        Instruction st;
                        st.opcode = Opcode::Store;
                        st.op1    = alc.result;
                        st.op2    = val;
                        emit(st);
                        valPtr = alc.result;
                    }
                    Instruction ins;
                    ins.opcode = Opcode::SlabInsert;
                    ins.result = nextValueId();
                    ins.type   = retTy;
                    ins.op1    = slabPtr;
                    ins.op2    = valPtr;
                    emit(ins);
                    return ins.result;
                }
                if (method == "get") {
                    auto hVal = lowerExpr(*mc.args[0]);
                    Instruction getInst;
                    getInst.opcode = Opcode::SlabGet;
                    getInst.result = nextValueId();
                    getInst.type   = retTy;
                    getInst.op1    = slabPtr;
                    getInst.op2    = hVal;
                    emit(getInst);
                    return getInst.result;
                }
                if (method == "is_valid") {
                    auto hVal = lowerExpr(*mc.args[0]);
                    Instruction ivInst;
                    ivInst.opcode = Opcode::SlabIsValid;
                    ivInst.result = nextValueId();
                    ivInst.type   = retTy;
                    ivInst.op1    = slabPtr;
                    ivInst.op2    = hVal;
                    emit(ivInst);
                    return ivInst.result;
                }
                if (method == "try_get") {
                    auto hVal = lowerExpr(*mc.args[0]);
                    auto outPtr = getStructPointer(*mc.args[1]);
                    Instruction tgInst;
                    tgInst.opcode = Opcode::SlabTryGet;
                    tgInst.result = nextValueId();
                    tgInst.type   = retTy;
                    tgInst.op1    = slabPtr;
                    tgInst.op2    = hVal;
                    tgInst.op3    = outPtr;
                    emit(tgInst);
                    return tgInst.result;
                }
                if (method == "remove") {
                    auto hVal = lowerExpr(*mc.args[0]);
                    Instruction rmInst;
                    rmInst.opcode = Opcode::SlabRemove;
                    rmInst.result = nextValueId();
                    rmInst.type   = retTy;
                    rmInst.op1    = slabPtr;
                    rmInst.op2    = hVal;
                    if (objTy->elementType && objTy->elementType->hasNonTrivialDestructor()) {
                        rmInst.strImm = objTy->elementType->structName + "_destructor";
                    }
                    emit(rmInst);
                    return rmInst.result;
                }
                if (method == "count") {
                    Instruction cntInst;
                    cntInst.opcode = Opcode::SlabCount;
                    cntInst.result = nextValueId();
                    cntInst.type   = retTy;
                    cntInst.op1    = slabPtr;
                    emit(cntInst);
                    return cntInst.result;
                }
                if (method == "capacity") {
                    Instruction capInst;
                    capInst.opcode = Opcode::SlabCapacity;
                    capInst.result = nextValueId();
                    capInst.type   = retTy;
                    capInst.op1    = slabPtr;
                    emit(capInst);
                    return capInst.result;
                }
                if (method == "clear") {
                    Instruction clrInst;
                    clrInst.opcode = Opcode::SlabClear;
                    clrInst.op1    = slabPtr;
                    if (objTy->elementType && objTy->elementType->hasNonTrivialDestructor()) {
                        clrInst.strImm = objTy->elementType->structName + "_destructor";
                    }
                    emit(clrInst);
                    return InvalidValue;
                }
            }

            if (objTy->isHandle()) {
                auto hVal = lowerExpr(*mc.object);
                auto retTy = checker_.getExprType(&expr);
                const std::string& method = mc.methodName;
                if (method == "is_valid") {
                    Instruction shr;
                    shr.opcode = Opcode::LShr;
                    shr.result = nextValueId();
                    shr.type   = checker_.context().getU64();
                    shr.op1    = hVal;
                    shr.op2    = emitIntConstant(32, checker_.context().getI32());
                    emit(shr);

                    Instruction tr;
                    tr.opcode = Opcode::Cast;
                    tr.result = nextValueId();
                    tr.type   = checker_.context().getU32();
                    tr.op1    = shr.result;
                    emit(tr);

                    Instruction cmp;
                    cmp.opcode = Opcode::ICmpNe;
                    cmp.result = nextValueId();
                    cmp.type   = retTy;
                    cmp.op1    = tr.result;
                    cmp.op2    = emitIntConstant(0, checker_.context().getU32());
                    emit(cmp);
                    return cmp.result;
                }
                if (method == "slot" || method == "slot_index") {
                    Instruction tr;
                    tr.opcode = Opcode::Cast;
                    tr.result = nextValueId();
                    tr.type   = retTy;
                    tr.op1    = hVal;
                    emit(tr);
                    return tr.result;
                }
                if (method == "generation" || method == "gen") {
                    Instruction shr;
                    shr.opcode = Opcode::LShr;
                    shr.result = nextValueId();
                    shr.type   = checker_.context().getU64();
                    shr.op1    = hVal;
                    shr.op2    = emitIntConstant(32, checker_.context().getI32());
                    emit(shr);

                    Instruction tr;
                    tr.opcode = Opcode::Cast;
                    tr.result = nextValueId();
                    tr.type   = retTy;
                    tr.op1    = shr.result;
                    emit(tr);
                    return tr.result;
                }
            }

            auto objPtr = getStructPointer(*mc.object);
            if (!objTy || !objTy->isRecord()) return InvalidValue;

            const sema::MethodType* m = objTy->findMethod(mc.methodName);
            auto retTy = checker_.getExprType(&expr);

            std::vector<ValueId> args;
            args.push_back(objPtr); // this
            for (size_t i = 0; i < mc.args.size(); ++i) {
                const sema::Type* expectedParamTy = (m && i < m->paramTypes.size()) ? m->paramTypes[i] : nullptr;
                ValueId argVal = InvalidValue;
                if (expectedParamTy && expectedParamTy->isReference()) {
                    argVal = getStructPointer(*mc.args[i]);
                } else {
                    argVal = lowerExpr(*mc.args[i]);
                    auto argTy = checker_.getExprType(mc.args[i].get());
                    if (expectedParamTy) {
                        argVal = emitCastIfNeeded(argVal, argTy, expectedParamTy);
                    }
                }
                args.push_back(argVal);
            }

            if (m && m->isVirtual) {
                Instruction cv;
                cv.opcode   = Opcode::CallVirtual;
                cv.result   = (retTy && !retTy->isVoid()) ? nextValueId() : InvalidValue;
                cv.type     = retTy;
                cv.op1      = objPtr;
                cv.intImm   = m->vtableSlot;
                cv.strImm   = mc.methodName;
                cv.callArgs = std::move(args);
                emit(cv);
                return cv.result;
            } else {
                std::string recName = (m && !m->definingClass.empty()) ? m->definingClass : objTy->structName;
                Instruction call;
                call.opcode   = Opcode::Call;
                call.result   = (retTy && !retTy->isVoid()) ? nextValueId() : InvalidValue;
                call.type     = retTy;
                call.strImm   = sema::mangleMethodName(recName, mc.methodName, mc.args.size());
                call.callArgs = std::move(args);
                emit(call);
                return call.result;
            }
        }
        case ExprKind::Match: {
            return lowerMatchExpr(static_cast<MatchExpr&>(expr));
        }
        case ExprKind::Try: {
            return lowerTryExpr(static_cast<TryExpr&>(expr));
        }
        case ExprKind::Ternary: {
            return lowerTernaryExpr(static_cast<TernaryExpr&>(expr));
        }
        case ExprKind::Tuple: {
            auto& te = static_cast<TupleExpr&>(expr);
            auto tupleTy = checker_.getExprType(&expr);
            if (!tupleTy) return InvalidValue;

            Instruction allocaInst;
            allocaInst.opcode = Opcode::Alloca;
            allocaInst.result = nextValueId();
            allocaInst.type   = tupleTy;
            emit(allocaInst);

            for (size_t i = 0; i < te.elements.size(); ++i) {
                auto elemVal = lowerExpr(*te.elements[i]);
                auto elemTy = (i < tupleTy->tupleElements.size()) ? tupleTy->tupleElements[i] : checker_.getExprType(te.elements[i].get());

                Instruction gfp;
                gfp.opcode = Opcode::GetFieldPtr;
                gfp.result = nextValueId();
                gfp.op1    = allocaInst.result;
                gfp.intImm = static_cast<int64_t>(i);
                gfp.type   = elemTy;
                emit(gfp);

                Instruction store;
                store.opcode = Opcode::Store;
                store.op1    = gfp.result;
                store.op2    = elemVal;
                emit(store);
            }

            Instruction load;
            load.opcode = Opcode::Load;
            load.result = nextValueId();
            load.op1    = allocaInst.result;
            load.type   = tupleTy;
            emit(load);
            return load.result;
        }
        case ExprKind::TupleIndex: {
            auto& tie = static_cast<TupleIndexExpr&>(expr);
            auto elemTy = checker_.getExprType(&expr);
            auto tupPtr = getStructPointer(*tie.tuple);

            Instruction gfp;
            gfp.opcode = Opcode::GetFieldPtr;
            gfp.result = nextValueId();
            gfp.op1    = tupPtr;
            gfp.intImm = static_cast<int64_t>(tie.index);
            gfp.type   = elemTy;
            emit(gfp);

            Instruction load;
            load.opcode = Opcode::Load;
            load.result = nextValueId();
            load.op1    = gfp.result;
            load.type   = elemTy;
            emit(load);
            return load.result;
        }
        case ExprKind::Lambda: {
            return lowerLambda(static_cast<LambdaExpr&>(expr));
        }
        case ExprKind::Range: {
            return InvalidValue;
        }
        case ExprKind::Await: {
            auto& aw = static_cast<AwaitExpr&>(expr);
            auto taskVal = lowerExpr(*aw.operand);
            auto taskTy = checker_.getExprType(aw.operand.get());
            const sema::Type* innerTy = (taskTy && taskTy->isTask() && taskTy->elementType)
                                            ? taskTy->elementType
                                            : checker_.context().getI32();

            Instruction awaitInst;
            awaitInst.opcode = Opcode::AsyncTaskAwait;
            awaitInst.result = nextValueId();
            awaitInst.op1    = taskVal;
            awaitInst.type   = checker_.context().getReference(innerTy, false);
            emit(awaitInst);

            ValueId resVal = InvalidValue;
            if (!innerTy->isVoid()) {
                Instruction loadInst;
                loadInst.opcode = Opcode::Load;
                loadInst.result = nextValueId();
                loadInst.op1    = awaitInst.result;
                loadInst.type   = innerTy;
                emit(loadInst);
                resVal = loadInst.result;
            }

            Instruction freeInst;
            freeInst.opcode = Opcode::AsyncTaskFree;
            freeInst.op1    = taskVal;
            emit(freeInst);

            return resVal;
        }
    }
    return InvalidValue;
}

ValueId IRBuilder::getStructPointer(Expr& objExpr) {
    if (objExpr.kind == ExprKind::This) {
        return currentThisValue_;
    }
    if (objExpr.kind == ExprKind::Unary) {
        auto& un = static_cast<UnaryExpr&>(objExpr);
        if (un.op == UnaryOp::Deref) {
            return lowerExpr(*un.operand);
        }
    }
    if (objExpr.kind == ExprKind::TupleIndex) {
        auto& tie = static_cast<TupleIndexExpr&>(objExpr);
        auto basePtr = getStructPointer(*tie.tuple);
        auto elemTy = checker_.getExprType(&objExpr);

        Instruction gfp;
        gfp.opcode = Opcode::GetFieldPtr;
        gfp.result = nextValueId();
        gfp.op1    = basePtr;
        gfp.intImm = static_cast<int64_t>(tie.index);
        gfp.type   = elemTy;
        emit(gfp);
        return gfp.result;
    }
    if (objExpr.kind == ExprKind::Identifier) {
        auto& id = static_cast<IdentifierExpr&>(objExpr);
        auto* b = lookupBinding(id.name);
        if (b) {
            if (b->type && b->type->isReference()) {
                Instruction loadRef;
                loadRef.opcode = Opcode::Load;
                loadRef.result = nextValueId();
                loadRef.op1    = b->ptr;
                loadRef.type   = b->type;
                emit(loadRef);
                return loadRef.result;
            }
            return b->ptr;
        }
        if (currentRecord_ && currentRecord_->findField(id.name)) {
            int fIdx = currentRecord_->getFieldIndex(id.name);
            auto f = currentRecord_->findField(id.name);
            Instruction gfp;
            gfp.opcode = Opcode::GetFieldPtr;
            gfp.result = nextValueId();
            gfp.op1    = currentThisValue_;
            gfp.intImm = fIdx;
            gfp.type   = f ? f->type : nullptr;
            gfp.strImm = currentRecord_->structName;
            emit(gfp);
            return gfp.result;
        }
        return InvalidValue;
    } else if (objExpr.kind == ExprKind::Field) {
        auto& innerFe = static_cast<FieldExpr&>(objExpr);
        auto basePtr = getStructPointer(*innerFe.object);
        auto baseTy = checker_.getExprType(innerFe.object.get());
        if (baseTy && baseTy->isReference()) baseTy = baseTy->elementType;
        if (!baseTy || !baseTy->isRecord()) return InvalidValue;
        int fieldIdx = baseTy->getFieldIndex(innerFe.fieldName);
        auto f = baseTy->findField(innerFe.fieldName);
        Instruction gfp;
        gfp.opcode = Opcode::GetFieldPtr;
        gfp.result = nextValueId();
        gfp.op1    = basePtr;
        gfp.intImm = fieldIdx;
        gfp.type   = f ? f->type : nullptr;
        gfp.strImm = baseTy->structName;
        emit(gfp);
        return gfp.result;
    } else if (objExpr.kind == ExprKind::Index) {
        auto& idx = static_cast<IndexExpr&>(objExpr);
        auto arrTy = checker_.getExprType(idx.array.get());
        if (arrTy && arrTy->isReference()) arrTy = arrTy->elementType;
        if (arrTy && arrTy->isSlab()) {
            auto slabPtr = getStructPointer(*idx.array);
            auto hVal = lowerExpr(*idx.index);
            auto elemTy = checker_.getExprType(&objExpr);
            Instruction getInst;
            getInst.opcode = Opcode::SlabGet;
            getInst.result = nextValueId();
            getInst.type   = checker_.context().getReference(elemTy);
            getInst.op1    = slabPtr;
            getInst.op2    = hVal;
            emit(getInst);
            return getInst.result;
        }

        ValueId arrVal = InvalidValue;
        if (idx.array->kind == ExprKind::Identifier) {
            auto& id = static_cast<IdentifierExpr&>(*idx.array);
            auto* b = lookupBinding(id.name);
            if (b) {
                if (b->type && b->type->isArrayView()) {
                    arrVal = lowerExpr(*idx.array);
                } else {
                    arrVal = b->ptr;
                }
            }
        }
        if (arrVal == InvalidValue) {
            arrVal = lowerExpr(*idx.array);
        }
        auto idxVal = lowerExpr(*idx.index);
        auto elemTy = checker_.getExprType(&objExpr);

        // Bounds check if known size or view
        if (arrTy && (arrTy->isArrayView() || arrTy->arraySize >= 0)) {
            Instruction chk;
            chk.opcode = Opcode::BoundsCheck;
            chk.op1    = idxVal;
            chk.op2    = arrVal;
            emit(chk);
        }

        Instruction gep;
        gep.opcode = Opcode::GetArrayElemPtr;
        gep.result = nextValueId();
        gep.op1    = arrVal;
        gep.op2    = idxVal;
        gep.type   = elemTy;
        emit(gep);
        return gep.result;
    } else {
        auto valTy = checker_.getExprType(&objExpr);
        if (valTy && valTy->isReference()) {
            return lowerExpr(objExpr);
        }
        auto valId = lowerExpr(objExpr);
        Instruction allocaInst;
        allocaInst.opcode = Opcode::Alloca;
        allocaInst.result = nextValueId();
        allocaInst.type   = valTy;
        emit(allocaInst);

        Instruction store;
        store.opcode = Opcode::Store;
        store.op1    = allocaInst.result;
        store.op2    = valId;
        emit(store);
        return allocaInst.result;
    }
}

ValueId IRBuilder::lowerMatchExpr(MatchExpr& expr) {
    if (!currentBlock_ || isCurrentBlockTerminated() || !expr.expr) {
        return InvalidValue;
    }

    auto targetTy = checker_.getExprType(expr.expr.get());
    if (targetTy && targetTy->isReference()) {
        targetTy = targetTy->elementType;
    }
    if (!targetTy) return InvalidValue;

    auto resultTy = checker_.getExprType(&expr);
    if (!resultTy) resultTy = checker_.context().getI32();

    Instruction resultSlot;
    resultSlot.opcode = Opcode::Alloca;
    resultSlot.result = nextValueId();
    resultSlot.type   = resultTy;
    emit(resultSlot);

    ValueId targetVal = lowerExpr(*expr.expr);
    if (expr.expr->kind == ast::ExprKind::Identifier) {
        const auto& id = static_cast<const ast::IdentifierExpr&>(*expr.expr);
        markVariableMoved(id.name);
    }

    ValueId tagVal = InvalidValue;
    ValueId enumSlotPtr = InvalidValue;

    if (targetTy->isEnum()) {
        if (targetTy->isNicheOptimized) {
            Instruction tagInst;
            tagInst.opcode = Opcode::TagGet;
            tagInst.result = nextValueId();
            tagInst.op1    = targetVal;
            tagInst.type   = checker_.context().getI32();
            emit(tagInst);
            tagVal = tagInst.result;
        } else {
            if (expr.expr->kind == ast::ExprKind::Identifier) {
                const auto& id = static_cast<const ast::IdentifierExpr&>(*expr.expr);
                auto* b = lookupBinding(id.name);
                if (b && b->ptr != InvalidValue) {
                    enumSlotPtr = b->ptr;
                }
            }
            if (enumSlotPtr == InvalidValue) {
                Instruction allocaInst;
                allocaInst.opcode = Opcode::Alloca;
                allocaInst.result = nextValueId();
                allocaInst.type   = targetTy;
                emit(allocaInst);
                enumSlotPtr = allocaInst.result;

                Instruction store;
                store.opcode = Opcode::Store;
                store.op1    = enumSlotPtr;
                store.op2    = targetVal;
                emit(store);
            }

            Instruction tagInst;
            tagInst.opcode = Opcode::TagGet;
            tagInst.result = nextValueId();
            tagInst.op1    = enumSlotPtr;
            tagInst.type   = checker_.context().getI32();
            emit(tagInst);
            tagVal = tagInst.result;
        }
    } else {
        tagVal = targetVal;
    }

    auto mergeBlock = currentFunction_->createBlock(nextLabel("match_merge"));
    BasicBlock* defaultBlock = nullptr;
    std::vector<std::pair<int64_t, std::string>> switchCases;
    std::vector<BasicBlock*> caseBlocks;

    for (size_t i = 0; i < expr.cases.size(); ++i) {
        auto cb = currentFunction_->createBlock(nextLabel("match_case"));
        caseBlocks.push_back(cb);
        const auto& pat = expr.cases[i].pattern;

        if (pat.kind == MatchPatternKind::Wildcard || pat.kind == MatchPatternKind::Binding) {
            defaultBlock = cb;
        } else if (pat.kind == MatchPatternKind::Variant) {
            int vIdx = targetTy->isEnum() ? targetTy->getVariantIndex(pat.variantName) : -1;
            if (vIdx >= 0) {
                switchCases.push_back({ static_cast<int64_t>(vIdx), cb->label });
            }
        } else if (pat.kind == MatchPatternKind::Literal && pat.literalExpr) {
            if (pat.literalExpr->kind == ExprKind::IntLiteral) {
                auto& lit = static_cast<IntLiteralExpr&>(*pat.literalExpr);
                switchCases.push_back({ lit.value, cb->label });
            }
        }
    }

    bool synthesizedDefault = false;
    if (!defaultBlock) {
        defaultBlock = currentFunction_->createBlock(nextLabel("match_default"));
        synthesizedDefault = true;
    }

    Instruction sw;
    sw.opcode = Opcode::Switch;
    sw.op1 = tagVal;
    sw.label1 = defaultBlock->label;
    sw.switchCases = std::move(switchCases);
    emit(sw);

    bool anyCaseFallsThrough = false;

    for (size_t i = 0; i < expr.cases.size(); ++i) {
        setInsertBlock(caseBlocks[i]);
        pushScope();

        const auto& pat = expr.cases[i].pattern;
        if (pat.kind == MatchPatternKind::Binding && !pat.bindings.empty()) {
            const std::string& bName = pat.bindings[0];
            if (!bName.empty()) {
                Instruction bindSlot;
                bindSlot.opcode = Opcode::Alloca;
                bindSlot.result = nextValueId();
                bindSlot.type   = targetTy;
                emit(bindSlot);

                Instruction bindStore;
                bindStore.opcode = Opcode::Store;
                bindStore.op1    = bindSlot.result;
                bindStore.op2    = targetVal;
                emit(bindStore);

                setBinding(bName, Binding{ bindSlot.result, targetTy });
            }
        } else if (pat.kind == MatchPatternKind::Variant && targetTy->isEnum()) {
            int vIdx = targetTy->getVariantIndex(pat.variantName);
            if (vIdx >= 0 && !pat.bindings.empty()) {
                const auto& variant = targetTy->enumVariants[vIdx];
                for (size_t k = 0; k < pat.bindings.size(); ++k) {
                    const std::string& bName = pat.bindings[k];
                    const sema::Type* pTy = (k < variant.payloadTypes.size()) ? variant.payloadTypes[k] : nullptr;
                    ValueId extractedVal = InvalidValue;

                    if (targetTy->isNicheOptimized) {
                        extractedVal = targetVal;
                    } else {
                        int64_t currentOffset = 0;
                        for (size_t prev = 0; prev < k; ++prev) {
                            const auto* prevTy = variant.payloadTypes[prev];
                            size_t a = prevTy ? prevTy->alignment() : 1;
                            if (a == 0) a = 1;
                            currentOffset = (currentOffset + a - 1) & ~(a - 1);
                            if (prevTy) currentOffset += prevTy->size();
                        }
                        if (pTy) {
                            size_t a = pTy->alignment();
                            if (a == 0) a = 1;
                            currentOffset = (currentOffset + a - 1) & ~(a - 1);
                        }

                        Instruction gfp;
                        gfp.opcode = Opcode::VariantPayloadPtr;
                        gfp.result = nextValueId();
                        gfp.op1    = enumSlotPtr;
                        gfp.intImm = static_cast<int64_t>(k);
                        gfp.intImm2 = currentOffset;
                        gfp.type   = targetTy;
                        emit(gfp);

                        Instruction loadPayload;
                        loadPayload.opcode = Opcode::Load;
                        loadPayload.result = nextValueId();
                        loadPayload.op1    = gfp.result;
                        loadPayload.type   = pTy ? pTy : checker_.context().getI32();
                        emit(loadPayload);
                        extractedVal = loadPayload.result;
                    }

                    if (!bName.empty() && bName != "_") {
                        Instruction subSlot;
                        subSlot.opcode = Opcode::Alloca;
                        subSlot.result = nextValueId();
                        subSlot.type   = pTy;
                        emit(subSlot);

                        Instruction storeSub;
                        storeSub.opcode = Opcode::Store;
                        storeSub.op1    = subSlot.result;
                        storeSub.op2    = extractedVal;
                        emit(storeSub);

                        setBinding(bName, Binding{ subSlot.result, pTy });

                        if (pTy && pTy->hasNonTrivialDestructor() && !cleanupScopes_.empty()) {
                            ensureDestructor(pTy);
                            cleanupScopes_.back().push_back(CleanupItem{
                                .kind = CleanupKind::Destructor,
                                .ptr = subSlot.result,
                                .type = pTy,
                                .varName = bName,
                            });
                        }
                    }
                }
            }
        }

        if (expr.cases[i].body) {
            if (expr.cases[i].body->kind == StmtKind::Expr) {
                auto& es = static_cast<ExprStmt&>(*expr.cases[i].body);
                if (es.expr) {
                    ValueId caseVal = lowerExpr(*es.expr);
                    caseVal = emitCastIfNeeded(caseVal, checker_.getExprType(es.expr.get()), resultTy);
                    Instruction storeRes;
                    storeRes.opcode = Opcode::Store;
                    storeRes.op1    = resultSlot.result;
                    storeRes.op2    = caseVal;
                    emit(storeRes);
                }
            } else {
                lowerStmt(*expr.cases[i].body);
            }
        }

        popScope();

        if (currentBlock_ && !isCurrentBlockTerminated()) {
            anyCaseFallsThrough = true;
            Instruction jmp;
            jmp.opcode = Opcode::Jump;
            jmp.label1 = mergeBlock->label;
            emit(jmp);
        }
    }

    if (synthesizedDefault) {
        setInsertBlock(defaultBlock);
        if (anyCaseFallsThrough) {
            Instruction jmp;
            jmp.opcode = Opcode::Jump;
            jmp.label1 = mergeBlock->label;
            emit(jmp);
        } else {
            if (currentFunction_->returnType && !currentFunction_->returnType->isVoid()) {
                Instruction ret;
                ret.opcode = Opcode::Return;
                ret.op1    = emitIntConstant(0, currentFunction_->returnType);
                emit(ret);
            } else {
                Instruction ret;
                ret.opcode = Opcode::Return;
                ret.op1    = InvalidValue;
                emit(ret);
            }
        }
    }

    if (!anyCaseFallsThrough) {
        if (!currentFunction_->blocks.empty() && currentFunction_->blocks.back().get() == mergeBlock) {
            currentFunction_->blocks.pop_back();
        }
        currentBlock_ = nullptr;
        return InvalidValue;
    }

    setInsertBlock(mergeBlock);
    Instruction loadRes;
    loadRes.opcode = Opcode::Load;
    loadRes.result = nextValueId();
    loadRes.op1    = resultSlot.result;
    loadRes.type   = resultTy;
    emit(loadRes);
    return loadRes.result;
}

ValueId IRBuilder::lowerTryExpr(TryExpr& expr) {
    if (!currentBlock_ || isCurrentBlockTerminated() || !expr.operand) {
        return InvalidValue;
    }

    auto exprTy = checker_.getExprType(expr.operand.get());
    if (!exprTy || !exprTy->isEnum()) {
        return InvalidValue;
    }

    ValueId val = lowerExpr(*expr.operand);

    ValueId trySlotPtr = InvalidValue;
    ValueId tagVal = InvalidValue;

    if (exprTy->isNicheOptimized) {
        Instruction tagInst;
        tagInst.opcode = Opcode::TagGet;
        tagInst.result = nextValueId();
        tagInst.op1    = val;
        tagInst.type   = checker_.context().getI32();
        emit(tagInst);
        tagVal = tagInst.result;
    } else {
        Instruction trySlot;
        trySlot.opcode = Opcode::Alloca;
        trySlot.result = nextValueId();
        trySlot.type   = exprTy;
        emit(trySlot);
        trySlotPtr = trySlot.result;

        Instruction store;
        store.opcode = Opcode::Store;
        store.op1    = trySlotPtr;
        store.op2    = val;
        emit(store);

        Instruction tagInst;
        tagInst.opcode = Opcode::TagGet;
        tagInst.result = nextValueId();
        tagInst.op1    = trySlotPtr;
        tagInst.type   = checker_.context().getI32();
        emit(tagInst);
        tagVal = tagInst.result;
    }

    // Tag 0 is Ok/Some (success). Tag 1 is Err/None (error).
    Instruction cmp;
    cmp.opcode = Opcode::ICmpEq;
    cmp.result = nextValueId();
    cmp.type   = checker_.context().getBool();
    cmp.op1    = tagVal;
    cmp.op2    = emitIntConstant(0);
    emit(cmp);

    auto successBlock = currentFunction_->createBlock(nextLabel("try_ok"));
    auto errBlock     = currentFunction_->createBlock(nextLabel("try_err"));

    Instruction br;
    br.opcode = Opcode::Branch;
    br.op1    = cmp.result;
    br.label1 = successBlock->label;
    br.label2 = errBlock->label;
    emit(br);

    // In errBlock: early-return val
    setInsertBlock(errBlock);
    emitCleanupsUpTo(0);
    Instruction retInst;
    retInst.opcode = Opcode::Return;
    retInst.op1    = val;
    emit(retInst);
    currentBlock_ = nullptr;

    // In successBlock: extract payload 0
    setInsertBlock(successBlock);
    if (exprTy->isNicheOptimized) {
        return val;
    } else {
        Instruction gfp;
        gfp.opcode = Opcode::VariantPayloadPtr;
        gfp.result = nextValueId();
        gfp.op1    = trySlotPtr;
        gfp.intImm = 0;
        gfp.intImm2 = 0;
        gfp.type   = exprTy;
        emit(gfp);

        const sema::Type* innerTy = (!exprTy->enumVariants.empty() && !exprTy->enumVariants[0].payloadTypes.empty())
                                      ? exprTy->enumVariants[0].payloadTypes[0]
                                      : checker_.context().getI32();

        Instruction loadPayload;
        loadPayload.opcode = Opcode::Load;
        loadPayload.result = nextValueId();
        loadPayload.op1    = gfp.result;
        loadPayload.type   = innerTy;
        emit(loadPayload);
        return loadPayload.result;
    }
}

ValueId IRBuilder::lowerTernaryExpr(TernaryExpr& expr) {
    if (!currentBlock_ || isCurrentBlockTerminated() || !expr.condition) {
        return InvalidValue;
    }

    auto resultTy = checker_.getExprType(&expr);
    if (!resultTy) resultTy = checker_.context().getI32();

    // Allocate result slot for short-circuit branch merge
    Instruction resultSlot;
    resultSlot.opcode = Opcode::Alloca;
    resultSlot.result = nextValueId();
    resultSlot.type   = resultTy;
    emit(resultSlot);

    ValueId condVal = lowerExpr(*expr.condition);
    auto condTy = checker_.getExprType(expr.condition.get());
    if (condTy && !condTy->isBool()) {
        condVal = emitCastIfNeeded(condVal, condTy, checker_.context().getBool());
    }

    auto thenBlock  = currentFunction_->createBlock(nextLabel("tern.then"));
    auto elseBlock  = currentFunction_->createBlock(nextLabel("tern.else"));
    auto mergeBlock = currentFunction_->createBlock(nextLabel("tern.merge"));

    Instruction br;
    br.opcode = Opcode::Branch;
    br.op1    = condVal;
    br.label1 = thenBlock->label;
    br.label2 = elseBlock->label;
    emit(br);

    // Then branch
    setInsertBlock(thenBlock);
    ValueId thenVal = lowerExpr(*expr.thenExpr);
    thenVal = emitCastIfNeeded(thenVal, checker_.getExprType(expr.thenExpr.get()), resultTy);
    Instruction storeThen;
    storeThen.opcode = Opcode::Store;
    storeThen.op1    = resultSlot.result;
    storeThen.op2    = thenVal;
    emit(storeThen);
    if (currentBlock_ && !isCurrentBlockTerminated()) {
        Instruction jmp;
        jmp.opcode = Opcode::Jump;
        jmp.label1 = mergeBlock->label;
        emit(jmp);
    }

    // Else branch
    setInsertBlock(elseBlock);
    ValueId elseVal = lowerExpr(*expr.elseExpr);
    elseVal = emitCastIfNeeded(elseVal, checker_.getExprType(expr.elseExpr.get()), resultTy);
    Instruction storeElse;
    storeElse.opcode = Opcode::Store;
    storeElse.op1    = resultSlot.result;
    storeElse.op2    = elseVal;
    emit(storeElse);
    if (currentBlock_ && !isCurrentBlockTerminated()) {
        Instruction jmp;
        jmp.opcode = Opcode::Jump;
        jmp.label1 = mergeBlock->label;
        emit(jmp);
    }

    // Merge block
    setInsertBlock(mergeBlock);
    Instruction loadRes;
    loadRes.opcode = Opcode::Load;
    loadRes.result = nextValueId();
    loadRes.op1    = resultSlot.result;
    loadRes.type   = resultTy;
    emit(loadRes);
    return loadRes.result;
}

ValueId IRBuilder::lowerLambda(ast::LambdaExpr& expr) {
    std::string lambdaName = nextLabel("__tamarin_lambda");
    auto exprTy = checker_.getExprType(&expr);
    const sema::Type* retTy = (exprTy && exprTy->isFunction() && exprTy->returnType)
                                  ? exprTy->returnType
                                  : checker_.context().getI32();

    const auto* caps = checker_.getLambdaCaptures(&expr);
    bool hasCaptures = (caps && !caps->empty());

    ValueId envAllocPtr = InvalidValue;
    const sema::Type* envStructTy = nullptr;
    std::vector<const sema::Type*> envElemTypes;

    if (hasCaptures) {
        for (const auto& cap : *caps) {
            envElemTypes.push_back(checker_.context().getReference(cap.type, false));
        }
        envStructTy = checker_.context().getTuple(envElemTypes);

        Instruction envAlloca;
        envAlloca.opcode = Opcode::Alloca;
        envAlloca.result = nextValueId();
        envAlloca.type   = envStructTy;
        emit(envAlloca);
        envAllocPtr = envAlloca.result;

        for (size_t i = 0; i < caps->size(); ++i) {
            const auto& cap = (*caps)[i];
            const Binding* b = lookupBinding(cap.name);

            Instruction gfp;
            gfp.opcode = Opcode::GetFieldPtr;
            gfp.result = nextValueId();
            gfp.op1    = envAlloca.result;
            gfp.intImm = static_cast<int64_t>(i);
            gfp.type   = envElemTypes[i];
            emit(gfp);

            Instruction storeInst;
            storeInst.opcode = Opcode::Store;
            storeInst.op1    = gfp.result;
            storeInst.op2    = b ? b->ptr : InvalidValue;
            emit(storeInst);
        }
    }

    auto* parentFn = currentFunction_;
    auto* parentBlock = currentBlock_;
    auto savedScopeStack = std::move(scopeStack_);
    auto savedCleanupScopes = std::move(cleanupScopes_);
    scopeStack_.clear();
    cleanupScopes_.clear();

    auto lambdaFn = currentModule_->createFunction(lambdaName, retTy);

    // Param 0: hidden __env (ptr)
    ValueId envParamVal = nextValueId();
    const sema::Type* envPtrTy = hasCaptures
        ? checker_.context().getReference(envStructTy, false)
        : checker_.context().getReference(checker_.context().getI8(), false);
    lambdaFn->params.push_back(IRParam{"__env", envPtrTy, envParamVal});

    for (const auto& param : expr.params) {
        ValueId pVal = nextValueId();
        const sema::Type* pTy = nullptr;
        if (param.type && param.type->kind != ast::TypeNodeKind::Auto) {
            pTy = checker_.resolveType(*param.type);
        } else {
            pTy = checker_.context().getI32();
        }
        if (param.isArray) {
            pTy = checker_.context().getArrayView(pTy, param.isConst);
        }
        lambdaFn->params.push_back(IRParam{param.name, pTy, pVal});
    }

    currentFunction_ = lambdaFn;
    auto entryBlock = lambdaFn->createBlock("entry");
    setInsertBlock(entryBlock);
    pushScope();

    // Bind captures
    if (hasCaptures) {
        for (size_t i = 0; i < caps->size(); ++i) {
            const auto& cap = (*caps)[i];

            Instruction gfp;
            gfp.opcode = Opcode::GetFieldPtr;
            gfp.result = nextValueId();
            gfp.op1    = envParamVal;
            gfp.intImm = static_cast<int64_t>(i);
            gfp.type   = envElemTypes[i];
            emit(gfp);

            Instruction loadPtr;
            loadPtr.opcode = Opcode::Load;
            loadPtr.result = nextValueId();
            loadPtr.op1    = gfp.result;
            loadPtr.type   = envElemTypes[i];
            emit(loadPtr);

            Binding b;
            b.ptr = loadPtr.result;
            b.type = cap.type;
            b.isConst = !cap.isMutable;
            if (cap.type && cap.type->isArray() && cap.type->arraySize >= 0) {
                b.arrayLenValueId = emitIntConstant(cap.type->arraySize, checker_.context().getI64());
            }
            setBinding(cap.name, std::move(b));
        }
    }

    for (size_t i = 0; i < expr.params.size(); ++i) {
        const auto& param = expr.params[i];
        auto pTy = lambdaFn->params[i + 1].type;
        auto pVal = lambdaFn->params[i + 1].valueId;

        Instruction allocaInst;
        allocaInst.opcode = Opcode::Alloca;
        allocaInst.result = nextValueId();
        allocaInst.type   = pTy;
        emit(allocaInst);

        Instruction storeInst;
        storeInst.opcode = Opcode::Store;
        storeInst.op1    = allocaInst.result;
        storeInst.op2    = pVal;
        emit(storeInst);

        Binding b;
        b.ptr = allocaInst.result;
        b.type = pTy;
        b.isConst = param.isConst;
        b.isParameter = true;
        setBinding(param.name, std::move(b));
    }

    if (expr.exprBody) {
        ValueId bodyVal = lowerExpr(*expr.exprBody);
        bodyVal = emitCastIfNeeded(bodyVal, checker_.getExprType(expr.exprBody.get()), retTy);
        Instruction retInst;
        retInst.opcode = Opcode::Return;
        retInst.op1 = (retTy && !retTy->isVoid()) ? bodyVal : InvalidValue;
        emit(retInst);
    } else if (expr.blockBody) {
        lowerStmt(*expr.blockBody);
        if (currentBlock_ && !isCurrentBlockTerminated()) {
            Instruction retInst;
            retInst.opcode = Opcode::Return;
            retInst.op1 = (retTy && !retTy->isVoid()) ? emitIntConstant(0, retTy) : InvalidValue;
            emit(retInst);
        }
    } else {
        Instruction retInst;
        retInst.opcode = Opcode::Return;
        retInst.op1 = (retTy && !retTy->isVoid()) ? emitIntConstant(0, retTy) : InvalidValue;
        emit(retInst);
    }

    popScope();

    currentFunction_ = parentFn;
    setInsertBlock(parentBlock);
    scopeStack_ = std::move(savedScopeStack);
    cleanupScopes_ = std::move(savedCleanupScopes);

    Instruction fptr;
    fptr.opcode = Opcode::FunctionPtr;
    fptr.result = nextValueId();
    fptr.type   = exprTy ? exprTy : checker_.context().getFunction(retTy, {});
    fptr.strImm = lambdaName;
    fptr.op1    = hasCaptures ? envAllocPtr : InvalidValue;
    emit(fptr);
    return fptr.result;
}

} // namespace tamarin::ir
