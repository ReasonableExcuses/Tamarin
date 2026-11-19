// ============================================================================
// types.cpp - TypeContext implementation
// ============================================================================

#include "tamarin/sema/types.h"
#include <unordered_map>
#include <cstdint>

namespace tamarin::sema {

TypeContext::TypeContext() {
    float4Ty_.elementType = &f32Ty_;
    float4Ty_.vectorWidth = 4;

    float8Ty_.elementType = &f32Ty_;
    float8Ty_.vectorWidth = 8;

    int4Ty_.elementType = &i32Ty_;
    int4Ty_.vectorWidth = 4;

    int8Ty_.elementType = &i32Ty_;
    int8Ty_.vectorWidth = 8;

    int16Ty_.elementType = &i32Ty_;
    int16Ty_.vectorWidth = 16;
}

const Type* TypeContext::getSIMDVector(const Type* elem, uint32_t width) {
    if (elem == &f32Ty_) {
        if (width == 4) return &float4Ty_;
        if (width == 8) return &float8Ty_;
    } else if (elem == &i32Ty_) {
        if (width == 4) return &int4Ty_;
        if (width == 8) return &int8Ty_;
        if (width == 16) return &int16Ty_;
    }
    for (auto& t : arena_) {
        if (t->kind == TypeKind::SIMDVector && t->elementType == elem && t->vectorWidth == width) {
            return t.get();
        }
    }
    auto t = std::make_unique<Type>();
    t->kind = TypeKind::SIMDVector;
    t->elementType = elem;
    t->vectorWidth = width;
    arena_.push_back(std::move(t));
    return arena_.back().get();
}



const Type* TypeContext::getArray(const Type* elem, int64_t size, bool isConst) {
    for (auto& t : arena_) {
        if (t->kind == TypeKind::Array && t->elementType == elem && t->arraySize == size && t->isConst == isConst) {
            return t.get();
        }
    }
    auto t = std::make_unique<Type>();
    t->kind = TypeKind::Array;
    t->elementType = elem;
    t->arraySize = size;
    t->isConst = isConst;
    arena_.push_back(std::move(t));
    return arena_.back().get();
}



const Type* TypeContext::getArrayView(const Type* elem, bool isConst) {
    for (auto& t : arena_) {
        if (t->kind == TypeKind::ArrayView && t->elementType == elem && t->isConst == isConst) {
            return t.get();
        }
    }
    auto t = std::make_unique<Type>();
    t->kind = TypeKind::ArrayView;
    t->elementType = elem;
    t->isConst = isConst;
    arena_.push_back(std::move(t));
    return arena_.back().get();
}



const Type* TypeContext::getFunction(const Type* ret, std::vector<const Type*> params, std::vector<const ast::Expr*> defaultArgs) {
    for (auto& t : arena_) {
        if (t->kind == TypeKind::Function && t->returnType == ret && t->paramTypes == params && t->defaultArgs == defaultArgs) {
            return t.get();
        }
    }
    auto t = std::make_unique<Type>();
    t->kind = TypeKind::Function;
    t->returnType = ret;
    t->paramTypes = std::move(params);
    t->defaultArgs = std::move(defaultArgs);
    arena_.push_back(std::move(t));
    return arena_.back().get();
}

const Type* TypeContext::getTuple(std::vector<const Type*> elements) {
    for (auto& t : arena_) {
        if (t->kind == TypeKind::Tuple && t->tupleElements == elements) {
            return t.get();
        }
    }
    auto t = std::make_unique<Type>();
    t->kind = TypeKind::Tuple;
    t->tupleElements = std::move(elements);
    arena_.push_back(std::move(t));
    return arena_.back().get();
}



const Type* TypeContext::getReference(const Type* elem, bool isConst, bool isPointer) {
    for (auto& t : arena_) {
        if (t->kind == TypeKind::Reference && t->elementType == elem && t->isConstRef == isConst && t->isRawPointer == isPointer) {
            return t.get();
        }
    }
    auto t = std::make_unique<Type>();
    t->kind = TypeKind::Reference;
    t->elementType = elem;
    t->isConstRef = isConst;
    t->isRawPointer = isPointer;
    arena_.push_back(std::move(t));
    return arena_.back().get();
}



const Type* TypeContext::getHandle(const Type* elem) {
    for (auto& t : arena_) {
        if (t->kind == TypeKind::Handle && t->elementType == elem) {
            return t.get();
        }
    }
    auto t = std::make_unique<Type>();
    t->kind = TypeKind::Handle;
    t->elementType = elem;
    arena_.push_back(std::move(t));
    return arena_.back().get();
}

const Type* TypeContext::getTask(const Type* elem) {
    for (auto& t : arena_) {
        if (t->kind == TypeKind::Task && t->elementType == elem) {
            return t.get();
        }
    }
    auto t = std::make_unique<Type>();
    t->kind = TypeKind::Task;
    t->elementType = elem;
    arena_.push_back(std::move(t));
    return arena_.back().get();
}



const Type* TypeContext::getSlab(const Type* elem, int64_t capacity) {
    for (auto& t : arena_) {
        if (t->kind == TypeKind::Slab && t->elementType == elem && t->arraySize == capacity) {
            return t.get();
        }
    }
    auto t = std::make_unique<Type>();
    t->kind = TypeKind::Slab;
    t->elementType = elem;
    t->arraySize = capacity;
    arena_.push_back(std::move(t));
    return arena_.back().get();
}



static void computeRecordLayout(Type* t) {
    size_t currentOffset = 0;
    size_t maxAlign = 1;
    if (t->isPolymorphic) {
        currentOffset = 8;
        maxAlign = 8;
    }
    for (auto& f : t->structFields) {
        size_t fAlign = f.type ? f.type->alignment() : 1;
        size_t fSize = f.type ? f.type->size() : 0;
        if (fAlign > maxAlign) maxAlign = fAlign;
        size_t rem = currentOffset % fAlign;
        if (rem != 0) currentOffset += (fAlign - rem);
        f.offset = currentOffset;
        f.size = fSize;
        f.alignment = fAlign;
        currentOffset += fSize;
    }
    size_t rem = currentOffset % maxAlign;
    t->structSize = (rem != 0) ? currentOffset + (maxAlign - rem) : currentOffset;
    if (t->structSize == 0) t->structSize = 1;
    t->structAlignment = maxAlign;
}

const Type* TypeContext::getStruct(std::string name, std::vector<StructFieldType> fields, int layout) {
    for (auto& t : arena_) {
        if (t->isRecord() && t->structName == name) {
            if (t->structFields.empty() && !fields.empty()) {
                t->structFields = std::move(fields);
                t->layoutDirective = layout;
                computeRecordLayout(t.get());
            }
            return t.get();
        }
    }

    auto t = std::make_unique<Type>();
    t->kind = TypeKind::Struct;
    t->structName = std::move(name);
    t->structFields = std::move(fields);
    t->layoutDirective = layout;
    computeRecordLayout(t.get());

    arena_.push_back(std::move(t));
    return arena_.back().get();
}



const Type* TypeContext::getClass(std::string name, std::vector<StructFieldType> fields,
                                  int layout, std::string baseClass,
                                  bool isClass, bool isInterface) {
    for (auto& t : arena_) {
        if (t->isRecord() && t->structName == name) {
            t->structFields = std::move(fields);
            t->layoutDirective = layout;
            t->baseClassName = std::move(baseClass);
            t->isClassType = isClass;
            t->isInterfaceType = isInterface;
            if (isClass) t->kind = TypeKind::Class;
            else if (isInterface) t->kind = TypeKind::Interface;
            else t->kind = TypeKind::Struct;
            computeRecordLayout(t.get());
            return t.get();
        }
    }

    auto t = std::make_unique<Type>();
    t->kind = isClass ? TypeKind::Class : (isInterface ? TypeKind::Interface : TypeKind::Struct);
    t->structName = std::move(name);
    t->structFields = std::move(fields);
    t->layoutDirective = layout;
    t->baseClassName = std::move(baseClass);
    t->isClassType = isClass;
    t->isInterfaceType = isInterface;
    computeRecordLayout(t.get());

    arena_.push_back(std::move(t));
    return arena_.back().get();
}



const Type* TypeContext::findRecord(const std::string& name) const {
    for (const auto& t : arena_) {
        if (t->isRecord() && t->structName == name) {
            return t.get();
        }
    }
    return nullptr;
}

const Type* TypeContext::findStruct(const std::string& name) const {
    return findRecord(name);
}

const Type* TypeContext::findClass(const std::string& name) const {
    return findRecord(name);
}

std::vector<const Type*> TypeContext::getRecordTypes() const {
    std::vector<const Type*> res;
    for (const auto& t : arena_) {
        if (t->isRecord()) {
            res.push_back(t.get());
        }
    }
    return res;
}

std::vector<const Type*> TypeContext::getStructTypes() const {
    return getRecordTypes();
}

const Type* TypeContext::getEnum(std::string name, std::vector<EnumVariantType> variants, bool isNiche) {
    for (const auto& t : arena_) {
        if (t->kind == TypeKind::Enum && t->enumName == name) {
            if (!variants.empty() && t->enumVariants.empty()) {
                t->enumVariants = std::move(variants);
            }
            return t.get();
        }
    }
    auto ty = std::make_unique<Type>();
    ty->kind = TypeKind::Enum;
    ty->enumName = name;
    ty->isNicheOptimized = isNiche;

    size_t maxPayload = 0;
    size_t maxAlign = 1;

    for (int32_t i = 0; i < static_cast<int32_t>(variants.size()); ++i) {
        auto& v = variants[i];
        v.tag = i;
        size_t currentOffset = 0;
        size_t varMaxAlign = 1;
        for (const auto* pTy : v.payloadTypes) {
            if (pTy) {
                size_t a = pTy->alignment();
                if (a == 0) a = 1;
                currentOffset = (currentOffset + a - 1) & ~(a - 1);
                currentOffset += pTy->size();
                if (a > varMaxAlign) varMaxAlign = a;
            }
        }
        if (varMaxAlign > 0) {
            currentOffset = (currentOffset + varMaxAlign - 1) & ~(varMaxAlign - 1);
        }
        v.payloadSize = currentOffset;
        v.payloadAlignment = varMaxAlign;

        if (v.payloadSize > maxPayload) maxPayload = v.payloadSize;
        if (v.payloadAlignment > maxAlign) maxAlign = v.payloadAlignment;
    }

    ty->enumVariants = std::move(variants);
    ty->maxPayloadSize = maxPayload;
    ty->maxPayloadAlignment = maxAlign;

    auto* ptr = ty.get();
    arena_.push_back(std::move(ty));
    return ptr;
}

const Type* TypeContext::findEnum(const std::string& name) const {
    for (const auto& t : arena_) {
        if (t->kind == TypeKind::Enum && t->enumName == name) {
            return t.get();
        }
    }
    return nullptr;
}

std::vector<const Type*> TypeContext::getEnumTypes() const {
    std::vector<const Type*> res;
    for (const auto& t : arena_) {
        if (t->kind == TypeKind::Enum) {
            res.push_back(t.get());
        }
    }
    return res;
}

const Type* TypeContext::getCommonArithmeticType(const Type* lhs, const Type* rhs) const noexcept {
    if (!lhs && !rhs) return getI32();
    if (!lhs) return rhs;
    if (!rhs) return lhs;
    if (lhs == rhs || *lhs == *rhs) return lhs;

    // Floating-point operations
    if (lhs->isFloat() || rhs->isFloat()) {
        if (lhs->isFloat() && rhs->isFloat()) {
            return (lhs->bitWidth() >= rhs->bitWidth()) ? lhs : rhs;
        }
        const Type* floatTy = lhs->isFloat() ? lhs : rhs;
        const Type* intTy   = lhs->isFloat() ? rhs : lhs;
        if (intTy->bitWidth() > floatTy->bitWidth()) {
            return getF64();
        }
        return floatTy;
    }

    // Integer operations
    if (lhs->isInteger() && rhs->isInteger()) {
        int lBits = lhs->bitWidth();
        int rBits = rhs->bitWidth();
        bool lSigned = lhs->isSigned();
        bool rSigned = rhs->isSigned();

        // Same signedness: wider integer wins; if same bit width, use canonical kind tie-break
        if (lSigned == rSigned) {
            if (lBits != rBits) {
                return (lBits > rBits) ? lhs : rhs;
            }
            return (lhs->kind <= rhs->kind) ? lhs : rhs;
        }

        // Mixed signedness:
        const Type* signedTy   = lSigned ? lhs : rhs;
        const Type* unsignedTy = lSigned ? rhs : lhs;
        int sBits = signedTy->bitWidth();
        int uBits = unsignedTy->bitWidth();

        // If unsigned operand is >= width of signed operand, promote to unsigned
        // (modular arithmetic: e.g. u32 + i32 -> u32; u64 + i32 -> u64; u64 + i64 -> u64)
        if (uBits >= sBits) {
            return unsignedTy;
        }

        // If signed operand is strictly wider than unsigned operand (e.g. i64 vs u32),
        // the wider signed type can represent all positive values of unsigned without truncation.
        return signedTy;
    }

    return lhs->size() >= rhs->size() ? lhs : rhs;
}

bool Type::hasNonTrivialDestructor() const noexcept {
    if (isReference()) return false;
    if (isSlab()) return true;
    if (destructor != nullptr) return true;
    if (!isRecord() && !isArray() && !isEnum() && !isTuple()) return false;

    try {
        enum class Color : uint8_t { Gray, Black };
        std::unordered_map<const Type*, Color> visited;

        struct StackFrame {
            const Type* node;
            size_t childIdx;
        };

        std::vector<StackFrame> stack;
        visited[this] = Color::Gray;
        stack.push_back({this, 0});

        auto getChildCount = [](const Type* t) -> size_t {
            if (t->isRecord()) {
                size_t count = (t->baseClass != nullptr) ? 1 : 0;
                for (const auto& f : t->structFields) {
                    if (f.name == "_vptr") continue;
                    if (!f.type || f.type->isReference()) continue;
                    count++;
                }
                return count;
            }
            if (t->isArray()) {
                return (t->elementType && !t->elementType->isReference()) ? 1 : 0;
            }
            if (t->isEnum()) {
                size_t count = 0;
                for (const auto& v : t->enumVariants) {
                    for (const auto* pTy : v.payloadTypes) {
                        if (pTy && !pTy->isReference()) count++;
                    }
                }
                return count;
            }
            if (t->isTuple()) {
                size_t count = 0;
                for (const auto* elem : t->tupleElements) {
                    if (elem && !elem->isReference()) count++;
                }
                return count;
            }
            return 0;
        };

        auto getChild = [](const Type* t, size_t idx) -> const Type* {
            if (t->isRecord()) {
                size_t current = 0;
                if (t->baseClass) {
                    if (idx == current) return t->baseClass;
                    current++;
                }
                for (const auto& f : t->structFields) {
                    if (f.name == "_vptr") continue;
                    if (!f.type || f.type->isReference()) continue;
                    if (idx == current) return f.type;
                    current++;
                }
                return nullptr;
            }
            if (t->isArray()) {
                if (idx == 0 && t->elementType && !t->elementType->isReference()) {
                    return t->elementType;
                }
                return nullptr;
            }
            if (t->isEnum()) {
                size_t current = 0;
                for (const auto& v : t->enumVariants) {
                    for (const auto* pTy : v.payloadTypes) {
                        if (pTy && !pTy->isReference()) {
                            if (idx == current) return pTy;
                            current++;
                        }
                    }
                }
                return nullptr;
            }
            if (t->isTuple()) {
                size_t current = 0;
                for (const auto* elem : t->tupleElements) {
                    if (elem && !elem->isReference()) {
                        if (idx == current) return elem;
                        current++;
                    }
                }
                return nullptr;
            }
            return nullptr;
        };

        while (!stack.empty()) {
            StackFrame& frame = stack.back();
            const Type* curr = frame.node;
            size_t numChildren = getChildCount(curr);

            if (frame.childIdx < numChildren) {
                const Type* child = getChild(curr, frame.childIdx);
                frame.childIdx++;

                if (!child || child->isReference()) {
                    continue;
                }

                if (child->destructor != nullptr) {
                    // Child has an explicit destructor -> whole composite is non-trivial
                    return true;
                }

                if (!child->isRecord() && !child->isArray() && !child->isEnum() && !child->isTuple()) {
                    // Primitive or non-composite leaf -> trivial
                    continue;
                }

                auto it = visited.find(child);
                if (it != visited.end()) {
                    if (it->second == Color::Gray) {
                        // Cycle detected in active exploration path -> conservative non-trivial
                        return true;
                    }
                    // Color is Black -> previously explored and verified trivial
                    continue;
                }

                // Node is White -> mark Gray and push to explore
                visited[child] = Color::Gray;
                stack.push_back({child, 0});
            } else {
                // All children of curr verified trivial without cycles
                visited[curr] = Color::Black;
                stack.pop_back();
            }
        }

        return false;
    } catch (...) {
        // Any host allocation exception: fail conservatively
        return true;
    }
}

bool Type::canContainBorrow() const noexcept {
    if (isReference() || isArrayView() || isFunction()) return true;
    if (!isRecord() && !isArray() && !isEnum() && !isTuple()) return false;

    try {
        enum class Color : uint8_t { Gray, Black };
        std::unordered_map<const Type*, Color> visited;

        struct StackFrame {
            const Type* node;
            size_t childIdx;
        };

        std::vector<StackFrame> stack;
        visited[this] = Color::Gray;
        stack.push_back({this, 0});

        while (!stack.empty()) {
            auto& [curr, childIdx] = stack.back();

            size_t numChildren = 0;
            if (curr->isRecord()) {
                numChildren = curr->structFields.size() + (curr->baseClass ? 1 : 0);
            } else if (curr->isArray() && curr->elementType) {
                numChildren = 1;
            } else if (curr->isEnum()) {
                for (const auto& v : curr->enumVariants) {
                    numChildren += v.payloadTypes.size();
                }
            } else if (curr->isTuple()) {
                numChildren = curr->tupleElements.size();
            }

            if (childIdx < numChildren) {
                const Type* child = nullptr;
                if (curr->isRecord()) {
                    if (childIdx < curr->structFields.size()) {
                        const auto& f = curr->structFields[childIdx];
                        childIdx++;
                        if (f.name == "_vptr") continue;
                        child = f.type;
                    } else {
                        childIdx++;
                        child = curr->baseClass;
                    }
                } else if (curr->isArray()) {
                    childIdx++;
                    child = curr->elementType;
                } else if (curr->isEnum()) {
                    size_t cur = 0;
                    size_t targetIdx = childIdx++;
                    for (const auto& v : curr->enumVariants) {
                        for (const auto* pTy : v.payloadTypes) {
                            if (cur == targetIdx) {
                                child = pTy;
                                break;
                            }
                            cur++;
                        }
                        if (child) break;
                    }
                } else if (curr->isTuple()) {
                    child = curr->tupleElements[childIdx++];
                }

                if (!child) continue;

                if (child->isReference() || child->isArrayView()) {
                    return true;
                }

                if (!child->isRecord() && !child->isArray() && !child->isEnum() && !child->isTuple()) {
                    continue;
                }

                auto it = visited.find(child);
                if (it != visited.end()) {
                    if (it->second == Color::Gray) {
                        // Cycle detected in active exploration path -> conservative true
                        return true;
                    }
                    continue; // Black -> already verified
                }

                visited[child] = Color::Gray;
                stack.push_back({child, 0});
            } else {
                visited[curr] = Color::Black;
                stack.pop_back();
            }
        }

        return false;
    } catch (...) {
        return true;
    }
}

} // namespace tamarin::sema

