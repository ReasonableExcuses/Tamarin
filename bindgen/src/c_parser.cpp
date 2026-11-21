// ============================================================================
// c_parser.cpp - Implementation of C header parsing via libclang
// ============================================================================

#include "bindgen/c_parser.h"
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <regex>

namespace tamarin::bindgen {

namespace {

std::string toStdString(CXString cx) {
    if (!cx.data) return "";
    const char* cstr = clang_getCString(cx);
    std::string s = cstr ? cstr : "";
    clang_disposeString(cx);
    return s;
}

std::string sanitizeIdentifier(const std::string& name) {
    static const std::unordered_set<std::string> keywords = {
        "void", "bool", "char", "byte", "short", "int", "long", "unsigned",
        "float", "double", "size_t", "ptrdiff_t", "u8", "u16", "u32", "u64", "u128",
        "usize", "i8", "i16", "i32", "i64", "i128", "isize", "const", "static",
        "extern", "auto", "if", "else", "while", "for", "do", "switch", "case",
        "default", "break", "continue", "return", "match", "struct", "class",
        "interface", "enum", "trait", "impl", "typedef", "sizeof", "type", "fn",
        "in", "let", "mut", "pub", "alignas", "import", "export", "operator", "select",
        "spawn", "await", "tag", "as", "region", "Handle", "Slab", "unsafe", "defer",
        "parallel", "reduce", "task_scope", "where", "cold", "span", "atomic", "chan",
        "reduction", "public", "private", "protected", "virtual", "override", "this",
        "null", "true", "false", "min", "max"
    };
    if (keywords.find(name) != keywords.end()) {
        return name + "_";
    }
    if (name.empty()) {
        return "_unused";
    }
    return name;
}

std::string normalizeTypeName(std::string name) {
    while (!name.empty() && isspace(static_cast<unsigned char>(name.front()))) name.erase(0, 1);
    while (!name.empty() && isspace(static_cast<unsigned char>(name.back()))) name.pop_back();

    bool changed = true;
    while (changed) {
        changed = false;
        if (name.starts_with("const ")) {
            name = name.substr(6);
            changed = true;
        } else if (name.starts_with("struct ")) {
            name = name.substr(7);
            changed = true;
        } else if (name.starts_with("union ")) {
            name = name.substr(6);
            changed = true;
        } else if (name.starts_with("enum ")) {
            name = name.substr(5);
            changed = true;
        } else if (name.starts_with("volatile ")) {
            name = name.substr(9);
            changed = true;
        }
        while (!name.empty() && isspace(static_cast<unsigned char>(name.front()))) name.erase(0, 1);
    }
    while (!name.empty() && isspace(static_cast<unsigned char>(name.back()))) name.pop_back();
    return name;
}

struct FieldVisitorContext {
    CParser* parser;
    CStruct* st;
};

struct EnumVisitorContext {
    CEnum* en;
};

} // namespace

CParser::CParser(ParserOptions options) : options_(std::move(options)) {
    index_ = clang_createIndex(0, 0);
}

CParser::~CParser() {
    if (tu_) {
        clang_disposeTranslationUnit(tu_);
        tu_ = nullptr;
    }
    if (index_) {
        clang_disposeIndex(index_);
        index_ = nullptr;
    }
}

bool CParser::isFromTargetHeader(CXCursor cursor) const {
    if (options_.includeSystemHeaders) return true;

    CXSourceLocation loc = clang_getCursorLocation(cursor);
    CXFile file;
    unsigned line, col, offset;
    clang_getSpellingLocation(loc, &file, &line, &col, &offset);

    if (!file) return false;
    std::string filePath = toStdString(clang_getFileName(file));
    if (filePath.empty()) return false;

    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path targetPath = fs::canonical(options_.headerPath, ec);
    if (ec) targetPath = fs::absolute(options_.headerPath);

    fs::path cursorPath = fs::canonical(filePath, ec);
    if (ec) cursorPath = fs::absolute(filePath);

    if (cursorPath == targetPath) return true;

    // Also accept headers located in user-specified include dirs
    for (const auto& inc : options_.includeDirs) {
        fs::path incPath = fs::canonical(inc, ec);
        if (ec) incPath = fs::absolute(inc);
        auto rel = fs::relative(cursorPath, incPath, ec);
        if (!ec && !rel.empty() && !rel.string().starts_with("..")) {
            return true;
        }
    }

    return false;
}

CType CParser::convertType(CXType type, CXCursor cursor) {
    CType res;
    res.rawSpelling = toStdString(clang_getTypeSpelling(type));

    // Const check
    if (clang_isConstQualifiedType(type)) {
        res.isConst = true;
    }

    switch (type.kind) {
        case CXType_Void:
            res.tamarinType = "void";
            return res;
        case CXType_Bool:
            res.tamarinType = "bool";
            return res;
        case CXType_Char_U:
        case CXType_UChar:
            res.tamarinType = "u8";
            return res;
        case CXType_Char_S:
        case CXType_SChar:
            res.tamarinType = "i8";
            return res;
        case CXType_Char16:
        case CXType_UShort:
            res.tamarinType = "u16";
            return res;
        case CXType_Short:
            res.tamarinType = "i16";
            return res;
        case CXType_Char32:
        case CXType_UInt:
            res.tamarinType = "u32";
            return res;
        case CXType_Int:
            res.tamarinType = "int";
            return res;
        case CXType_ULong: {
            long long sz = clang_Type_getSizeOf(type);
            res.tamarinType = (sz == 8) ? "u64" : "u32";
            return res;
        }
        case CXType_Long: {
            long long sz = clang_Type_getSizeOf(type);
            res.tamarinType = (sz == 8) ? "i64" : "int";
            return res;
        }
        case CXType_ULongLong:
            res.tamarinType = "u64";
            return res;
        case CXType_LongLong:
            res.tamarinType = "i64";
            return res;
        case CXType_Float:
            res.tamarinType = "float";
            return res;
        case CXType_Double:
            res.tamarinType = "double";
            return res;

        case CXType_Pointer: {
            res.isPointer = true;
            CXType pointee = clang_getPointeeType(type);
            bool isConstPointee = clang_isConstQualifiedType(pointee);
            if (pointee.kind == CXType_Void) {
                res.tamarinType = isConstPointee ? "*const void" : "*void";
                return res;
            }
            if (pointee.kind == CXType_Char_S || pointee.kind == CXType_Char_U) {
                res.tamarinType = isConstPointee ? "*const u8" : "*u8";
                return res;
            }
            if (pointee.kind == CXType_FunctionProto) {
                res.isFunctionPointer = true;
                CXType retType = clang_getResultType(pointee);
                std::string fnSig = "fn(";
                int numArgs = clang_getNumArgTypes(pointee);
                for (int i = 0; i < numArgs; ++i) {
                    if (i > 0) fnSig += ", ";
                    CXType argTy = clang_getArgType(pointee, i);
                    std::string aTy = convertType(argTy).tamarinType;
                    while (true) {
                        auto p1 = aTy.find("/*");
                        if (p1 == std::string::npos) break;
                        auto p2 = aTy.find("*/", p1);
                        if (p2 == std::string::npos) break;
                        aTy.erase(p1, (p2 + 2) - p1);
                    }
                    fnSig += aTy;
                }
                std::string rTy = convertType(retType).tamarinType;
                while (true) {
                    auto p1 = rTy.find("/*");
                    if (p1 == std::string::npos) break;
                    auto p2 = rTy.find("*/", p1);
                    if (p2 == std::string::npos) break;
                    rTy.erase(p1, (p2 + 2) - p1);
                }
                fnSig += ") -> " + rTy;
                res.tamarinType = "*void /* " + fnSig + " */";
                return res;
            }
            CType inner = convertType(pointee);
            if (isConstPointee) {
                res.tamarinType = "*const " + inner.tamarinType;
            } else {
                res.tamarinType = "*" + inner.tamarinType;
            }
            return res;
        }

        case CXType_ConstantArray: {
            long long sz = clang_getArraySize(type);
            res.arraySize = sz;
            CXType elemTy = clang_getArrayElementType(type);
            CType inner = convertType(elemTy);
            res.tamarinType = inner.tamarinType + "[" + std::to_string(sz) + "]";
            return res;
        }

        case CXType_IncompleteArray: {
            CXType elemTy = clang_getArrayElementType(type);
            CType inner = convertType(elemTy);
            res.tamarinType = "*" + inner.tamarinType;
            return res;
        }

        case CXType_Typedef: {
            std::string tName = normalizeTypeName(toStdString(clang_getTypeSpelling(type)));
            // Map common standard C types directly
            if (tName == "size_t" || tName == "__size_t" || tName == "uintptr_t" || tName == "__uintptr_t") {
                res.tamarinType = "usize";
                return res;
            }
            if (tName == "intptr_t" || tName == "__intptr_t" || tName == "ptrdiff_t" || tName == "__ptrdiff_t" || tName == "ssize_t") {
                res.tamarinType = "isize";
                return res;
            }
            if (tName == "uint8_t")  { res.tamarinType = "u8"; return res; }
            if (tName == "uint16_t") { res.tamarinType = "u16"; return res; }
            if (tName == "uint32_t") { res.tamarinType = "u32"; return res; }
            if (tName == "uint64_t") { res.tamarinType = "u64"; return res; }
            if (tName == "int8_t")   { res.tamarinType = "i8"; return res; }
            if (tName == "int16_t")  { res.tamarinType = "i16"; return res; }
            if (tName == "int32_t")  { res.tamarinType = "i32"; return res; }
            if (tName == "int64_t")  { res.tamarinType = "i64"; return res; }
            if (tName == "bool")     { res.tamarinType = "bool"; return res; }

            auto it = typedefMap_.find(tName);
            if (it != typedefMap_.end()) {
                res.tamarinType = it->second;
                return res;
            }

            CXType canon = clang_getCanonicalType(type);
            if (canon.kind == CXType_Pointer && clang_getPointeeType(canon).kind == CXType_FunctionProto) {
                return convertType(canon);
            }
            if (canon.kind != CXType_Record &&
                canon.kind != CXType_Invalid && canon.kind != CXType_Unexposed) {
                return convertType(canon);
            }
            if (canon.kind == CXType_Record || canon.kind == CXType_Elaborated) {
                std::string sName = normalizeTypeName(toStdString(clang_getTypeSpelling(canon)));
                if (!sName.empty() && sName.find("unnamed") == std::string::npos) {
                    if (visitedStructs_.find(sName) == visitedStructs_.end() && visitedStructs_.find(tName) == visitedStructs_.end()) {
                        opaqueStructs_.insert(tName);
                    }
                    res.tamarinType = tName;
                    return res;
                }
            }
            res.tamarinType = tName;
            return res;
        }

        case CXType_Enum: {
            std::string sName = normalizeTypeName(toStdString(clang_getTypeSpelling(type)));
            if (!sName.empty() && sName.find("unnamed") == std::string::npos &&
                visitedEnums_.find(sName) != visitedEnums_.end()) {
                res.tamarinType = sName;
                return res;
            }
            long long sz = clang_Type_getSizeOf(type);
            if (sz == 8) res.tamarinType = "i64";
            else if (sz == 2) res.tamarinType = "i16";
            else if (sz == 1) res.tamarinType = "u8";
            else res.tamarinType = "int";
            return res;
        }

        case CXType_Elaborated: {
            CXType named = clang_Type_getNamedType(type);
            if (named.kind != CXType_Invalid && named.kind != CXType_Unexposed && named.kind != CXType_Elaborated) {
                return convertType(named, cursor);
            }
            CXType canon = clang_getCanonicalType(type);
            if (canon.kind != CXType_Invalid && canon.kind != CXType_Unexposed && canon.kind != CXType_Elaborated) {
                return convertType(canon, cursor);
            }
            std::string sName = normalizeTypeName(toStdString(clang_getTypeSpelling(type)));
            res.tamarinType = sName;
            return res;
        }

        case CXType_Record: {
            std::string sName = normalizeTypeName(toStdString(clang_getTypeSpelling(type)));
            if (sName.empty() || sName.find("unnamed") != std::string::npos) {
                long long sz = clang_Type_getSizeOf(type);
                if (sz <= 0) sz = 8;
                res.tamarinType = "u8[" + std::to_string(sz) + "]";
                return res;
            }
            auto it = typedefMap_.find(sName);
            if (it != typedefMap_.end()) {
                sName = it->second;
            }
            if (visitedStructs_.find(sName) == visitedStructs_.end()) {
                opaqueStructs_.insert(sName);
            }
            res.tamarinType = sName;
            return res;
        }

        default: {
            std::string sp = normalizeTypeName(toStdString(clang_getTypeSpelling(type)));
            if (sp == "size_t" || sp == "__size_t" || sp == "uintptr_t" || sp == "__uintptr_t") {
                res.tamarinType = "usize";
                return res;
            }
            if (sp == "intptr_t" || sp == "__intptr_t" || sp == "ptrdiff_t" || sp == "__ptrdiff_t" || sp == "ssize_t") {
                res.tamarinType = "isize";
                return res;
            }
            auto it = typedefMap_.find(sp);
            if (it != typedefMap_.end()) {
                res.tamarinType = it->second;
                return res;
            }
            CXType canon = clang_getCanonicalType(type);
            if (canon.kind != type.kind && canon.kind != CXType_Invalid && canon.kind != CXType_Unexposed) {
                return convertType(canon, cursor);
            }
            res.tamarinType = sp;
            return res;
        }
    }
}

struct VisitorContext {
    CParser* parser;
    CHeaderModel* model;
};

CXChildVisitResult CParser::visitTopLevel(CXCursor cursor, CXCursor /*parent*/, CXClientData clientData) {
    auto* ctx = static_cast<VisitorContext*>(clientData);
    if (!ctx->parser->isFromTargetHeader(cursor)) {
        return CXChildVisit_Continue;
    }

    CXCursorKind kind = clang_getCursorKind(cursor);
    switch (kind) {
        case CXCursor_StructDecl:
            ctx->parser->processStruct(cursor, *ctx->model);
            break;
        case CXCursor_UnionDecl:
            ctx->parser->processUnion(cursor, *ctx->model);
            break;
        case CXCursor_EnumDecl:
            ctx->parser->processEnum(cursor, *ctx->model);
            break;
        case CXCursor_FunctionDecl:
            ctx->parser->processFunction(cursor, *ctx->model);
            break;
        case CXCursor_TypedefDecl:
            ctx->parser->processTypedef(cursor, *ctx->model);
            break;
        case CXCursor_MacroDefinition:
            ctx->parser->processMacro(cursor, *ctx->model);
            break;
        default:
            break;
    }
    return CXChildVisit_Continue;
}

void CParser::processStruct(CXCursor cursor, CHeaderModel& model) {
    if (!clang_isCursorDefinition(cursor)) return;

    std::string name = toStdString(clang_getCursorSpelling(cursor));
    if (name.empty()) {
        CXType ty = clang_getCursorType(cursor);
        name = normalizeTypeName(toStdString(clang_getTypeSpelling(ty)));
    }
    name = normalizeTypeName(name);
    if (name.empty()) return;

    if (!options_.prefixFilter.empty() && !name.starts_with(options_.prefixFilter)) {
        return;
    }
    if (visitedStructs_.find(name) != visitedStructs_.end()) return;
    visitedStructs_.insert(name);

    CStruct st;
    st.name = name;
    st.isUnion = false;
    CXType curType = clang_getCursorType(cursor);
    st.sizeInBytes = clang_Type_getSizeOf(curType);
    st.alignInBytes = clang_Type_getAlignOf(curType);

    FieldVisitorContext fCtx{this, &st};

    clang_visitChildren(cursor, visitStructFields, &fCtx);
    model.structs.push_back(std::move(st));
}

CXChildVisitResult CParser::visitStructFields(CXCursor cursor, CXCursor /*parent*/, CXClientData clientData) {
    auto* ctx = static_cast<FieldVisitorContext*>(clientData);
    if (clang_getCursorKind(cursor) == CXCursor_FieldDecl) {
        if (clang_Cursor_isBitField(cursor)) {
            int bw = clang_getFieldDeclBitWidth(cursor);
            if (bw == 0) {
                return CXChildVisit_Continue; // Unnamed zero-width bitfield
            }
        }
        CField field;
        field.name = sanitizeIdentifier(toStdString(clang_getCursorSpelling(cursor)));
        if (field.name.empty() || field.name == "_unused") {
            field.name = "_anon" + std::to_string(ctx->st->fields.size());
        }
        CXType fType = clang_getCursorType(cursor);
        field.type = ctx->parser->convertType(fType, cursor);
        if (clang_Cursor_isBitField(cursor)) {
            field.bitWidth = clang_getFieldDeclBitWidth(cursor);
        }
        field.offsetInBits = clang_Cursor_getOffsetOfField(cursor);
        ctx->st->fields.push_back(std::move(field));
    }
    return CXChildVisit_Continue;
}

void CParser::processUnion(CXCursor cursor, CHeaderModel& model) {
    if (!clang_isCursorDefinition(cursor)) return;

    std::string name = normalizeTypeName(toStdString(clang_getCursorSpelling(cursor)));
    if (name.empty()) return;

    if (!options_.prefixFilter.empty() && !name.starts_with(options_.prefixFilter)) {
        return;
    }
    if (visitedStructs_.find(name) != visitedStructs_.end()) return;
    visitedStructs_.insert(name);

    CStruct un;
    un.name = name;
    un.isUnion = true;
    CXType curType = clang_getCursorType(cursor);
    un.sizeInBytes = clang_Type_getSizeOf(curType);
    un.alignInBytes = clang_Type_getAlignOf(curType);

    FieldVisitorContext fCtx{this, &un};

    clang_visitChildren(cursor, visitStructFields, &fCtx);
    model.structs.push_back(std::move(un));
}

void CParser::processEnum(CXCursor cursor, CHeaderModel& model) {
    if (!clang_isCursorDefinition(cursor)) return;

    std::string name = normalizeTypeName(toStdString(clang_getCursorSpelling(cursor)));

    CEnum en;
    en.name = name;

    EnumVisitorContext eCtx{&en};

    clang_visitChildren(cursor, visitEnumConstants, &eCtx);

    if (name.empty()) {
        // Anonymous enum: emit individual constants
        for (const auto& c : en.constants) {
            CConstant constant;
            constant.name = c.name;
            constant.value = std::to_string(c.value);
            constant.type = "int";
            model.constants.push_back(std::move(constant));
        }
        return;
    }

    if (!options_.prefixFilter.empty() && !name.starts_with(options_.prefixFilter)) {
        return;
    }
    if (visitedEnums_.find(name) != visitedEnums_.end()) return;
    visitedEnums_.insert(name);

    model.enums.push_back(std::move(en));
}

CXChildVisitResult CParser::visitEnumConstants(CXCursor cursor, CXCursor /*parent*/, CXClientData clientData) {
    auto* ctx = static_cast<EnumVisitorContext*>(clientData);
    if (clang_getCursorKind(cursor) == CXCursor_EnumConstantDecl) {
        CEnumConstant ec;
        ec.name = toStdString(clang_getCursorSpelling(cursor));
        ec.value = clang_getEnumConstantDeclValue(cursor);
        ctx->en->constants.push_back(std::move(ec));
    }
    return CXChildVisit_Continue;
}

void CParser::processFunction(CXCursor cursor, CHeaderModel& model) {
    std::string name = toStdString(clang_getCursorSpelling(cursor));
    if (name.empty()) return;

    if (!options_.prefixFilter.empty() && !name.starts_with(options_.prefixFilter)) {
        return;
    }
    if (visitedFunctions_.find(name) != visitedFunctions_.end()) return;
    visitedFunctions_.insert(name);

    CFunction fn;
    fn.name = name;
    CXType retTy = clang_getCursorResultType(cursor);
    fn.returnType = convertType(retTy, cursor);
    fn.isVariadic = clang_Cursor_isVariadic(cursor);

    int numArgs = clang_Cursor_getNumArguments(cursor);
    for (int i = 0; i < numArgs; ++i) {
        CXCursor argCursor = clang_Cursor_getArgument(cursor, i);
        CParam param;
        param.name = sanitizeIdentifier(toStdString(clang_getCursorSpelling(argCursor)));
        if (param.name.empty() || param.name == "_unused") {
            param.name = "arg" + std::to_string(i);
        }
        CXType argTy = clang_getCursorType(argCursor);
        param.type = convertType(argTy, argCursor);
        fn.params.push_back(std::move(param));
    }

    model.functions.push_back(std::move(fn));
}

void CParser::processTypedef(CXCursor cursor, CHeaderModel& model) {
    std::string name = toStdString(clang_getCursorSpelling(cursor));
    if (name.empty()) return;

    if (!options_.prefixFilter.empty() && !name.starts_with(options_.prefixFilter)) {
        return;
    }
    if (visitedTypedefs_.find(name) != visitedTypedefs_.end()) return;
    visitedTypedefs_.insert(name);

    CXType uType = clang_getTypedefDeclUnderlyingType(cursor);
    CType target = convertType(uType, cursor);

    // If typedef names a struct that had no name or tag name:
    CXCursor declCursor = clang_getTypeDeclaration(uType);
    CXCursorKind declKind = clang_getCursorKind(declCursor);
    if (declKind == CXCursor_StructDecl || declKind == CXCursor_UnionDecl || declKind == CXCursor_ClassDecl) {
        std::string structSpelling = normalizeTypeName(toStdString(clang_getCursorSpelling(declCursor)));
        if (structSpelling.empty()) {
            // Give the anonymous struct this typedef name
            processStruct(declCursor, model);
            if (!model.structs.empty() && model.structs.back().name.empty()) {
                model.structs.back().name = name;
                visitedStructs_.insert(name);
            }
            typedefMap_[name] = name;
            return;
        } else if (structSpelling != name) {
            bool found = false;
            for (auto& st : model.structs) {
                if (st.name == structSpelling) {
                    if (structSpelling.starts_with("tag") || structSpelling.starts_with("_") ||
                        structSpelling.ends_with("_t") || structSpelling.ends_with("_s")) {
                        st.name = name;
                        visitedStructs_.insert(name);
                        typedefMap_[structSpelling] = name;
                        typedefMap_[name] = name;
                    }
                    found = true;
                    break;
                }
            }
            typedefMap_[structSpelling] = name;
            typedefMap_[name] = name;
            return;
        }
    }

    if (declKind == CXCursor_EnumDecl) {
        std::string enumSpelling = toStdString(clang_getCursorSpelling(declCursor));
        if (enumSpelling.empty()) {
            CEnum en;
            en.name = name;
            EnumVisitorContext eCtx{&en};
            clang_visitChildren(declCursor, visitEnumConstants, &eCtx);
            model.enums.push_back(std::move(en));
            return;
        }
    }

    std::string resolvedTarget = target.tamarinType;
    while (typedefMap_.find(resolvedTarget) != typedefMap_.end() && typedefMap_[resolvedTarget] != resolvedTarget) {
        resolvedTarget = typedefMap_[resolvedTarget];
    }
    typedefMap_[name] = resolvedTarget;

    CTypedef td;
    td.name = name;
    td.targetType = target;
    model.typedefs.push_back(std::move(td));
}

static bool isValidMacroValue(const std::string& val) {
    if (val.empty()) return false;
    static const std::vector<std::string> blacklist = {
        "__attribute__", "__declspec", "__has_", "__asm__",
        "WINAPI", "APIENTRY", "WINGDIAPI", "restrict",
        "__cdecl", "__stdcall", "__fastcall", "__forceinline", "__inline",
        "_Printf_", "_Scanf_", "_In_", "_Out_", "_Success_", "_Null_",
        "memcpy", "memmove", "memset", "malloc", "free", "alloca"
    };
    for (const auto& b : blacklist) {
        if (val.find(b) != std::string::npos) return false;
    }

    if (val.starts_with("\"") && val.ends_with("\"")) {
        for (size_t i = 1; i + 1 < val.size(); ++i) {
            if (val[i] == '"' && val[i - 1] != '\\') {
                return false;
            }
        }
        return true;
    }

    if (!val.starts_with("\"") && !val.starts_with("'") && !val.starts_with("(") && !val.starts_with("~")) {
        char first = val.front();
        if (!isdigit(static_cast<unsigned char>(first)) && first != '+' && first != '-') {
            if (!val.starts_with("SDL_") && val != "true" && val != "false") {
                return false;
            }
        }
    }

    return true;
}

static std::string sanitizeMacroValue(std::string val) {
    if (val.empty()) return "";
    if (val.find("__attribute__") != std::string::npos ||
        val.find("__declspec") != std::string::npos ||
        val.find("__has_") != std::string::npos ||
        val.find("__asm__") != std::string::npos ||
        val.find("WINAPI_FAMILY") != std::string::npos) {
        return "";
    }

    // Strip SDL_SINT64_C(x) / SDL_UINT64_C(x) / INT64_C(x)
    static const std::regex cMacroRegex(R"([A-Za-z0-9_]+_C\s*\(\s*([^)]+)\s*\))");
    val = std::regex_replace(val, cMacroRegex, "$1");

    // Strip C-style cast patterns: ((Type)val) or (Type)val
    static const std::regex doubleParenCast(R"(\(\s*\(\s*[A-Za-z0-9_]+\s*\*?\s*\)\s*([^)]+)\s*\))");
    val = std::regex_replace(val, doubleParenCast, "$1");
    static const std::regex singleParenCast(R"(\(\s*[A-Za-z0-9_]+\s*\*?\s*\)\s*([A-Za-z0-9_~]+))");
    val = std::regex_replace(val, singleParenCast, "$1");
    val = std::regex_replace(val, doubleParenCast, "$1");

    // Strip C integer suffixes (u, U, l, L, ll, LL, ul, UL, ull, ULL)
    static const std::regex hexSuffixRegex(R"((0x[0-9a-fA-F]+)[uUlL]+)");
    val = std::regex_replace(val, hexSuffixRegex, "$1");
    static const std::regex decSuffixRegex(R"((\b[0-9]+)[uUlL]+)");
    val = std::regex_replace(val, decSuffixRegex, "$1");

    // Trim whitespace
    while (!val.empty() && isspace(static_cast<unsigned char>(val.front()))) val.erase(val.begin());
    while (!val.empty() && isspace(static_cast<unsigned char>(val.back()))) val.pop_back();

    if (!isValidMacroValue(val)) return "";

    return val;
}

void CParser::processMacro(CXCursor cursor, CHeaderModel& model) {
    if (clang_Cursor_isMacroFunctionLike(cursor)) return;

    std::string name = toStdString(clang_getCursorSpelling(cursor));
    if (name.empty() || name.starts_with("__") || name.starts_with("_")) return;

    if (!options_.prefixFilter.empty() && !name.starts_with(options_.prefixFilter)) {
        return;
    }

    CXSourceRange range = clang_getCursorExtent(cursor);
    CXToken* tokens = nullptr;
    unsigned numTokens = 0;
    clang_tokenize(tu_, range, &tokens, &numTokens);

    if (numTokens >= 2) {
        std::string valStr;
        for (unsigned i = 1; i < numTokens; ++i) {
            std::string t = toStdString(clang_getTokenSpelling(tu_, tokens[i]));
            if (i > 1 && !valStr.empty() && !t.empty()) {
                char last = valStr.back();
                char first = t.front();
                if (isalnum(static_cast<unsigned char>(last)) && isalnum(static_cast<unsigned char>(first))) {
                    valStr += " ";
                }
            }
            valStr += t;
        }

        valStr = sanitizeMacroValue(valStr);

        if (!valStr.empty() && valStr.find('{') == std::string::npos) {
            CConstant c;
            c.name = name;
            c.value = valStr;
            if (valStr.starts_with("\"")) {
                c.type = "string";
            } else if (valStr.find('.') != std::string::npos ||
                       valStr.find("e-") != std::string::npos || valStr.find("e+") != std::string::npos ||
                       valStr.find("E-") != std::string::npos || valStr.find("E+") != std::string::npos ||
                       valStr.ends_with("f") || valStr.ends_with("F")) {
                c.type = "float";
            } else if (valStr.starts_with("'")) {
                c.type = "char";
            } else if ((valStr.starts_with("0x") || valStr.starts_with("0X")) && valStr.size() > 10) {
                c.type = "i64";
            } else if (valStr.size() > 10 && isdigit(static_cast<unsigned char>(valStr[0]))) {
                c.type = "i64";
            } else {
                c.type = "int";
            }
            model.constants.push_back(std::move(c));
        }
    }

    if (tokens) {
        clang_disposeTokens(tu_, tokens, numTokens);
    }
}

bool CParser::parse(CHeaderModel& outModel, std::string& outError) {
    outModel.headerPath = options_.headerPath;

    std::vector<const char*> args;
    args.push_back("-x");
    args.push_back("c");
    args.push_back("-std=c11");
    args.push_back("-D__TAMARIN_BINDGEN__=1");

    std::vector<std::string> incFlags;
    for (const auto& inc : options_.includeDirs) {
        incFlags.push_back("-I" + inc);
    }
    for (const auto& flag : incFlags) {
        args.push_back(flag.c_str());
    }

    std::vector<std::string> defFlags;
    for (const auto& def : options_.defines) {
        defFlags.push_back("-D" + def);
    }
    for (const auto& flag : defFlags) {
        args.push_back(flag.c_str());
    }

    for (const auto& extra : options_.extraClangArgs) {
        args.push_back(extra.c_str());
    }

    unsigned flags = CXTranslationUnit_DetailedPreprocessingRecord |
                     CXTranslationUnit_SkipFunctionBodies;

    tu_ = clang_parseTranslationUnit(
        index_,
        options_.headerPath.c_str(),
        args.data(),
        static_cast<int>(args.size()),
        nullptr,
        0,
        flags
    );

    if (!tu_) {
        outError = "Failed to parse translation unit with libclang: " + options_.headerPath;
        return false;
    }

    // Check for diagnostic errors
    unsigned numDiag = clang_getNumDiagnostics(tu_);
    for (unsigned i = 0; i < numDiag; ++i) {
        CXDiagnostic diag = clang_getDiagnostic(tu_, i);
        CXDiagnosticSeverity sev = clang_getDiagnosticSeverity(diag);
        if (sev >= CXDiagnostic_Error) {
            std::string diagMsg = toStdString(clang_formatDiagnostic(diag, clang_defaultDiagnosticDisplayOptions()));
            // If the error is fatal, record it
            if (sev >= CXDiagnostic_Fatal) {
                outError += diagMsg + "\n";
            }
        }
        clang_disposeDiagnostic(diag);
    }

    CXCursor root = clang_getTranslationUnitCursor(tu_);
    VisitorContext ctx{this, &outModel};
    clang_visitChildren(root, visitTopLevel, &ctx);

    // Synthesize opaque struct definitions for any undefined records referenced in the header
    for (const auto& opName : opaqueStructs_) {
        if (visitedStructs_.find(opName) == visitedStructs_.end()) {
            CStruct st;
            st.name = opName;
            st.sizeInBytes = 1;
            outModel.structs.push_back(std::move(st));
            visitedStructs_.insert(opName);
        }
    }

    return true;
}

} // namespace tamarin::bindgen
