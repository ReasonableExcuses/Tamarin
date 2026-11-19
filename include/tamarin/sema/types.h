// ============================================================================
// types.h - Tamarin type representation for semantic analysis
// ============================================================================
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace tamarin::ast {
enum class LayoutDirective;
struct Expr;
}

namespace tamarin::sema {

enum class TypeKind {
    Void,
    Bool,
    // Signed integers
    I8, I16, I32, I64, I128, Isize,
    // Unsigned integers
    U8, U16, U32, U64, U128, Usize,
    // Floating point
    F32, F64,
    // Compound types
    Array,      // Fixed-size array T[N]
    ArrayView,  // Bounded view T[] (ptr + len)
    Function,   // (T1, T2) -> R
    Named,      // User-defined / unresolved
    Struct,     // User-defined struct
    Class,      // User-defined class
    Interface,  // User-defined interface
    Reference,  // Reference T& or const T&
    Handle,     // Generational handle Handle<T>
    Slab,       // Generational slab Slab<T>
    Enum,       // Tagged union (Algebraic Data Type)
    SIMDVector, // First-class SIMD vector: float4, float8, int4, int8, int16
    Tuple,      // Anonymous product type (T1, T2, ...)
    Task,       // In-flight asynchronous computation token Task<T>
};

struct Type;

struct EnumVariantType {
    std::string name;
    int32_t tag = 0;
    std::vector<const Type*> payloadTypes;
    size_t payloadSize = 0;
    size_t payloadAlignment = 1;
};

struct StructFieldType {
    std::string name;
    const Type* type = nullptr;
    size_t offset = 0;
    size_t size = 0;
    size_t alignment = 1;
    bool isConst = false;
    bool isCold = false;
    int access = 0; // 0 = Public, 1 = Private, 2 = Protected
};

struct MethodType {
    std::string name;
    std::string definingClass;
    const Type* returnType = nullptr;
    std::vector<const Type*> paramTypes;
    std::vector<std::string> paramNames;
    std::vector<const ast::Expr*> defaultArgs;
    bool isConst = false;
    bool isVirtual = false;
    bool isOverride = false;
    bool isPureVirtual = false;
    bool isAsync = false;
    int vtableSlot = -1; // -1 if non-virtual
    int access = 0;      // 0 = Public, 1 = Private, 2 = Protected
};

struct ConstructorType {
    std::vector<const Type*> paramTypes;
    std::vector<std::string> paramNames;
    int access = 0;      // 0 = Public, 1 = Private, 2 = Protected
};

struct DestructorType {
    bool isVirtual = false;
    int access = 0;      // 0 = Public, 1 = Private, 2 = Protected
};

inline std::string mangleMethodName(const std::string& recName, const std::string& methodName, size_t paramCount = 1) {
    if (methodName == "operator+") return recName + (paramCount == 0 ? "_op_pos" : "_op_add");
    if (methodName == "operator-") return recName + (paramCount == 0 ? "_op_neg" : "_op_sub");
    if (methodName == "operator*") return recName + "_op_mul";
    if (methodName == "operator/") return recName + "_op_div";
    if (methodName == "operator%") return recName + "_op_mod";
    if (methodName == "operator==") return recName + "_op_eq";
    if (methodName == "operator!=") return recName + "_op_ne";
    if (methodName == "operator<") return recName + "_op_lt";
    if (methodName == "operator<=") return recName + "_op_le";
    if (methodName == "operator>") return recName + "_op_gt";
    if (methodName == "operator>=") return recName + "_op_ge";
    if (methodName == "operator[]") return recName + "_op_index";
    if (methodName == "operator!") return recName + "_op_not";
    if (methodName == "operator~") return recName + "_op_bitnot";
    if (methodName == "operator&") return recName + (paramCount == 0 ? "_op_addressof" : "_op_bitand");
    if (methodName == "operator|") return recName + "_op_bitor";
    if (methodName == "operator^") return recName + "_op_bitxor";
    if (methodName == "operator<<") return recName + "_op_shl";
    if (methodName == "operator>>") return recName + "_op_shr";
    return recName + "_" + methodName;
}

struct Type {
    TypeKind kind;
    const Type* elementType = nullptr;   // for Array, ArrayView, and Reference
    int64_t     arraySize   = -1;        // for fixed Array (-1 = inferred/unknown)
    const Type* returnType  = nullptr;   // for Function
    std::vector<const Type*> paramTypes; // for Function
    std::vector<const ast::Expr*> defaultArgs; // for Function
    std::string name;                    // for Named types

    // Struct / Class metadata
    std::string structName;
    std::vector<StructFieldType> structFields;
    size_t structSize = 0;
    size_t structAlignment = 1;
    int layoutDirective = 0; // cast from ast::LayoutDirective

    // OOP metadata
    std::string baseClassName;
    const Type* baseClass = nullptr;
    std::vector<MethodType> methods;
    std::vector<ConstructorType> constructors;
    std::unique_ptr<DestructorType> destructor;
    std::vector<std::string> vtable;
    bool isPolymorphic = false;
    bool isAbstract = false;
    bool isClassType = false;
    bool isInterfaceType = false;

    // Reference metadata
    bool isConstRef = false;
    bool isConst = false;
    bool isRawPointer = false;

    // SIMD Vector metadata
    uint32_t vectorWidth = 0;

    // Enum / Tagged Union metadata
    std::string enumName;
    std::vector<EnumVariantType> enumVariants;
    size_t maxPayloadSize = 0;
    size_t maxPayloadAlignment = 1;
    bool isNicheOptimized = false;

    // Tuple metadata
    std::vector<const Type*> tupleElements;

    [[nodiscard]] bool isTuple() const noexcept {
        return kind == TypeKind::Tuple;
    }

    [[nodiscard]] bool isRecord() const noexcept {
        return kind == TypeKind::Struct || kind == TypeKind::Class || kind == TypeKind::Interface;
    }

    [[nodiscard]] bool isStruct() const noexcept {
        return isRecord();
    }

    [[nodiscard]] bool isClass() const noexcept {
        return kind == TypeKind::Class || (isRecord() && isClassType);
    }

    [[nodiscard]] bool isInterface() const noexcept {
        return kind == TypeKind::Interface || (isRecord() && isInterfaceType);
    }

    [[nodiscard]] bool isReference() const noexcept {
        return kind == TypeKind::Reference;
    }

    [[nodiscard]] bool isPointer() const noexcept {
        return kind == TypeKind::Reference && isRawPointer;
    }

    [[nodiscard]] bool isRef() const noexcept {
        return kind == TypeKind::Reference && !isRawPointer;
    }

    [[nodiscard]] bool isHandle() const noexcept {
        return kind == TypeKind::Handle;
    }

    [[nodiscard]] bool isSlab() const noexcept {
        return kind == TypeKind::Slab;
    }

    [[nodiscard]] bool isEnum() const noexcept {
        return kind == TypeKind::Enum;
    }

    [[nodiscard]] bool isSIMDVector() const noexcept {
        return kind == TypeKind::SIMDVector;
    }

    [[nodiscard]] bool isFunction() const noexcept {
        return kind == TypeKind::Function;
    }

    [[nodiscard]] bool isTask() const noexcept {
        return kind == TypeKind::Task;
    }

    [[nodiscard]] const EnumVariantType* findVariant(const std::string& variantName) const {
        for (const auto& v : enumVariants) {
            if (v.name == variantName) return &v;
        }
        return nullptr;
    }

    [[nodiscard]] int getVariantIndex(const std::string& variantName) const {
        for (size_t i = 0; i < enumVariants.size(); ++i) {
            if (enumVariants[i].name == variantName) return static_cast<int>(i);
        }
        return -1;
    }

    [[nodiscard]] bool hasNonTrivialDestructor() const noexcept;
    [[nodiscard]] bool canContainBorrow() const noexcept;

    [[nodiscard]] std::string destructorName() const {
        if (isRecord()) return structName + "_destructor";
        if (isEnum()) {
            std::string s = enumName;
            for (char& c : s) {
                if (c == '<' || c == '>' || c == ',' || c == ' ' || c == ':' || c == '&' || c == '*' || c == '[' || c == ']') {
                    c = '_';
                }
            }
            return s + "_destructor";
        }
        if (isArray() && elementType) return elementType->destructorName();
        return "";
    }

    [[nodiscard]] bool hasResourceOwningClass() const noexcept {
        if (isClass() && hasNonTrivialDestructor()) return true;
        if (isRecord()) {
            for (const auto& f : structFields) {
                if (f.type && f.type->hasResourceOwningClass()) return true;
            }
        }
        return false;
    }

    [[nodiscard]] const StructFieldType* findField(const std::string& fieldName) const {
        for (const auto& f : structFields) {
            if (f.name == fieldName) return &f;
        }
        if (baseClass) return baseClass->findField(fieldName);
        return nullptr;
    }

    [[nodiscard]] int getFieldIndex(const std::string& fieldName) const {
        for (size_t i = 0; i < structFields.size(); ++i) {
            if (structFields[i].name == fieldName) return static_cast<int>(i);
        }
        return -1;
    }

    [[nodiscard]] const MethodType* findMethod(const std::string& methodName) const {
        for (const auto& m : methods) {
            if (m.name == methodName) return &m;
        }
        if (baseClass) return baseClass->findMethod(methodName);
        return nullptr;
    }

    [[nodiscard]] const MethodType* findMethod(const std::string& methodName, size_t paramCount) const {
        for (const auto& m : methods) {
            if (m.name == methodName && m.paramTypes.size() == paramCount) return &m;
        }
        if (baseClass) return baseClass->findMethod(methodName, paramCount);
        return nullptr;
    }

    [[nodiscard]] bool inheritsFrom(const Type* other) const noexcept {
        if (!other) return false;
        const Type* cur = baseClass;
        while (cur) {
            if (cur == other || cur->structName == other->structName) return true;
            cur = cur->baseClass;
        }
        return false;
    }

    [[nodiscard]] bool isInteger() const noexcept {
        return (kind >= TypeKind::I8 && kind <= TypeKind::Isize) ||
               (kind >= TypeKind::U8 && kind <= TypeKind::Usize);
    }

    [[nodiscard]] bool isSigned() const noexcept {
        return kind >= TypeKind::I8 && kind <= TypeKind::Isize;
    }

    [[nodiscard]] bool isSignedInteger() const noexcept {
        return isSigned();
    }

    [[nodiscard]] bool isUnsigned() const noexcept {
        return kind >= TypeKind::U8 && kind <= TypeKind::Usize;
    }

    [[nodiscard]] bool isUnsignedInteger() const noexcept {
        return isUnsigned();
    }

    [[nodiscard]] size_t alignment() const noexcept {
        switch (kind) {
            case TypeKind::Bool:
            case TypeKind::I8:
            case TypeKind::U8:    return 1;
            case TypeKind::I16:
            case TypeKind::U16:   return 2;
            case TypeKind::I32:
            case TypeKind::U32:
            case TypeKind::F32:   return 4;
            case TypeKind::I64:
            case TypeKind::U64:
            case TypeKind::F64:
            case TypeKind::Usize:
            case TypeKind::Isize: return 8;
            case TypeKind::I128:
            case TypeKind::U128:  return 16;
            case TypeKind::Array:
                return elementType ? elementType->alignment() : 1;
            case TypeKind::ArrayView:
                return 8;
            case TypeKind::Reference:
                return 8;
            case TypeKind::Handle:
                return 8;
            case TypeKind::Function:
                return 8;
            case TypeKind::Task:
                return 8;
            case TypeKind::Slab:
                return 8;
            case TypeKind::Struct:
            case TypeKind::Class:
            case TypeKind::Interface:
                return structAlignment > 0 ? structAlignment : 1;
            case TypeKind::Enum:
                if (isNicheOptimized) return 8;
                return std::max((size_t)4, maxPayloadAlignment);
            case TypeKind::SIMDVector:
                return size() >= 64 ? 64 : (size() >= 32 ? 32 : 16);
            case TypeKind::Tuple: {
                size_t maxAlign = 1;
                for (auto el : tupleElements) {
                    if (el) {
                        size_t a = el->alignment();
                        if (a > maxAlign) maxAlign = a;
                    }
                }
                return maxAlign;
            }
            default: return 1;
        }
    }

    [[nodiscard]] size_t size() const noexcept {
        if (isTuple()) {
            size_t total = 0;
            size_t maxAlign = 1;
            for (auto el : tupleElements) {
                if (!el) continue;
                size_t a = el->alignment();
                if (a > maxAlign) maxAlign = a;
                total = (total + a - 1) & ~(a - 1);
                total += el->size();
            }
            return (total + maxAlign - 1) & ~(maxAlign - 1);
        }
        if (isSIMDVector()) {
            return vectorWidth * (elementType ? elementType->size() : 4);
        }
        if (isReference()) return 8;
        if (isHandle()) return 8;
        if (isTask()) return 8;
        if (isFunction()) return 16;
        if (isSlab()) return 88;
        if (isEnum()) {
            if (isNicheOptimized) return 8;
            if (maxPayloadSize == 0) return 4;
            size_t payloadOffset = std::max((size_t)4, maxPayloadAlignment);
            size_t total = payloadOffset + maxPayloadSize;
            size_t align = alignment();
            return (total + align - 1) & ~(align - 1);
        }
        if (isRecord()) return structSize;
        if (isArray()) {
            return (arraySize >= 0 && elementType) ? static_cast<size_t>(arraySize) * elementType->size() : 0;
        }
        if (isArrayView()) return 16;
        int bits = bitWidth();
        return bits > 0 ? static_cast<size_t>((bits + 7) / 8) : 4;
    }

    [[nodiscard]] bool isFloat() const noexcept {
        return kind == TypeKind::F32 || kind == TypeKind::F64;
    }

    [[nodiscard]] bool isNumeric() const noexcept {
        return isInteger() || isFloat();
    }

    [[nodiscard]] bool isVoid() const noexcept {
        return kind == TypeKind::Void;
    }

    [[nodiscard]] bool isBool() const noexcept {
        return kind == TypeKind::Bool;
    }

    [[nodiscard]] bool isArray() const noexcept {
        return kind == TypeKind::Array;
    }

    [[nodiscard]] bool isArrayView() const noexcept {
        return kind == TypeKind::ArrayView;
    }

    [[nodiscard]] int bitWidth() const noexcept {
        switch (kind) {
            case TypeKind::Bool:  return 1;
            case TypeKind::I8:
            case TypeKind::U8:    return 8;
            case TypeKind::I16:
            case TypeKind::U16:   return 16;
            case TypeKind::I32:
            case TypeKind::U32:
            case TypeKind::F32:   return 32;
            case TypeKind::I64:
            case TypeKind::U64:
            case TypeKind::F64:
            case TypeKind::Usize:
            case TypeKind::Isize: return 64;
            case TypeKind::I128:
            case TypeKind::U128:  return 128;
            default:              return 0;
        }
    }

    bool operator==(const Type& other) const noexcept {
        if (kind != other.kind) {
            if (isRecord() && other.isRecord()) {
                return structName == other.structName;
            }
            return false;
        }
        if (isReference()) {
            return isConstRef == other.isConstRef && isRawPointer == other.isRawPointer && *elementType == *other.elementType;
        }
        if (isRecord()) {
            return structName == other.structName;
        }
        if (isArray()) {
            return isConst == other.isConst &&
                   arraySize == other.arraySize &&
                   *elementType == *other.elementType;
        }
        if (isArrayView()) {
            return isConst == other.isConst &&
                   *elementType == *other.elementType;
        }
        if (isSIMDVector()) {
            return vectorWidth == other.vectorWidth && elementType && other.elementType && *elementType == *other.elementType;
        }
        if (isHandle()) {
            return (!elementType && !other.elementType) || (elementType && other.elementType && *elementType == *other.elementType);
        }
        if (isTask()) {
            return (!elementType && !other.elementType) || (elementType && other.elementType && *elementType == *other.elementType);
        }
        if (isSlab()) {
            return (!elementType && !other.elementType) || (elementType && other.elementType && *elementType == *other.elementType);
        }
        if (isEnum()) {
            return enumName == other.enumName;
        }
        if (isTuple() && other.isTuple()) {
            if (tupleElements.size() != other.tupleElements.size()) return false;
            for (size_t i = 0; i < tupleElements.size(); ++i) {
                if (*tupleElements[i] != *other.tupleElements[i]) return false;
            }
            return true;
        }
        if (kind == TypeKind::Function) {
            if (*returnType != *other.returnType) return false;
            if (paramTypes.size() != other.paramTypes.size()) return false;
            for (size_t i = 0; i < paramTypes.size(); ++i) {
                if (*paramTypes[i] != *other.paramTypes[i]) return false;
            }
            return true;
        }
        return true;
    }

    bool operator!=(const Type& other) const noexcept {
        return !(*this == other);
    }

    /// Check if this type can be implicitly converted to `target`.
    /// Tamarin rules: very restrictive, same-family widening or exact match.
    [[nodiscard]] bool isImplicitlyConvertibleTo(const Type& target) const noexcept {
        if (*this == target) return true;
        // Reference / pointer target
        if (target.isReference()) {
            const Type* targetElem = target.elementType;
            if (!targetElem || targetElem->isVoid()) {
                if (isReference() && isConstRef && !target.isConstRef) {
                    return false;
                }
                return true;
            }
            if (isArray() || isArrayView()) {
                if (elementType && (*elementType == *targetElem || targetElem->isVoid())) {
                    if ((isConst || (elementType && elementType->isConst)) && !target.isConstRef) return false;
                    return true;
                }
            }
            const Type* srcElem = isReference() ? elementType : this;
            if (*srcElem == *targetElem) {
                if (isReference() && target.isPointer() && isConstRef && !target.isConstRef) {
                    return false;
                }
                return true;
            }
            if (srcElem->isRecord() && targetElem->isRecord() && srcElem->inheritsFrom(targetElem)) {
                return true;
            }
            return false;
        }
        // Reference source decaying/dereferencing to value
        if (isReference()) {
            return elementType->isImplicitlyConvertibleTo(target);
        }
        // Records: same struct or derived to base conversion
        if (isRecord() && target.isRecord()) {
            if (structName == target.structName) return true;
            if (inheritsFrom(&target)) return true;
        }
        // Array T[N] can decay to ArrayView T[]
        if (isArray() && target.isArrayView()) {
            if (!target.isConst && isConst) return false;
            return *elementType == *target.elementType;
        }
        // ArrayView can convert to const ArrayView
        if (isArrayView() && target.isArrayView()) {
            if (!target.isConst && isConst) return false;
            return *elementType == *target.elementType;
        }
        // Array initialization conversion
        if (isArray() && target.isArray()) {
            return (target.arraySize < 0 || arraySize <= target.arraySize) &&
                   elementType->isImplicitlyConvertibleTo(*target.elementType);
        }
        // Integer conversion: allow between all integer types (widening and narrowing)
        if (isInteger() && target.isInteger()) {
            return true;
        }
        // Float widening (f32 -> f64)
        if (isFloat() && target.isFloat()) {
            return bitWidth() <= target.bitWidth();
        }
        // Integer to float promotion
        if (isInteger() && target.isFloat()) {
            return true;
        }
        // Enum conversion (e.g. Result<T, int> from Result::Ok to Result<T, E>)
        if (isEnum() && target.isEnum()) {
            if (enumName == target.enumName) return true;
            if (enumName.rfind("Result<", 0) == 0 && target.enumName.rfind("Result<", 0) == 0) {
                if (enumVariants.size() == 2 && target.enumVariants.size() == 2) {
                    bool ok0Matches = (!enumVariants[0].payloadTypes.empty() && !target.enumVariants[0].payloadTypes.empty() &&
                                       *enumVariants[0].payloadTypes[0] == *target.enumVariants[0].payloadTypes[0]);
                    bool err1Matches = (!enumVariants[1].payloadTypes.empty() && !target.enumVariants[1].payloadTypes.empty() &&
                                        *enumVariants[1].payloadTypes[0] == *target.enumVariants[1].payloadTypes[0]);
                    if (ok0Matches || err1Matches) return true;
                }
            }
        }
        return false;
    }

    [[nodiscard]] std::string toString() const {
        switch (kind) {
            case TypeKind::Void:     return "void";
            case TypeKind::Bool:     return "bool";
            case TypeKind::I8:       return "i8";
            case TypeKind::I16:      return "i16";
            case TypeKind::I32:      return "int";
            case TypeKind::I64:      return "long";
            case TypeKind::I128:     return "i128";
            case TypeKind::Isize:    return "isize";
            case TypeKind::U8:       return "u8";
            case TypeKind::U16:      return "u16";
            case TypeKind::U32:      return "unsigned int";
            case TypeKind::U64:      return "unsigned long";
            case TypeKind::U128:     return "u128";
            case TypeKind::Usize:    return "size_t";
            case TypeKind::F32:      return "float";
            case TypeKind::F64:      return "double";
            case TypeKind::Struct:   return (isClassType ? "class " : (isInterfaceType ? "interface " : "struct ")) + structName;
            case TypeKind::Class:    return "class " + structName;
            case TypeKind::Interface:return "interface " + structName;
            case TypeKind::Reference:return (isConstRef ? "const " : "") + (elementType ? elementType->toString() : "void") + (isRawPointer ? "*" : "&");
            case TypeKind::Handle:   return "Handle<" + (elementType ? elementType->toString() : "?") + ">";
            case TypeKind::Task:     return "Task<" + (elementType ? elementType->toString() : "void") + ">";
            case TypeKind::Slab:     return "Slab<" + (elementType ? elementType->toString() : "?") + ">";
            case TypeKind::Enum:     return enumName;
            case TypeKind::SIMDVector:
                return (elementType && elementType->isFloat() ? "float" : "int") + std::to_string(vectorWidth);
            case TypeKind::Tuple: {
                std::string s = "(";
                for (size_t i = 0; i < tupleElements.size(); ++i) {
                    if (i > 0) s += ", ";
                    s += tupleElements[i] ? tupleElements[i]->toString() : "?";
                }
                return s + ")";
            }
            case TypeKind::Array:
                return (isConst ? "const " : "") + elementType->toString() + "[" + std::to_string(arraySize) + "]";
            case TypeKind::ArrayView:
                return (isConst ? "const " : "") + elementType->toString() + "[]";
            case TypeKind::Function: {
                std::string s = "fn(";
                for (size_t i = 0; i < paramTypes.size(); ++i) {
                    if (i > 0) s += ", ";
                    s += paramTypes[i]->toString();
                }
                s += ") -> ";
                s += returnType ? returnType->toString() : "void";
                return s;
            }
            case TypeKind::Named:    return name;
        }
        return "<unknown>";
    }
};

// ── Built-in singleton types ────────────────────────────────────────────────

class TypeContext {
public:
    TypeContext();

    const Type* getVoid()  const noexcept { return &voidTy_; }
    const Type* getBool()  const noexcept { return &boolTy_; }
    const Type* getI8()    const noexcept { return &i8Ty_; }
    const Type* getI16()   const noexcept { return &i16Ty_; }
    const Type* getI32()   const noexcept { return &i32Ty_; }
    const Type* getI64()   const noexcept { return &i64Ty_; }
    const Type* getI128()  const noexcept { return &i128Ty_; }
    const Type* getIsize() const noexcept { return &isizeTy_; }
    const Type* getU8()    const noexcept { return &u8Ty_; }
    const Type* getU16()   const noexcept { return &u16Ty_; }
    const Type* getU32()   const noexcept { return &u32Ty_; }
    const Type* getU64()   const noexcept { return &u64Ty_; }
    const Type* getU128()  const noexcept { return &u128Ty_; }
    const Type* getUsize() const noexcept { return &usizeTy_; }
    const Type* getF32()   const noexcept { return &f32Ty_; }
    const Type* getF64()   const noexcept { return &f64Ty_; }

    const Type* getFloat4() const noexcept { return &float4Ty_; }
    const Type* getFloat8() const noexcept { return &float8Ty_; }
    const Type* getInt4()   const noexcept { return &int4Ty_; }
    const Type* getInt8()   const noexcept { return &int8Ty_; }
    const Type* getInt16()  const noexcept { return &int16Ty_; }
    const Type* getSIMDVector(const Type* elem, uint32_t width);

    const Type* getArray(const Type* elem, int64_t size, bool isConst = false);
    const Type* getArrayView(const Type* elem, bool isConst = false);
    const Type* getReference(const Type* elem, bool isConst = false, bool isPointer = false);
    const Type* getHandle(const Type* elem);
    const Type* getTask(const Type* elem);
    const Type* getSlab(const Type* elem, int64_t capacity = 0);
    const Type* getFunction(const Type* ret, std::vector<const Type*> params, std::vector<const ast::Expr*> defaultArgs = {});
    const Type* getTuple(std::vector<const Type*> elements);
    const Type* getStruct(std::string name, std::vector<StructFieldType> fields,
                          int layout = 0);
    const Type* getClass(std::string name, std::vector<StructFieldType> fields,
                         int layout = 0, std::string baseClass = "",
                         bool isClass = true, bool isInterface = false);
    const Type* findStruct(const std::string& name) const;
    const Type* findClass(const std::string& name) const;
    const Type* findRecord(const std::string& name) const;
    const Type* getEnum(std::string name, std::vector<EnumVariantType> variants, bool isNiche = false);
    const Type* findEnum(const std::string& name) const;
    std::vector<const Type*> getEnumTypes() const;
    std::vector<const Type*> getStructTypes() const;
    std::vector<const Type*> getRecordTypes() const;

    [[nodiscard]] const Type* getCommonArithmeticType(const Type* lhs, const Type* rhs) const noexcept;

private:
    Type voidTy_{TypeKind::Void};
    Type boolTy_{TypeKind::Bool};
    Type i8Ty_{TypeKind::I8};
    Type i16Ty_{TypeKind::I16};
    Type i32Ty_{TypeKind::I32};
    Type i64Ty_{TypeKind::I64};
    Type i128Ty_{TypeKind::I128};
    Type isizeTy_{TypeKind::Isize};
    Type u8Ty_{TypeKind::U8};
    Type u16Ty_{TypeKind::U16};
    Type u32Ty_{TypeKind::U32};
    Type u64Ty_{TypeKind::U64};
    Type u128Ty_{TypeKind::U128};
    Type usizeTy_{TypeKind::Usize};
    Type f32Ty_{TypeKind::F32};
    Type f64Ty_{TypeKind::F64};
    Type float4Ty_{TypeKind::SIMDVector};
    Type float8Ty_{TypeKind::SIMDVector};
    Type int4Ty_{TypeKind::SIMDVector};
    Type int8Ty_{TypeKind::SIMDVector};
    Type int16Ty_{TypeKind::SIMDVector};

    std::vector<std::unique_ptr<Type>> arena_;
};

} // namespace tamarin::sema

