// ============================================================================
// llvm_codegen.cpp - Lowers Tamarin IR to LLVM IR and produces native executables
// ============================================================================

#include "tamarin/backend/llvm_codegen.h"
#include "tamarin/config.h"
#include "tamarin/target/process_util.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <sstream>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <unistd.h>
#else
#include <unistd.h>
#endif

namespace tamarin::backend {

static std::filesystem::path getCurrentExecutableDir() {
#ifdef _WIN32
    wchar_t exePathBuf[MAX_PATH];
    if (GetModuleFileNameW(nullptr, exePathBuf, MAX_PATH)) {
        return std::filesystem::path(exePathBuf).parent_path();
    }
#elif defined(__APPLE__)
    char pathBuf[1024];
    uint32_t size = sizeof(pathBuf);
    if (_NSGetExecutablePath(pathBuf, &size) == 0) {
        std::error_code ec;
        return std::filesystem::canonical(pathBuf, ec).parent_path();
    }
#else
    char pathBuf[1024];
    ssize_t len = readlink("/proc/self/exe", pathBuf, sizeof(pathBuf) - 1);
    if (len > 0) {
        pathBuf[len] = '\0';
        std::error_code ec;
        return std::filesystem::canonical(pathBuf, ec).parent_path();
    }
#endif
    return std::filesystem::current_path();
}

static int runSystemCommand(const std::string& cmd) {
    return tamarin::target::executeCommand(cmd);
}

static std::string findRuntimeLibrary(bool isStatic = false) {
    if (isStatic) {
        if (const char* env = std::getenv("TAMARIN_RUNTIME_STATIC_LIB")) {
            if (std::filesystem::exists(env)) return env;
        }
    } else {
        if (const char* env = std::getenv("TAMARIN_RUNTIME_LIB")) {
            if (std::filesystem::exists(env)) return env;
        }
    }

    std::vector<std::string> libNames;
    if (isStatic) {
        libNames = {
            "tamarin_runtime_static.lib",
            "libtamarin_runtime_static.a",
            "tamarin_runtime_static.a",
            "libtamarin_runtime.a",
            "tamarin_runtime.lib"
        };
    } else {
        libNames = {
            "tamarin_runtime.lib",
            "libtamarin_runtime.a",
            "tamarin_runtime.a",
            "tamarin_runtime_static.lib",
            "libtamarin_runtime_static.a"
        };
    }

    auto exeDir = getCurrentExecutableDir();
    std::vector<std::filesystem::path> searchDirs = {
        exeDir,
        exeDir.parent_path(),
        exeDir.parent_path() / "Release",
        exeDir.parent_path() / "Debug",
        exeDir.parent_path().parent_path() / "build",
        exeDir.parent_path().parent_path() / "build" / "Release",
        exeDir.parent_path().parent_path() / "build" / "Debug",
        std::filesystem::current_path(),
        std::filesystem::current_path() / "build",
        std::filesystem::current_path() / "build" / "Release",
        std::filesystem::current_path() / "build" / "Debug",
        std::filesystem::current_path() / "Release",
        std::filesystem::current_path() / "Debug"
    };

    for (const auto& dir : searchDirs) {
        for (const auto& name : libNames) {
            auto cand = dir / name;
            std::error_code ec;
            if (std::filesystem::exists(cand, ec) && !std::filesystem::is_directory(cand, ec)) {
                return std::filesystem::absolute(cand).string();
            }
        }
    }

    if (isStatic) {
        return findRuntimeLibrary(false);
    }
    return "";
}

LLVMCodeGen::LLVMCodeGen(const ir::IRModule& module, DiagnosticEngine& diag, std::string clangPath)
    : module_(module), diag_(diag), clangPath_(std::move(clangPath)) {
    if (clangPath_.empty()) {
        const char* env = std::getenv("TAMARIN_CLANG");
        if (env && *env) {
            clangPath_ = env;
        } else {
#ifdef TAMARIN_DEFAULT_CLANG_PATH
            clangPath_ = TAMARIN_DEFAULT_CLANG_PATH;
#else
            clangPath_ = "clang";
#endif
        }
    }
    const char* fm = std::getenv("TAMARIN_FAST_MATH");
    if (fm && *fm && std::string(fm) != "0") {
        fastMath_ = true;
    }
}

void LLVMCodeGen::setClangPath(std::string path) {
    if (!path.empty()) {
        clangPath_ = std::move(path);
    }
}

const std::string& LLVMCodeGen::getClangPath() const {
    return clangPath_;
}

const sema::Type* LLVMCodeGen::getValueType(ir::ValueId id) const {
    auto it = valueTypes_.find(id);
    return it != valueTypes_.end() ? it->second : nullptr;
}

std::string LLVMCodeGen::formatOperand(ir::ValueId id, const sema::Type* ty) const {
    if (id == ir::InvalidValue) {
        return "undef";
    }
    auto it = intConstants_.find(id);
    if (it != intConstants_.end()) {
        if (ty && (ty->isReference() || isPointerValue_.count(id)) && it->second == 0) {
            return "null";
        }
        return std::to_string(it->second);
    }
    auto fIt = floatConstants_.find(id);
    if (fIt != floatConstants_.end()) {
        double d = fIt->second;
        if (d == 0.0) {
            return "0.0";
        }
        auto vTy = getValueType(id);
        const sema::Type* actualTy = ty ? ty : vTy;
        if (actualTy && actualTy->isFloat() && actualTy->kind == sema::TypeKind::F32) {
            d = static_cast<double>(static_cast<float>(d));
        }
        uint64_t bits = 0;
        std::memcpy(&bits, &d, sizeof(bits));
        char buf[32];
        std::snprintf(buf, sizeof(buf), "0x%016llX", static_cast<unsigned long long>(bits));
        return std::string(buf);
    }
    auto bIt = boolConstants_.find(id);
    if (bIt != boolConstants_.end()) {
        return bIt->second ? "1" : "0";
    }
    return "%v" + std::to_string(id);
}

std::string LLVMCodeGen::sanitizeEnumName(const std::string& name) const {
    std::string s = name;
    for (char& c : s) {
        if (c == '<' || c == '>' || c == ',' || c == ' ' || c == ':' || c == '&' || c == '*' || c == '[' || c == ']') {
            c = '_';
        }
    }
    return s;
}

std::string LLVMCodeGen::zeroValue(const sema::Type* ty) const {
    if (!ty) return "0";
    switch (ty->kind) {
        case sema::TypeKind::Bool:      return "0";
        case sema::TypeKind::F32:
        case sema::TypeKind::F64:       return "0.0";
        case sema::TypeKind::Array:
        case sema::TypeKind::ArrayView:
        case sema::TypeKind::Struct:
        case sema::TypeKind::Class:
        case sema::TypeKind::Interface:
        case sema::TypeKind::SIMDVector:
        case sema::TypeKind::Tuple:
        case sema::TypeKind::Slab:      return "zeroinitializer";
        case sema::TypeKind::Function:  return "zeroinitializer";
        case sema::TypeKind::Enum:
            return ty->isNicheOptimized ? "0" : "zeroinitializer";
        case sema::TypeKind::Reference:
        case sema::TypeKind::Task:      return "null";
        default:                        return "0";
    }
}

std::string LLVMCodeGen::llvmType(const sema::Type* ty) const {
    if (!ty) return "i32";

    switch (ty->kind) {
        case sema::TypeKind::Void:     return "void";
        case sema::TypeKind::Bool:     return "i1";
        case sema::TypeKind::I8:
        case sema::TypeKind::U8:       return "i8";
        case sema::TypeKind::I16:
        case sema::TypeKind::U16:      return "i16";
        case sema::TypeKind::I32:
        case sema::TypeKind::U32:      return "i32";
        case sema::TypeKind::I64:
        case sema::TypeKind::U64:
        case sema::TypeKind::Usize:
        case sema::TypeKind::Isize:    return "i64";
        case sema::TypeKind::F32:      return "float";
        case sema::TypeKind::F64:      return "double";
        case sema::TypeKind::Struct:
        case sema::TypeKind::Class:
        case sema::TypeKind::Interface:
            return "%struct." + ty->structName;
        case sema::TypeKind::Enum:
            if (ty->isNicheOptimized) return "i64";
            return "%enum." + sanitizeEnumName(ty->enumName);
        case sema::TypeKind::Reference:
            return "ptr";
        case sema::TypeKind::Function:
            return "{ ptr, ptr }";
        case sema::TypeKind::Array:
            return "[" + std::to_string(ty->arraySize >= 0 ? ty->arraySize : 0) +
                   " x " + llvmType(ty->elementType) + "]";
        case sema::TypeKind::ArrayView:
            // Bounded array view: { ptr, i64 }
            return "{ ptr, i64 }";
        case sema::TypeKind::Tuple: {
            std::string res = "{ ";
            for (size_t i = 0; i < ty->tupleElements.size(); ++i) {
                if (i > 0) res += ", ";
                res += llvmType(ty->tupleElements[i]);
            }
            res += " }";
            return res;
        }
        case sema::TypeKind::Handle:
            return "i64";
        case sema::TypeKind::Task:
            return "ptr";
        case sema::TypeKind::Slab:
            return "%__tamarin_slab";
        case sema::TypeKind::SIMDVector: {
            uint32_t w = ty->vectorWidth > 0 ? ty->vectorWidth : 4;
            std::string elem = (ty->elementType && ty->elementType->isFloat()) ? "float" : "i32";
            return "<" + std::to_string(w) + " x " + elem + ">";
        }
        default:
            return "i32";
    }
}

static std::string escapeLLVMString(const std::string& str) {
    std::ostringstream ss;
    for (unsigned char c : str) {
        if (c == '\\') {
            ss << "\\5C";
        } else if (c == '"') {
            ss << "\\22";
        } else if (c == '\n') {
            ss << "\\0A";
        } else if (c == '\r') {
            ss << "\\0D";
        } else if (c == '\t') {
            ss << "\\09";
        } else if (c >= 32 && c <= 126) {
            ss << static_cast<char>(c);
        } else {
            ss << '\\' << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << static_cast<int>(c) << std::dec;
        }
    }
    ss << "\\00";
    return ss.str();
}

void LLVMCodeGen::emitHeader(std::ostream& os) {
    os << "; ModuleID = '" << module_.name << "'\n";
    os << "source_filename = \"" << module_.name << ".tmn\"\n";
    os << "target datalayout = \"" << targetTriple_.llvmDataLayout() << "\"\n";
    os << "target triple = \"" << targetTriple_.str() << "\"\n\n";

    os << "%__tamarin_region_chunk = type { ptr, i64, i64, i64 }\n";
    os << "%__tamarin_dtor_record = type { ptr, ptr, ptr, i64, i64 }\n";
    os << "%__tamarin_sidecar_chunk = type { ptr, i64, i64 }\n";
    os << "%__tamarin_region = type { ptr, ptr, i64, i64, i64, i64, ptr, ptr, ptr, i64, i64, i64, i64, i64, i64 }\n\n";
    os << "%__tamarin_slab_chunk = type { ptr, ptr, i32, i32, i32, i32, ptr, ptr, i64, i64 }\n";
    os << "%__tamarin_slab = type { ptr, ptr, i64, i64, i64, i64, i64, ptr, ptr, i64, i64 }\n\n";

    std::unordered_set<std::string> emitted;
    std::function<void(const sema::Type*)> emitStruct = [&](const sema::Type* ty) {
        if (!ty || !ty->isRecord() || emitted.count(ty->structName)) return;
        emitted.insert(ty->structName);

        for (const auto& f : ty->structFields) {
            if (f.type && f.type->isRecord()) {
                emitStruct(f.type);
            }
        }

        os << "%struct." << ty->structName << " = type { ";
        for (size_t i = 0; i < ty->structFields.size(); ++i) {
            if (i > 0) os << ", ";
            os << llvmType(ty->structFields[i].type);
        }
        os << " }\n";
    };

    // First emit all record types in module_.recordTypes
    for (const auto* recTy : module_.recordTypes) {
        emitStruct(recTy);
    }

    for (const auto& fn : module_.functions) {
        if (fn->returnType) emitStruct(fn->returnType);
        for (const auto& p : fn->params) {
            if (p.type) emitStruct(p.type);
        }
        for (const auto& b : fn->blocks) {
            for (const auto& inst : b->instructions) {
                if (inst.type) emitStruct(inst.type);
            }
        }
    }
    if (!emitted.empty()) {
        os << "\n";
    }

    // Emit enum definitions
    std::unordered_set<std::string> emittedEnums;
    auto emitEnumDef = [&](const sema::Type* ty) {
        if (!ty || !ty->isEnum() || ty->isNicheOptimized) return;
        std::string eName = sanitizeEnumName(ty->enumName);
        if (emittedEnums.count(eName)) return;
        emittedEnums.insert(eName);
        if (ty->size() <= 4) {
            os << "%enum." << eName << " = type { i32 }\n";
        } else {
            size_t align = std::max((size_t)4, ty->maxPayloadAlignment);
            if (align >= 8) {
                size_t rem = ty->size() > 8 ? (ty->size() - 8) : 0;
                if (rem == 0) {
                    os << "%enum." << eName << " = type { i32, i32 }\n";
                } else if (rem % 8 == 0) {
                    size_t n64 = rem / 8;
                    if (n64 == 1) {
                        os << "%enum." << eName << " = type { i32, i32, i64 }\n";
                    } else {
                        os << "%enum." << eName << " = type { i32, i32, [" << n64 << " x i64] }\n";
                    }
                } else if (rem % 4 == 0) {
                    size_t n32 = rem / 4;
                    if (n32 == 1) {
                        os << "%enum." << eName << " = type { i32, i32, i32 }\n";
                    } else {
                        os << "%enum." << eName << " = type { i32, i32, [" << n32 << " x i32] }\n";
                    }
                } else {
                    os << "%enum." << eName << " = type { i32, i32, [" << rem << " x i8] }\n";
                }
            } else {
                size_t rem = ty->size() > 4 ? (ty->size() - 4) : 0;
                if (rem == 0) {
                    os << "%enum." << eName << " = type { i32 }\n";
                } else if (rem % 4 == 0) {
                    size_t n32 = rem / 4;
                    if (n32 == 1) {
                        os << "%enum." << eName << " = type { i32, i32 }\n";
                    } else {
                        os << "%enum." << eName << " = type { i32, [" << n32 << " x i32] }\n";
                    }
                } else {
                    os << "%enum." << eName << " = type { i32, [" << rem << " x i8] }\n";
                }
            }
        }
    };

    for (const auto* enumTy : module_.enumTypes) {
        emitEnumDef(enumTy);
    }
    for (const auto& fn : module_.functions) {
        if (fn->returnType) emitEnumDef(fn->returnType);
        for (const auto& p : fn->params) {
            if (p.type) emitEnumDef(p.type);
        }
        for (const auto& b : fn->blocks) {
            for (const auto& inst : b->instructions) {
                if (inst.type) emitEnumDef(inst.type);
            }
        }
    }
    if (!emittedEnums.empty()) {
        os << "\n";
    }

    // Emit vtables for polymorphic classes
    for (const auto* recTy : module_.recordTypes) {
        if (recTy && recTy->isPolymorphic && !recTy->vtable.empty()) {
            os << "@_vtable_" << recTy->structName << " = private unnamed_addr constant ["
               << recTy->vtable.size() << " x ptr] [";
            for (size_t i = 0; i < recTy->vtable.size(); ++i) {
                if (i > 0) os << ", ";
                if (recTy->vtable[i].empty()) {
                    os << "ptr null";
                } else {
                    os << "ptr @" << recTy->vtable[i];
                }
            }
            os << "]\n";
        }
    }

    // Emit module string constants
    for (size_t i = 0; i < module_.stringLiterals.size(); ++i) {
        const auto& str = module_.stringLiterals[i];
        int64_t totalBytes = static_cast<int64_t>(str.size()) + 1;
        os << "@.str." << i << " = private unnamed_addr constant ["
           << totalBytes << " x i8] c\"" << escapeLLVMString(str) << "\", align 1\n";
    }

    // Emit module global variables
    for (const auto& g : module_.globals) {
        if (g.isArray) {
            std::string elemStr = (g.type && g.type->elementType) ? llvmType(g.type->elementType) : "i32";
            int64_t arrCount = g.arraySize > 0 ? g.arraySize : (g.type ? g.type->arraySize : 0);
            if (arrCount <= 0) arrCount = 1;
            if (!g.arrayInitializers.empty()) {
                os << "@" << g.name << " = internal global [" << arrCount << " x " << elemStr << "] [";
                for (int64_t i = 0; i < arrCount; ++i) {
                    if (i > 0) os << ", ";
                    int64_t val = (i < (int64_t)g.arrayInitializers.size()) ? g.arrayInitializers[i] : 0;
                    os << elemStr << " " << val;
                }
                os << "], align 16\n";
            } else {
                os << "@" << g.name << " = internal global [" << arrCount << " x " << elemStr
                   << "] zeroinitializer, align 16\n";
            }
        } else {
            std::string tyStr = g.type ? llvmType(g.type) : "i32";
            std::string valStr = std::to_string(g.initialValue);
            if (g.type && g.type->isFloat()) {
                valStr = "0.0";
            }
            os << "@" << g.name << " = internal global " << tyStr << " " << valStr << ", align 8\n";
        }
    }

    os << "\n";
}

void LLVMCodeGen::emitRuntimeHelpers(std::ostream& os) {
    os << "; --- Tamarin Runtime Declarations ---\n";
    if (targetTriple_.isWindows()) {
        os << "declare i32 @_write(i32, ptr, i32)\n";
        os << "declare i32 @QueryPerformanceCounter(ptr)\n";
        os << "declare i32 @QueryPerformanceFrequency(ptr)\n";
        os << "define private i32 @__tamarin_raw_write(i32 %fd, ptr %buf, i32 %count) {\n";
        os << "entry:\n";
        os << "  %res = call i32 @_write(i32 %fd, ptr %buf, i32 %count)\n";
        os << "  ret i32 %res\n";
        os << "}\n\n";
    } else {
        os << "declare i64 @write(i32, ptr, i64)\n";
        os << "declare i32 @clock_gettime(i32, ptr)\n";
        os << "define private i32 @__tamarin_raw_write(i32 %fd, ptr %buf, i32 %count) {\n";
        os << "entry:\n";
        os << "  %c64 = sext i32 %count to i64\n";
        os << "  %res = call i64 @write(i32 %fd, ptr %buf, i64 %c64)\n";
        os << "  %r32 = trunc i64 %res to i32\n";
        os << "  ret i32 %r32\n";
        os << "}\n\n";
    }
    os << "declare i32 @snprintf(ptr, i64, ptr, ...)\n";
    os << "declare i32 @getchar()\n";
    os << "declare ptr @fopen(ptr, ptr)\n";
    os << "declare i32 @fclose(ptr)\n";
    os << "declare i64 @fread(ptr, i64, i64, ptr)\n";
    os << "declare i64 @fwrite(ptr, i64, i64, ptr)\n";
    os << "declare i32 @fflush(ptr)\n";
    os << "declare i32 @remove(ptr)\n";
    os << "declare ptr @malloc(i64)\n";
    os << "declare ptr @realloc(ptr, i64)\n";
    os << "declare void @free(ptr)\n";
    os << "declare void @exit(i32)\n";
    os << "declare void @llvm.memset.p0.i64(ptr nocapture writeonly, i8, i64, i1 immarg)\n";
    os << "declare void @llvm.memcpy.p0.p0.i64(ptr nocapture writeonly, ptr nocapture readonly, i64, i1 immarg)\n";
    os << "declare void @llvm.memmove.p0.p0.i64(ptr nocapture writeonly, ptr nocapture readonly, i64, i1 immarg)\n\n";

    // --- Tamarin Runtime Page Provider & Thread-Local Cache Declarations ---
    os << "declare ptr @__tamarin_runtime_page_reserve(i64)\n";
    os << "declare ptr @__tamarin_runtime_page_commit(ptr, i64, i64)\n";
    os << "declare i32 @__tamarin_runtime_page_decommit(ptr, i64, i64)\n";
    os << "declare i32 @__tamarin_runtime_page_release(ptr)\n";
    os << "declare i64 @__tamarin_runtime_page_size()\n";
    os << "declare i64 @__tamarin_runtime_allocation_granularity()\n";
    os << "declare i64 @__tamarin_runtime_working_set_bytes()\n";
    os << "declare ptr @__tamarin_runtime_rcache_acquire(i64, ptr, ptr)\n";
    os << "declare i32 @__tamarin_runtime_rcache_put(ptr, i64, i64, i64)\n";
    os << "declare ptr @__tamarin_runtime_chunk_cache_acquire(i64, ptr, ptr)\n";
    os << "declare i32 @__tamarin_runtime_chunk_cache_put(ptr, i64, i64, i64)\n";
    os << "declare ptr @__tamarin_runtime_sidecar_cache_acquire()\n";
    os << "declare void @__tamarin_runtime_sidecar_cache_put(ptr)\n";
    os << "declare void @__tamarin_runtime_sidecar_cache_clear()\n";
    os << "declare i32 @llvm.cttz.i32(i32, i1 immarg)\n";
    os << "declare i64 @llvm.cttz.i64(i64, i1 immarg)\n";
    os << "declare i32 @llvm.ctlz.i32(i32, i1 immarg)\n";
    os << "declare i64 @llvm.ctlz.i64(i64, i1 immarg)\n";
    os << "declare i32 @llvm.ctpop.i32(i32)\n";
    os << "declare i64 @llvm.ctpop.i64(i64)\n";
    os << "declare i32 @llvm.bitreverse.i32(i32)\n";
    os << "declare i64 @llvm.bitreverse.i64(i64)\n";
    os << "declare i32 @llvm.bswap.i32(i32)\n";
    os << "declare i64 @llvm.bswap.i64(i64)\n";
    os << "declare float @llvm.sqrt.f32(float)\n";
    os << "declare double @llvm.sqrt.f64(double)\n";
    os << "declare float @llvm.vector.reduce.fadd.v4f32(float, <4 x float>)\n";
    os << "declare float @llvm.vector.reduce.fadd.v8f32(float, <8 x float>)\n";
    os << "declare i32 @llvm.vector.reduce.add.v4i32(<4 x i32>)\n";
    os << "declare i32 @llvm.vector.reduce.add.v8i32(<8 x i32>)\n";
    os << "declare i32 @llvm.vector.reduce.add.v16i32(<16 x i32>)\n\n";

    // --- Tamarin Generational Slab Runtime API ---
    os << "declare void @__tamarin_slab_init(ptr, i64, i64, i64)\n";
    os << "declare i64 @__tamarin_slab_insert(ptr, ptr)\n";
    os << "declare ptr @__tamarin_slab_get(ptr, i64) nounwind readonly\n";
    os << "declare i32 @__tamarin_slab_is_valid(ptr, i64) nounwind readonly\n";
    os << "declare i32 @__tamarin_slab_try_get(ptr, i64, ptr)\n";
    os << "declare i32 @__tamarin_slab_remove(ptr, i64, ptr)\n";
    os << "declare i64 @__tamarin_slab_count(ptr) nounwind readonly\n";
    os << "declare i64 @__tamarin_slab_capacity(ptr) nounwind readonly\n";
    os << "declare void @__tamarin_slab_clear(ptr, ptr)\n";
    os << "declare void @__tamarin_slab_destroy(ptr, ptr)\n";
    os << "declare void @__tamarin_slab_panic(i32, i32, i32) noreturn nounwind\n\n";

    // --- Tamarin Structured Concurrency & Parallel Execution Runtime API ---
    os << "declare void @tamarin_parallel_for(i64, i64, i64, ptr, ptr)\n";
    os << "declare ptr @tamarin_task_scope_begin()\n";
    os << "declare void @tamarin_task_spawn(ptr, ptr, ptr)\n";
    os << "declare void @tamarin_task_scope_end(ptr)\n";
    os << "declare void @tamarin_yield()\n";
    os << "declare i32 @tamarin_concurrency_get_num_threads()\n";
    os << "declare ptr @tamarin_async_task_create(ptr, ptr, i64, i64)\n";
    os << "declare ptr @tamarin_async_task_await(ptr)\n";
    os << "declare i32 @tamarin_async_task_is_ready(ptr)\n";
    os << "declare void @tamarin_async_task_free(ptr)\n\n";

    // Fast-path inlined slab get with slow-path fallback
    os << "define internal ptr @__tamarin_slab_get_fast(ptr %slab, i64 %handle) alwaysinline {\n";
    os << "entry:\n";
    os << "  %slot64 = and i64 %handle, 4294967295\n";
    os << "  %slot = trunc i64 %slot64 to i32\n";
    os << "  %gen64 = lshr i64 %handle, 32\n";
    os << "  %gen = trunc i64 %gen64 to i32\n";
    os << "  %slab_null = icmp eq ptr %slab, null\n";
    os << "  %gen_zero = icmp eq i32 %gen, 0\n";
    os << "  %bad_hdr = or i1 %slab_null, %gen_zero\n";
    os << "  br i1 %bad_hdr, label %fallback, label %check_cap\n";
    os << "check_cap:\n";
    os << "  %cap_ptr = getelementptr inbounds i8, ptr %slab, i64 16\n";
    os << "  %cap = load i64, ptr %cap_ptr, align 8\n";
    os << "  %in_cap = icmp ult i64 %slot64, %cap\n";
    os << "  br i1 %in_cap, label %check_chunks, label %fallback\n";
    os << "check_chunks:\n";
    os << "  %chunks_ptr = getelementptr inbounds i8, ptr %slab, i64 56\n";
    os << "  %chunks = load ptr, ptr %chunks_ptr, align 8\n";
    os << "  %has_chunks = icmp ne ptr %chunks, null\n";
    os << "  br i1 %has_chunks, label %fallback, label %fast_lookup\n";
    os << "fast_lookup:\n";
    os << "  %gens_base_ptr = getelementptr inbounds i8, ptr %slab, i64 8\n";
    os << "  %gens_base = load ptr, ptr %gens_base_ptr, align 8\n";
    os << "  %gen_ptr = getelementptr inbounds i32, ptr %gens_base, i64 %slot64\n";
    os << "  %cur_gen = load i32, ptr %gen_ptr, align 4\n";
    os << "  %gen_match = icmp eq i32 %cur_gen, %gen\n";
    os << "  %active_bit = and i32 %cur_gen, 1\n";
    os << "  %is_active = icmp ne i32 %active_bit, 0\n";
    os << "  %valid = and i1 %gen_match, %is_active\n";
    os << "  br i1 %valid, label %slot_ok, label %do_panic\n";
    os << "slot_ok:\n";
    os << "  %stride_ptr = getelementptr inbounds i8, ptr %slab, i64 48\n";
    os << "  %stride = load i64, ptr %stride_ptr, align 8\n";
    os << "  %offset = mul i64 %slot64, %stride\n";
    os << "  %slots_base_ptr = getelementptr inbounds i8, ptr %slab, i64 0\n";
    os << "  %slots_base = load ptr, ptr %slots_base_ptr, align 8\n";
    os << "  %slot_addr = getelementptr inbounds i8, ptr %slots_base, i64 %offset\n";
    os << "  ret ptr %slot_addr\n";
    os << "do_panic:\n";
    os << "  call void @__tamarin_slab_panic(i32 %slot, i32 %gen, i32 %cur_gen)\n";
    os << "  unreachable\n";
    os << "fallback:\n";
    os << "  %res = call ptr @__tamarin_slab_get(ptr %slab, i64 %handle)\n";
    os << "  ret ptr %res\n";
    os << "}\n\n";

    // String constant for bounds error (43 characters + 1 newline = 44 bytes)
    os << "@__tamarin_str_bounds_err = private unnamed_addr constant [44 x i8] c\"TAMARIN runtime error: index out of bounds\\0A\\00\"\n";
    os << "@__tamarin_str_alloc_err = private unnamed_addr constant [42 x i8] c\"TAMARIN runtime error: allocation failed\\0A\\00\"\n";
    os << "@__tamarin_str_region_oom = private unnamed_addr constant [45 x i8] c\"TAMARIN runtime error: region out of memory\\0A\\00\"\n";
    os << "@__tamarin_str_region_stats_hdr = private unnamed_addr constant [42 x i8] c\"[TAMARIN MEMORY STATS] Region destroyed:\\0A\\00\"\n";
    os << "@__tamarin_str_region_stats_fmt = private unnamed_addr constant [507 x i8] c\"  Initial hint:      %10llu bytes\\0A  Virtual reserved:  %10llu bytes\\0A  Committed bytes / commit charge: %10llu bytes\\0A  Process working set (resident RAM): %10llu bytes\\0A  Payload handed out: %10llu bytes\\0A  Alignment padding:        %10llu bytes\\0A  Unused committed bytes:   %10llu bytes\\0A  Chunks:            %10llu\\0A  Reservation count:        %10llu\\0A  Commit operation count:   %10llu\\0A  Sidecar capacity:  %10llu bytes\\0A  Sidecar used:      %10llu bytes\\0A  Destructor records:%10llu\\0A  Allocations:       %10llu\\0A\\00\"\n";
    os << "@__tamarin_flag_memory_stats = internal global i32 " << (enableMemoryStats_ ? 1 : 0) << "\n";
    os << "@__tamarin_nl = private unnamed_addr constant [1 x i8] c\"\\0A\"\n";
    os << "@__tamarin_fmt_int = private unnamed_addr constant [3 x i8] c\"%d\\00\"\n";
    os << "@__tamarin_fmt_i64 = private unnamed_addr constant [5 x i8] c\"%lld\\00\"\n";
    os << "@__tamarin_fmt_float = private unnamed_addr constant [3 x i8] c\"%g\\00\"\n";
    os << "@__tamarin_str_true = private unnamed_addr constant [4 x i8] c\"true\"\n";
    os << "@__tamarin_str_false = private unnamed_addr constant [5 x i8] c\"false\"\n";
    os << "@__tamarin_mode_r = private unnamed_addr constant [2 x i8] c\"r\\00\"\n";
    os << "@__tamarin_mode_rb = private unnamed_addr constant [3 x i8] c\"rb\\00\"\n";
    os << "@__tamarin_mode_wb = private unnamed_addr constant [3 x i8] c\"wb\\00\"\n";
    os << "@__tamarin_mode_ab = private unnamed_addr constant [3 x i8] c\"ab\\00\"\n";
    os << "@__tamarin_input_buf = internal global [4096 x i8] zeroinitializer\n";
    os << "@__tamarin_bench_start = internal global i64 0\n";
    os << "@__tamarin_bench_freq = internal global i64 0\n\n";

    if (targetTriple_.isWindows()) {
        os << "define private void @__tamarin_benchmark_start() {\n";
        os << "entry:\n";
        os << "  %call_freq = call i32 @QueryPerformanceFrequency(ptr @__tamarin_bench_freq)\n";
        os << "  %freq = load i64, ptr @__tamarin_bench_freq, align 8\n";
        os << "  %freq_ok = icmp sgt i64 %freq, 0\n";
        os << "  br i1 %freq_ok, label %take_start, label %timer_err\n";
        os << "take_start:\n";
        os << "  %call_cnt = call i32 @QueryPerformanceCounter(ptr @__tamarin_bench_start)\n";
        os << "  ret void\n";
        os << "timer_err:\n";
        os << "  call void @exit(i32 1)\n";
        os << "  unreachable\n";
        os << "}\n\n";

        os << "define private double @__tamarin_benchmark_elapsed_ms() {\n";
        os << "entry:\n";
        os << "  %end = alloca i64, align 8\n";
        os << "  %call_cnt = call i32 @QueryPerformanceCounter(ptr %end)\n";
        os << "  %t0 = load i64, ptr @__tamarin_bench_start, align 8\n";
        os << "  %t1 = load i64, ptr %end, align 8\n";
        os << "  %freq = load i64, ptr @__tamarin_bench_freq, align 8\n";
        os << "  %freq_ok = icmp sgt i64 %freq, 0\n";
        os << "  br i1 %freq_ok, label %calc, label %timer_err\n";
        os << "calc:\n";
        os << "  %diff = sub i64 %t1, %t0\n";
        os << "  %diff_f = sitofp i64 %diff to double\n";
        os << "  %freq_f = sitofp i64 %freq to double\n";
        os << "  %diff_ms = fmul double %diff_f, 1000.0\n";
        os << "  %ms = fdiv double %diff_ms, %freq_f\n";
        os << "  ret double %ms\n";
        os << "timer_err:\n";
        os << "  call void @exit(i32 1)\n";
        os << "  unreachable\n";
        os << "}\n\n";
    } else {
        int clockId = targetTriple_.isMacOS() ? 6 : 1;
        os << "define private void @__tamarin_benchmark_start() {\n";
        os << "entry:\n";
        os << "  %ts = alloca { i64, i64 }, align 8\n";
        os << "  %call_cnt = call i32 @clock_gettime(i32 " << clockId << ", ptr %ts)\n";
        os << "  %cnt_ok = icmp eq i32 %call_cnt, 0\n";
        os << "  br i1 %cnt_ok, label %read_time, label %time_err\n";
        os << "time_err:\n";
        os << "  call void @exit(i32 1)\n";
        os << "  unreachable\n";
        os << "read_time:\n";
        os << "  %sec = load i64, ptr %ts, align 8\n";
        os << "  %p_nsec = getelementptr inbounds { i64, i64 }, ptr %ts, i32 0, i32 1\n";
        os << "  %nsec = load i64, ptr %p_nsec, align 8\n";
        os << "  %sec_ns = mul i64 %sec, 1000000000\n";
        os << "  %total_ns = add i64 %sec_ns, %nsec\n";
        os << "  store i64 %total_ns, ptr @__tamarin_bench_start, align 8\n";
        os << "  ret void\n";
        os << "}\n\n";

        os << "define private double @__tamarin_benchmark_elapsed_ms() {\n";
        os << "entry:\n";
        os << "  %ts = alloca { i64, i64 }, align 8\n";
        os << "  %call_cnt = call i32 @clock_gettime(i32 " << clockId << ", ptr %ts)\n";
        os << "  %cnt_ok = icmp eq i32 %call_cnt, 0\n";
        os << "  br i1 %cnt_ok, label %read_elapsed, label %elapsed_err\n";
        os << "elapsed_err:\n";
        os << "  call void @exit(i32 1)\n";
        os << "  unreachable\n";
        os << "read_elapsed:\n";
        os << "  %sec = load i64, ptr %ts, align 8\n";
        os << "  %p_nsec = getelementptr inbounds { i64, i64 }, ptr %ts, i32 0, i32 1\n";
        os << "  %nsec = load i64, ptr %p_nsec, align 8\n";
        os << "  %sec_ns = mul i64 %sec, 1000000000\n";
        os << "  %total_ns = add i64 %sec_ns, %nsec\n";
        os << "  %t0 = load i64, ptr @__tamarin_bench_start, align 8\n";
        os << "  %diff_ns = sub i64 %total_ns, %t0\n";
        os << "  %diff_f = sitofp i64 %diff_ns to double\n";
        os << "  %ms = fdiv double %diff_f, 1000000.0\n";
        os << "  ret double %ms\n";
        os << "}\n\n";
    }

    os << "define private i64 @__tamarin_f64_to_bits(double %d) {\n";
    os << "entry:\n";
    os << "  %bits = bitcast double %d to i64\n";
    os << "  ret i64 %bits\n";
    os << "}\n\n";

    os << "define private i32 @__tamarin_ctz32(i32 %x) {\n";
    os << "entry:\n";
    os << "  %r = call i32 @llvm.cttz.i32(i32 %x, i1 false)\n";
    os << "  ret i32 %r\n";
    os << "}\n\n";

    os << "define private i32 @__tamarin_ctz64(i64 %x) {\n";
    os << "entry:\n";
    os << "  %r = call i64 @llvm.cttz.i64(i64 %x, i1 false)\n";
    os << "  %t = trunc i64 %r to i32\n";
    os << "  ret i32 %t\n";
    os << "}\n\n";

    os << "define private i32 @__tamarin_clz32(i32 %x) {\n";
    os << "entry:\n";
    os << "  %r = call i32 @llvm.ctlz.i32(i32 %x, i1 false)\n";
    os << "  ret i32 %r\n";
    os << "}\n\n";

    os << "define private i32 @__tamarin_clz64(i64 %x) {\n";
    os << "entry:\n";
    os << "  %r = call i64 @llvm.ctlz.i64(i64 %x, i1 false)\n";
    os << "  %t = trunc i64 %r to i32\n";
    os << "  ret i32 %t\n";
    os << "}\n\n";

    os << "define private i32 @__tamarin_popcount32(i32 %x) {\n";
    os << "entry:\n";
    os << "  %r = call i32 @llvm.ctpop.i32(i32 %x)\n";
    os << "  ret i32 %r\n";
    os << "}\n\n";

    os << "define private i32 @__tamarin_popcount64(i64 %x) {\n";
    os << "entry:\n";
    os << "  %r = call i64 @llvm.ctpop.i64(i64 %x)\n";
    os << "  %t = trunc i64 %r to i32\n";
    os << "  ret i32 %t\n";
    os << "}\n\n";

    os << "define private i32 @__tamarin_bitrev32(i32 %x) {\n";
    os << "entry:\n";
    os << "  %r = call i32 @llvm.bitreverse.i32(i32 %x)\n";
    os << "  ret i32 %r\n";
    os << "}\n\n";

    os << "define private i64 @__tamarin_bitrev64(i64 %x) {\n";
    os << "entry:\n";
    os << "  %r = call i64 @llvm.bitreverse.i64(i64 %x)\n";
    os << "  ret i64 %r\n";
    os << "}\n\n";

    os << "define private i32 @__tamarin_bswap32(i32 %x) {\n";
    os << "entry:\n";
    os << "  %r = call i32 @llvm.bswap.i32(i32 %x)\n";
    os << "  ret i32 %r\n";
    os << "}\n\n";

    os << "define private i64 @__tamarin_bswap64(i64 %x) {\n";
    os << "entry:\n";
    os << "  %r = call i64 @llvm.bswap.i64(i64 %x)\n";
    os << "  ret i64 %r\n";
    os << "}\n\n";

    os << "define private i32 @__tamarin_hash32(i32 %x) {\n";
    os << "entry:\n";
    os << "  %s1 = lshr i32 %x, 16\n";
    os << "  %h1 = xor i32 %x, %s1\n";
    os << "  %m1 = mul i32 %h1, -2048144789\n";
    os << "  %s2 = lshr i32 %m1, 13\n";
    os << "  %h2 = xor i32 %m1, %s2\n";
    os << "  %m2 = mul i32 %h2, -1028477387\n";
    os << "  %s3 = lshr i32 %m2, 16\n";
    os << "  %h3 = xor i32 %m2, %s3\n";
    os << "  ret i32 %h3\n";
    os << "}\n\n";

    os << "define private i32 @__tamarin_hash64(i64 %x) {\n";
    os << "entry:\n";
    os << "  %s1 = lshr i64 %x, 30\n";
    os << "  %h1 = xor i64 %x, %s1\n";
    os << "  %m1 = mul i64 %h1, -4658895280553007463\n";
    os << "  %s2 = lshr i64 %m1, 27\n";
    os << "  %h2 = xor i64 %m1, %s2\n";
    os << "  %m2 = mul i64 %h2, -7723592293117613589\n";
    os << "  %s3 = lshr i64 %m2, 31\n";
    os << "  %h3 = xor i64 %m2, %s3\n";
    os << "  %s4 = lshr i64 %h3, 32\n";
    os << "  %h4 = xor i64 %h3, %s4\n";
    os << "  %t = trunc i64 %h4 to i32\n";
    os << "  ret i32 %t\n";
    os << "}\n\n";

    os << "define private i32 @__tamarin_hash_f32(float %f) {\n";
    os << "entry:\n";
    os << "  %b = bitcast float %f to i32\n";
    os << "  %h = call i32 @__tamarin_hash32(i32 %b)\n";
    os << "  ret i32 %h\n";
    os << "}\n\n";

    os << "define private i32 @__tamarin_hash_f64(double %d) {\n";
    os << "entry:\n";
    os << "  %b = bitcast double %d to i64\n";
    os << "  %h = call i32 @__tamarin_hash64(i64 %b)\n";
    os << "  ret i32 %h\n";
    os << "}\n\n";

    os << "define private i32 @__tamarin_hash_bytes(ptr %p, i64 %len) {\n";
    os << "entry:\n";
    os << "  %is_null = icmp eq ptr %p, null\n";
    os << "  br i1 %is_null, label %ret_zero, label %check_len\n";
    os << "ret_zero:\n";
    os << "  ret i32 0\n";
    os << "check_len:\n";
    os << "  %is_empty = icmp eq i64 %len, 0\n";
    os << "  br i1 %is_empty, label %ret_fnv, label %loop\n";
    os << "ret_fnv:\n";
    os << "  ret i32 -2128831035\n";
    os << "loop:\n";
    os << "  %i = phi i64 [ 0, %check_len ], [ %next_i, %loop ]\n";
    os << "  %h = phi i32 [ -2128831035, %check_len ], [ %h_mul, %loop ]\n";
    os << "  %ptr_elem = getelementptr inbounds i8, ptr %p, i64 %i\n";
    os << "  %b_val = load i8, ptr %ptr_elem, align 1\n";
    os << "  %b_i32 = zext i8 %b_val to i32\n";
    os << "  %h_xor = xor i32 %h, %b_i32\n";
    os << "  %h_mul = mul i32 %h_xor, 16777619\n";
    os << "  %next_i = add i64 %i, 1\n";
    os << "  %done = icmp eq i64 %next_i, %len\n";
    os << "  br i1 %done, label %exit, label %loop\n";
    os << "exit:\n";
    os << "  ret i32 %h_mul\n";
    os << "}\n\n";

    os << "define private i32 @__tamarin_hash_view({ ptr, i64 } %view) {\n";
    os << "entry:\n";
    os << "  %p = extractvalue { ptr, i64 } %view, 0\n";
    os << "  %len = extractvalue { ptr, i64 } %view, 1\n";
    os << "  %h = call i32 @__tamarin_hash_bytes(ptr %p, i64 %len)\n";
    os << "  ret i32 %h\n";
    os << "}\n\n";

    os << "define private float @__tamarin_sqrtf(float %x) alwaysinline mustprogress nounwind {\n";
    os << "entry:\n";
    os << "  %r = call " << (fastMath_ ? "fast " : "") << "float @llvm.sqrt.f32(float %x)\n";
    os << "  ret float %r\n";
    os << "}\n\n";

    os << "define private double @__tamarin_sqrt(double %x) alwaysinline mustprogress nounwind {\n";
    os << "entry:\n";
    os << "  %r = call " << (fastMath_ ? "fast " : "") << "double @llvm.sqrt.f64(double %x)\n";
    os << "  ret double %r\n";
    os << "}\n\n";

    os << "define private void @__tamarin_bounds_error() cold noreturn nounwind {\n";
    os << "entry:\n";
    os << "  %call = call i32 @__tamarin_raw_write(i32 2, ptr @__tamarin_str_bounds_err, i32 43)\n";
    os << "  call void @exit(i32 1)\n";
    os << "  unreachable\n";
    os << "}\n\n";

    os << "define private void @__tamarin_alloc_error() cold noreturn nounwind {\n";
    os << "entry:\n";
    os << "  %call = call i32 @__tamarin_raw_write(i32 2, ptr @__tamarin_str_alloc_err, i32 41)\n";
    os << "  call void @exit(i32 1)\n";
    os << "  unreachable\n";
    os << "}\n\n";

    // Unified low-level write sink for stdout with 64-bit length loop and partial write handling
    os << "define private void @__tamarin_write_stdout(ptr %buf, i64 %len) {\n";
    os << "entry:\n";
    os << "  %p_null = icmp eq ptr %buf, null\n";
    os << "  %len_nonpos = icmp sle i64 %len, 0\n";
    os << "  %skip = or i1 %p_null, %len_nonpos\n";
    os << "  br i1 %skip, label %done, label %loop\n";
    os << "loop:\n";
    os << "  %cur_ptr = phi ptr [ %buf, %entry ], [ %next_ptr, %step ]\n";
    os << "  %remaining = phi i64 [ %len, %entry ], [ %next_rem, %step ]\n";
    os << "  %limit = icmp slt i64 %remaining, 65536\n";
    os << "  %chunk = select i1 %limit, i64 %remaining, i64 65536\n";
    os << "  %chunk_i32 = trunc i64 %chunk to i32\n";
    os << "  %written = call i32 @__tamarin_raw_write(i32 1, ptr %cur_ptr, i32 %chunk_i32)\n";
    os << "  %progress = icmp sgt i32 %written, 0\n";
    os << "  br i1 %progress, label %step, label %done\n";
    os << "step:\n";
    os << "  %w_i64 = sext i32 %written to i64\n";
    os << "  %next_rem = sub i64 %remaining, %w_i64\n";
    os << "  %next_ptr = getelementptr inbounds i8, ptr %cur_ptr, i64 %w_i64\n";
    os << "  %finished = icmp sle i64 %next_rem, 0\n";
    os << "  br i1 %finished, label %done, label %loop\n";
    os << "done:\n";
    os << "  ret void\n";
    os << "}\n\n";

    os << "define private void @__tamarin_println_void() {\n";
    os << "entry:\n";
    os << "  call void @__tamarin_write_stdout(ptr @__tamarin_nl, i64 1)\n";
    os << "  ret void\n";
    os << "}\n\n";

    os << "define private void @__tamarin_print_str({ ptr, i64 } %s) {\n";
    os << "entry:\n";
    os << "  %ptr = extractvalue { ptr, i64 } %s, 0\n";
    os << "  %len = extractvalue { ptr, i64 } %s, 1\n";
    os << "  call void @__tamarin_write_stdout(ptr %ptr, i64 %len)\n";
    os << "  ret void\n";
    os << "}\n\n";

    os << "define private void @__tamarin_println_str({ ptr, i64 } %s) {\n";
    os << "entry:\n";
    os << "  call void @__tamarin_print_str({ ptr, i64 } %s)\n";
    os << "  call void @__tamarin_println_void()\n";
    os << "  ret void\n";
    os << "}\n\n";

    os << "define private void @__tamarin_print_int(i32 %n) {\n";
    os << "entry:\n";
    os << "  %buf = alloca [32 x i8], align 1\n";
    os << "  %w = call i32 (ptr, i64, ptr, ...) @snprintf(ptr %buf, i64 32, ptr @__tamarin_fmt_int, i32 %n)\n";
    os << "  %w_pos = icmp sgt i32 %w, 0\n";
    os << "  %len_i32 = select i1 %w_pos, i32 %w, i32 0\n";
    os << "  %len_i64 = sext i32 %len_i32 to i64\n";
    os << "  call void @__tamarin_write_stdout(ptr %buf, i64 %len_i64)\n";
    os << "  ret void\n";
    os << "}\n\n";

    os << "define private void @__tamarin_println_int(i32 %n) {\n";
    os << "entry:\n";
    os << "  call void @__tamarin_print_int(i32 %n)\n";
    os << "  call void @__tamarin_println_void()\n";
    os << "  ret void\n";
    os << "}\n\n";

    os << "define private void @__tamarin_print_i64(i64 %n) {\n";
    os << "entry:\n";
    os << "  %buf = alloca [32 x i8], align 1\n";
    os << "  %w = call i32 (ptr, i64, ptr, ...) @snprintf(ptr %buf, i64 32, ptr @__tamarin_fmt_i64, i64 %n)\n";
    os << "  %w_pos = icmp sgt i32 %w, 0\n";
    os << "  %len_i32 = select i1 %w_pos, i32 %w, i32 0\n";
    os << "  %len_i64 = sext i32 %len_i32 to i64\n";
    os << "  call void @__tamarin_write_stdout(ptr %buf, i64 %len_i64)\n";
    os << "  ret void\n";
    os << "}\n\n";

    os << "define private void @__tamarin_println_i64(i64 %n) {\n";
    os << "entry:\n";
    os << "  call void @__tamarin_print_i64(i64 %n)\n";
    os << "  call void @__tamarin_println_void()\n";
    os << "  ret void\n";
    os << "}\n\n";

    os << "define private void @__tamarin_print_float(double %f) {\n";
    os << "entry:\n";
    os << "  %buf = alloca [64 x i8], align 1\n";
    os << "  %w = call i32 (ptr, i64, ptr, ...) @snprintf(ptr %buf, i64 64, ptr @__tamarin_fmt_float, double %f)\n";
    os << "  %w_pos = icmp sgt i32 %w, 0\n";
    os << "  %len_i32 = select i1 %w_pos, i32 %w, i32 0\n";
    os << "  %len_i64 = sext i32 %len_i32 to i64\n";
    os << "  call void @__tamarin_write_stdout(ptr %buf, i64 %len_i64)\n";
    os << "  ret void\n";
    os << "}\n\n";

    os << "define private void @__tamarin_println_float(double %f) {\n";
    os << "entry:\n";
    os << "  call void @__tamarin_print_float(double %f)\n";
    os << "  call void @__tamarin_println_void()\n";
    os << "  ret void\n";
    os << "}\n\n";

    os << "define private void @__tamarin_print_bool(i1 %b) {\n";
    os << "entry:\n";
    os << "  %str = select i1 %b, ptr @__tamarin_str_true, ptr @__tamarin_str_false\n";
    os << "  %len = select i1 %b, i64 4, i64 5\n";
    os << "  call void @__tamarin_write_stdout(ptr %str, i64 %len)\n";
    os << "  ret void\n";
    os << "}\n\n";

    os << "define private void @__tamarin_println_bool(i1 %b) {\n";
    os << "entry:\n";
    os << "  call void @__tamarin_print_bool(i1 %b)\n";
    os << "  call void @__tamarin_println_void()\n";
    os << "  ret void\n";
    os << "}\n\n";

    // Safe bounded view to null-terminated C string helper
    os << "define private ptr @__tamarin_view_to_cstr({ ptr, i64 } %view) {\n";
    os << "entry:\n";
    os << "  %p = extractvalue { ptr, i64 } %view, 0\n";
    os << "  %len = extractvalue { ptr, i64 } %view, 1\n";
    os << "  %p_null = icmp eq ptr %p, null\n";
    os << "  %len_neg = icmp slt i64 %len, 0\n";
    os << "  %len_zero = icmp eq i64 %len, 0\n";
    os << "  %bad_len = or i1 %len_neg, %len_zero\n";
    os << "  %bad_in = or i1 %p_null, %bad_len\n";
    os << "  %too_big = icmp ugt i64 %len, 65536\n";
    os << "  %fail_initial = or i1 %bad_in, %too_big\n";
    os << "  br i1 %fail_initial, label %ret_null, label %alloc\n";
    os << "alloc:\n";
    os << "  %alloc_size = add i64 %len, 1\n";
    os << "  %mem = call ptr @malloc(i64 %alloc_size)\n";
    os << "  %mem_null = icmp eq ptr %mem, null\n";
    os << "  br i1 %mem_null, label %ret_null, label %loop_init\n";
    os << "loop_init:\n";
    os << "  br label %check_loop\n";
    os << "check_loop:\n";
    os << "  %i = phi i64 [ 0, %loop_init ], [ %i_next, %store_byte ]\n";
    os << "  %is_done = icmp eq i64 %i, %len\n";
    os << "  br i1 %is_done, label %finish, label %loop_body\n";
    os << "loop_body:\n";
    os << "  %src_ptr = getelementptr inbounds i8, ptr %p, i64 %i\n";
    os << "  %b = load i8, ptr %src_ptr\n";
    os << "  %is_null_byte = icmp eq i8 %b, 0\n";
    os << "  br i1 %is_null_byte, label %fail_embedded_null, label %store_byte\n";
    os << "store_byte:\n";
    os << "  %dst_ptr = getelementptr inbounds i8, ptr %mem, i64 %i\n";
    os << "  store i8 %b, ptr %dst_ptr\n";
    os << "  %i_next = add i64 %i, 1\n";
    os << "  br label %check_loop\n";
    os << "fail_embedded_null:\n";
    os << "  call void @free(ptr %mem)\n";
    os << "  br label %ret_null\n";
    os << "finish:\n";
    os << "  %term_ptr = getelementptr inbounds i8, ptr %mem, i64 %len\n";
    os << "  store i8 0, ptr %term_ptr\n";
    os << "  ret ptr %mem\n";
    os << "ret_null:\n";
    os << "  ret ptr null\n";
    os << "}\n\n";

    // Safe caller-owned input with truncation discard semantics
    os << "define private i32 @__tamarin_get_input_buf({ ptr, i64 } %buf, i32 %maxBytes) {\n";
    os << "entry:\n";
    os << "  %b_ptr = extractvalue { ptr, i64 } %buf, 0\n";
    os << "  %b_len = extractvalue { ptr, i64 } %buf, 1\n";
    os << "  %mb_i64 = sext i32 %maxBytes to i64\n";
    os << "  %limit = icmp slt i64 %mb_i64, %b_len\n";
    os << "  %cap = select i1 %limit, i64 %mb_i64, i64 %b_len\n";
    os << "  %has_cap = icmp sgt i64 %cap, 1\n";
    os << "  br i1 %has_cap, label %setup, label %no_cap\n";
    os << "no_cap:\n";
    os << "  %cap_is_one = icmp eq i64 %cap, 1\n";
    os << "  br i1 %cap_is_one, label %check_cap1, label %ret_zero\n";
    os << "check_cap1:\n";
    os << "  %c1_ch = call i32 @getchar()\n";
    os << "  %c1_eof = icmp eq i32 %c1_ch, -1\n";
    os << "  br i1 %c1_eof, label %c1_ret_eof, label %c1_check_nl\n";
    os << "c1_check_nl:\n";
    os << "  %c1_nl = icmp eq i32 %c1_ch, 10\n";
    os << "  br i1 %c1_nl, label %c1_ret_zero, label %c1_check_cr\n";
    os << "c1_check_cr:\n";
    os << "  %c1_cr = icmp eq i32 %c1_ch, 13\n";
    os << "  br i1 %c1_cr, label %c1_handle_cr, label %c1_discard\n";
    os << "c1_handle_cr:\n";
    os << "  %c1_ch2 = call i32 @getchar()\n";
    os << "  %c1_nl2 = icmp eq i32 %c1_ch2, 10\n";
    os << "  br i1 %c1_nl2, label %c1_ret_zero, label %c1_discard_with_ch2\n";
    os << "c1_discard_with_ch2:\n";
    os << "  %c1_d2_eof = icmp eq i32 %c1_ch2, -1\n";
    os << "  %c1_d2_nl = icmp eq i32 %c1_ch2, 10\n";
    os << "  %c1_d2_done = or i1 %c1_d2_eof, %c1_d2_nl\n";
    os << "  br i1 %c1_d2_done, label %c1_ret_trunc, label %c1_discard\n";
    os << "c1_discard:\n";
    os << "  %d1 = call i32 @getchar()\n";
    os << "  %d1_eof = icmp eq i32 %d1, -1\n";
    os << "  %d1_nl = icmp eq i32 %d1, 10\n";
    os << "  %d1_done = or i1 %d1_eof, %d1_nl\n";
    os << "  br i1 %d1_done, label %c1_ret_trunc, label %c1_discard\n";
    os << "c1_ret_eof:\n";
    os << "  store i8 0, ptr %b_ptr\n";
    os << "  ret i32 -1\n";
    os << "c1_ret_zero:\n";
    os << "  store i8 0, ptr %b_ptr\n";
    os << "  ret i32 0\n";
    os << "c1_ret_trunc:\n";
    os << "  store i8 0, ptr %b_ptr\n";
    os << "  ret i32 -2\n";
    os << "ret_zero:\n";
    os << "  ret i32 0\n";
    os << "setup:\n";
    os << "  %max_chars = sub i64 %cap, 1\n";
    os << "  br label %loop\n";
    os << "loop:\n";
    os << "  %stored = phi i64 [ 0, %setup ], [ %next_stored, %normal_char ], [ %eff_stored, %set_cr ]\n";
    os << "  %pending_cr = phi i1 [ 0, %setup ], [ 0, %normal_char ], [ 1, %set_cr ]\n";
    os << "  %ch = call i32 @getchar()\n";
    os << "  br i1 %pending_cr, label %check_cr_target, label %handle_ch\n";
    os << "check_cr_target:\n";
    os << "  %is_crlf = icmp eq i32 %ch, 10\n";
    os << "  br i1 %is_crlf, label %crlf_end, label %not_crlf\n";
    os << "crlf_end:\n";
    os << "  %term_crlf = getelementptr inbounds i8, ptr %b_ptr, i64 %stored\n";
    os << "  store i8 0, ptr %term_crlf\n";
    os << "  %crlf_len = trunc i64 %stored to i32\n";
    os << "  ret i32 %crlf_len\n";
    os << "not_crlf:\n";
    os << "  %can_store_cr = icmp slt i64 %stored, %max_chars\n";
    os << "  br i1 %can_store_cr, label %consume_pending_cr, label %discard_line_from_cr\n";
    os << "consume_pending_cr:\n";
    os << "  %slot_cr = getelementptr inbounds i8, ptr %b_ptr, i64 %stored\n";
    os << "  store i8 13, ptr %slot_cr\n";
    os << "  %stored_cr = add i64 %stored, 1\n";
    os << "  br label %handle_ch\n";
    os << "handle_ch:\n";
    os << "  %eff_stored = phi i64 [ %stored, %loop ], [ %stored_cr, %consume_pending_cr ]\n";
    os << "  %is_eof = icmp eq i32 %ch, -1\n";
    os << "  br i1 %is_eof, label %handle_eof, label %check_nl\n";
    os << "handle_eof:\n";
    os << "  %is_empty = icmp eq i64 %eff_stored, 0\n";
    os << "  br i1 %is_empty, label %ret_eof, label %normal_end\n";
    os << "check_nl:\n";
    os << "  %is_nl = icmp eq i32 %ch, 10\n";
    os << "  br i1 %is_nl, label %normal_end, label %check_cr\n";
    os << "check_cr:\n";
    os << "  %is_cr = icmp eq i32 %ch, 13\n";
    os << "  br i1 %is_cr, label %set_cr, label %check_store\n";
    os << "set_cr:\n";
    os << "  br label %loop\n";
    os << "check_store:\n";
    os << "  %can_store = icmp slt i64 %eff_stored, %max_chars\n";
    os << "  br i1 %can_store, label %normal_char, label %discard_line\n";
    os << "normal_char:\n";
    os << "  %trunc_ch = trunc i32 %ch to i8\n";
    os << "  %slot_ch = getelementptr inbounds i8, ptr %b_ptr, i64 %eff_stored\n";
    os << "  store i8 %trunc_ch, ptr %slot_ch\n";
    os << "  %next_stored = add i64 %eff_stored, 1\n";
    os << "  br label %loop\n";
    os << "discard_line_from_cr:\n";
    os << "  %dcr_eof = icmp eq i32 %ch, -1\n";
    os << "  %dcr_nl = icmp eq i32 %ch, 10\n";
    os << "  %dcr_done = or i1 %dcr_eof, %dcr_nl\n";
    os << "  br i1 %dcr_done, label %ret_trunc_cr, label %discard_loop_cr\n";
    os << "discard_loop_cr:\n";
    os << "  %dcr_ch = call i32 @getchar()\n";
    os << "  %dcr_is_eof = icmp eq i32 %dcr_ch, -1\n";
    os << "  %dcr_is_nl = icmp eq i32 %dcr_ch, 10\n";
    os << "  %dcr_is_done = or i1 %dcr_is_eof, %dcr_is_nl\n";
    os << "  br i1 %dcr_is_done, label %ret_trunc_cr, label %discard_loop_cr\n";
    os << "ret_trunc_cr:\n";
    os << "  %term_cr = getelementptr inbounds i8, ptr %b_ptr, i64 %stored\n";
    os << "  store i8 0, ptr %term_cr\n";
    os << "  ret i32 -2\n";
    os << "discard_line:\n";
    os << "  %d_eof = icmp eq i32 %ch, -1\n";
    os << "  %d_nl = icmp eq i32 %ch, 10\n";
    os << "  %already_done = or i1 %d_eof, %d_nl\n";
    os << "  br i1 %already_done, label %ret_trunc, label %discard_loop\n";
    os << "discard_loop:\n";
    os << "  %d_ch = call i32 @getchar()\n";
    os << "  %d_is_eof = icmp eq i32 %d_ch, -1\n";
    os << "  %d_is_nl = icmp eq i32 %d_ch, 10\n";
    os << "  %d_is_done = or i1 %d_is_eof, %d_is_nl\n";
    os << "  br i1 %d_is_done, label %ret_trunc, label %discard_loop\n";
    os << "normal_end:\n";
    os << "  %end_len = phi i64 [ %eff_stored, %handle_eof ], [ %eff_stored, %check_nl ]\n";
    os << "  %term_ptr = getelementptr inbounds i8, ptr %b_ptr, i64 %end_len\n";
    os << "  store i8 0, ptr %term_ptr\n";
    os << "  %end_i32 = trunc i64 %end_len to i32\n";
    os << "  ret i32 %end_i32\n";
    os << "ret_eof:\n";
    os << "  store i8 0, ptr %b_ptr\n";
    os << "  ret i32 -1\n";
    os << "ret_trunc:\n";
    os << "  %trunc_term = getelementptr inbounds i8, ptr %b_ptr, i64 %eff_stored\n";
    os << "  store i8 0, ptr %trunc_term\n";
    os << "  ret i32 -2\n";
    os << "}\n\n";

    // Legacy getInput() returning temporary view into shared static storage
    os << "define private { ptr, i64 } @__tamarin_get_input() {\n";
    os << "entry:\n";
    os << "  %p0 = getelementptr inbounds [4096 x i8], ptr @__tamarin_input_buf, i64 0, i64 0\n";
    os << "  %v0 = insertvalue { ptr, i64 } undef, ptr %p0, 0\n";
    os << "  %buf_view = insertvalue { ptr, i64 } %v0, i64 4096, 1\n";
    os << "  %n = call i32 @__tamarin_get_input_buf({ ptr, i64 } %buf_view, i32 4096)\n";
    os << "  %is_neg = icmp slt i32 %n, 0\n";
    os << "  %valid_len = select i1 %is_neg, i32 0, i32 %n\n";
    os << "  %len_i64 = sext i32 %valid_len to i64\n";
    os << "  %res = insertvalue { ptr, i64 } %v0, i64 %len_i64, 1\n";
    os << "  ret { ptr, i64 } %res\n";
    os << "}\n\n";

    // File operations with safe CRT bounded view conversion
    os << "define private i64 @__tamarin_file_open({ ptr, i64 } %path, { ptr, i64 } %mode) {\n";
    os << "entry:\n";
    os << "  %p_cstr = call ptr @__tamarin_view_to_cstr({ ptr, i64 } %path)\n";
    os << "  %p_bad = icmp eq ptr %p_cstr, null\n";
    os << "  br i1 %p_bad, label %fail_path, label %check_mode\n";
    os << "check_mode:\n";
    os << "  %m_cstr = call ptr @__tamarin_view_to_cstr({ ptr, i64 } %mode)\n";
    os << "  %m_bad = icmp eq ptr %m_cstr, null\n";
    os << "  br i1 %m_bad, label %fail_mode, label %do_open\n";
    os << "do_open:\n";
    os << "  %fp = call ptr @fopen(ptr %p_cstr, ptr %m_cstr)\n";
    os << "  call void @free(ptr %m_cstr)\n";
    os << "  call void @free(ptr %p_cstr)\n";
    os << "  %handle = ptrtoint ptr %fp to i64\n";
    os << "  ret i64 %handle\n";
    os << "fail_mode:\n";
    os << "  call void @free(ptr %p_cstr)\n";
    os << "  ret i64 0\n";
    os << "fail_path:\n";
    os << "  ret i64 0\n";
    os << "}\n\n";

    os << "define private i32 @__tamarin_file_close(i64 %handle) {\n";
    os << "entry:\n";
    os << "  %is_zero = icmp eq i64 %handle, 0\n";
    os << "  br i1 %is_zero, label %done, label %do_close\n";
    os << "do_close:\n";
    os << "  %fp = inttoptr i64 %handle to ptr\n";
    os << "  %res = call i32 @fclose(ptr %fp)\n";
    os << "  ret i32 %res\n";
    os << "done:\n";
    os << "  ret i32 0\n";
    os << "}\n\n";

    os << "define private i32 @__tamarin_file_read(i64 %handle, { ptr, i64 } %buf, i32 %maxBytes) {\n";
    os << "entry:\n";
    os << "  %is_zero = icmp eq i64 %handle, 0\n";
    os << "  br i1 %is_zero, label %err, label %do_read\n";
    os << "do_read:\n";
    os << "  %fp = inttoptr i64 %handle to ptr\n";
    os << "  %b_ptr = extractvalue { ptr, i64 } %buf, 0\n";
    os << "  %b_len = extractvalue { ptr, i64 } %buf, 1\n";
    os << "  %b_null = icmp eq ptr %b_ptr, null\n";
    os << "  %b_empty = icmp sle i64 %b_len, 0\n";
    os << "  %mb_neg = icmp sle i32 %maxBytes, 0\n";
    os << "  %invalid_buf = or i1 %b_null, %b_empty\n";
    os << "  %no_read = or i1 %invalid_buf, %mb_neg\n";
    os << "  br i1 %no_read, label %read_zero, label %perform_read\n";
    os << "perform_read:\n";
    os << "  %mb_i64 = sext i32 %maxBytes to i64\n";
    os << "  %limit = icmp slt i64 %mb_i64, %b_len\n";
    os << "  %actual = select i1 %limit, i64 %mb_i64, i64 %b_len\n";
    os << "  %n = call i64 @fread(ptr %b_ptr, i64 1, i64 %actual, ptr %fp)\n";
    os << "  %ret = trunc i64 %n to i32\n";
    os << "  ret i32 %ret\n";
    os << "read_zero:\n";
    os << "  ret i32 0\n";
    os << "err:\n";
    os << "  ret i32 -1\n";
    os << "}\n\n";

    os << "define private i32 @__tamarin_file_write(i64 %handle, { ptr, i64 } %text) {\n";
    os << "entry:\n";
    os << "  %is_zero = icmp eq i64 %handle, 0\n";
    os << "  br i1 %is_zero, label %err, label %do_write\n";
    os << "do_write:\n";
    os << "  %fp = inttoptr i64 %handle to ptr\n";
    os << "  %t_ptr = extractvalue { ptr, i64 } %text, 0\n";
    os << "  %t_len = extractvalue { ptr, i64 } %text, 1\n";
    os << "  %t_null = icmp eq ptr %t_ptr, null\n";
    os << "  %t_empty = icmp sle i64 %t_len, 0\n";
    os << "  %no_write = or i1 %t_null, %t_empty\n";
    os << "  br i1 %no_write, label %write_zero, label %perform_write\n";
    os << "perform_write:\n";
    os << "  %n = call i64 @fwrite(ptr %t_ptr, i64 1, i64 %t_len, ptr %fp)\n";
    os << "  %ret = trunc i64 %n to i32\n";
    os << "  ret i32 %ret\n";
    os << "write_zero:\n";
    os << "  ret i32 0\n";
    os << "err:\n";
    os << "  ret i32 -1\n";
    os << "}\n\n";

    os << "define private void @__tamarin_file_flush(i64 %handle) {\n";
    os << "entry:\n";
    os << "  %is_zero = icmp eq i64 %handle, 0\n";
    os << "  br i1 %is_zero, label %done, label %do_flush\n";
    os << "do_flush:\n";
    os << "  %fp = inttoptr i64 %handle to ptr\n";
    os << "  call i32 @fflush(ptr %fp)\n";
    os << "  ret void\n";
    os << "done:\n";
    os << "  ret void\n";
    os << "}\n\n";

    os << "define private i1 @__tamarin_file_exists({ ptr, i64 } %path) {\n";
    os << "entry:\n";
    os << "  %p_cstr = call ptr @__tamarin_view_to_cstr({ ptr, i64 } %path)\n";
    os << "  %p_bad = icmp eq ptr %p_cstr, null\n";
    os << "  br i1 %p_bad, label %not_found, label %do_check\n";
    os << "do_check:\n";
    os << "  %fp = call ptr @fopen(ptr %p_cstr, ptr @__tamarin_mode_r)\n";
    os << "  call void @free(ptr %p_cstr)\n";
    os << "  %is_null = icmp eq ptr %fp, null\n";
    os << "  br i1 %is_null, label %not_found, label %found\n";
    os << "found:\n";
    os << "  call i32 @fclose(ptr %fp)\n";
    os << "  ret i1 1\n";
    os << "not_found:\n";
    os << "  ret i1 0\n";
    os << "}\n\n";

    os << "define private i32 @__tamarin_file_remove({ ptr, i64 } %path) {\n";
    os << "entry:\n";
    os << "  %p_cstr = call ptr @__tamarin_view_to_cstr({ ptr, i64 } %path)\n";
    os << "  %p_bad = icmp eq ptr %p_cstr, null\n";
    os << "  br i1 %p_bad, label %fail, label %do_remove\n";
    os << "do_remove:\n";
    os << "  %res = call i32 @remove(ptr %p_cstr)\n";
    os << "  call void @free(ptr %p_cstr)\n";
    os << "  ret i32 %res\n";
    os << "fail:\n";
    os << "  ret i32 -1\n";
    os << "}\n\n";

    os << "define private i32 @__tamarin_read_file({ ptr, i64 } %path, { ptr, i64 } %buf, i32 %maxBytes) {\n";
    os << "entry:\n";
    os << "  %p_cstr = call ptr @__tamarin_view_to_cstr({ ptr, i64 } %path)\n";
    os << "  %p_bad = icmp eq ptr %p_cstr, null\n";
    os << "  br i1 %p_bad, label %err, label %open_file\n";
    os << "open_file:\n";
    os << "  %fp = call ptr @fopen(ptr %p_cstr, ptr @__tamarin_mode_rb)\n";
    os << "  call void @free(ptr %p_cstr)\n";
    os << "  %fp_bad = icmp eq ptr %fp, null\n";
    os << "  br i1 %fp_bad, label %err, label %read_data\n";
    os << "read_data:\n";
    os << "  %b_ptr = extractvalue { ptr, i64 } %buf, 0\n";
    os << "  %b_len = extractvalue { ptr, i64 } %buf, 1\n";
    os << "  %b_null = icmp eq ptr %b_ptr, null\n";
    os << "  %b_empty = icmp sle i64 %b_len, 0\n";
    os << "  %mb_neg = icmp sle i32 %maxBytes, 0\n";
    os << "  %invalid_buf = or i1 %b_null, %b_empty\n";
    os << "  %no_read = or i1 %invalid_buf, %mb_neg\n";
    os << "  br i1 %no_read, label %read_zero, label %perform_read\n";
    os << "perform_read:\n";
    os << "  %mb_i64 = sext i32 %maxBytes to i64\n";
    os << "  %limit = icmp slt i64 %mb_i64, %b_len\n";
    os << "  %actual = select i1 %limit, i64 %mb_i64, i64 %b_len\n";
    os << "  %n = call i64 @fread(ptr %b_ptr, i64 1, i64 %actual, ptr %fp)\n";
    os << "  call i32 @fclose(ptr %fp)\n";
    os << "  %ret = trunc i64 %n to i32\n";
    os << "  ret i32 %ret\n";
    os << "read_zero:\n";
    os << "  call i32 @fclose(ptr %fp)\n";
    os << "  ret i32 0\n";
    os << "err:\n";
    os << "  ret i32 -1\n";
    os << "}\n\n";

    os << "define private i32 @__tamarin_write_file({ ptr, i64 } %path, { ptr, i64 } %text) {\n";
    os << "entry:\n";
    os << "  %p_cstr = call ptr @__tamarin_view_to_cstr({ ptr, i64 } %path)\n";
    os << "  %p_bad = icmp eq ptr %p_cstr, null\n";
    os << "  br i1 %p_bad, label %err, label %open_file\n";
    os << "open_file:\n";
    os << "  %fp = call ptr @fopen(ptr %p_cstr, ptr @__tamarin_mode_wb)\n";
    os << "  call void @free(ptr %p_cstr)\n";
    os << "  %fp_bad = icmp eq ptr %fp, null\n";
    os << "  br i1 %fp_bad, label %err, label %write_data\n";
    os << "write_data:\n";
    os << "  %t_ptr = extractvalue { ptr, i64 } %text, 0\n";
    os << "  %t_len = extractvalue { ptr, i64 } %text, 1\n";
    os << "  %t_null = icmp eq ptr %t_ptr, null\n";
    os << "  %t_empty = icmp sle i64 %t_len, 0\n";
    os << "  %no_write = or i1 %t_null, %t_empty\n";
    os << "  br i1 %no_write, label %write_zero, label %perform_write\n";
    os << "perform_write:\n";
    os << "  %n = call i64 @fwrite(ptr %t_ptr, i64 1, i64 %t_len, ptr %fp)\n";
    os << "  call i32 @fclose(ptr %fp)\n";
    os << "  %ret = trunc i64 %n to i32\n";
    os << "  ret i32 %ret\n";
    os << "write_zero:\n";
    os << "  call i32 @fclose(ptr %fp)\n";
    os << "  ret i32 0\n";
    os << "err:\n";
    os << "  ret i32 -1\n";
    os << "}\n\n";

    os << "define private i32 @__tamarin_append_file({ ptr, i64 } %path, { ptr, i64 } %text) {\n";
    os << "entry:\n";
    os << "  %p_cstr = call ptr @__tamarin_view_to_cstr({ ptr, i64 } %path)\n";
    os << "  %p_bad = icmp eq ptr %p_cstr, null\n";
    os << "  br i1 %p_bad, label %err, label %open_file\n";
    os << "open_file:\n";
    os << "  %fp = call ptr @fopen(ptr %p_cstr, ptr @__tamarin_mode_ab)\n";
    os << "  call void @free(ptr %p_cstr)\n";
    os << "  %fp_bad = icmp eq ptr %fp, null\n";
    os << "  br i1 %fp_bad, label %err, label %append_data\n";
    os << "append_data:\n";
    os << "  %t_ptr = extractvalue { ptr, i64 } %text, 0\n";
    os << "  %t_len = extractvalue { ptr, i64 } %text, 1\n";
    os << "  %t_null = icmp eq ptr %t_ptr, null\n";
    os << "  %t_empty = icmp sle i64 %t_len, 0\n";
    os << "  %no_append = or i1 %t_null, %t_empty\n";
    os << "  br i1 %no_append, label %append_zero, label %perform_append\n";
    os << "perform_append:\n";
    os << "  %n = call i64 @fwrite(ptr %t_ptr, i64 1, i64 %t_len, ptr %fp)\n";
    os << "  call i32 @fclose(ptr %fp)\n";
    os << "  %ret = trunc i64 %n to i32\n";
    os << "  ret i32 %ret\n";
    os << "append_zero:\n";
    os << "  call i32 @fclose(ptr %fp)\n";
    os << "  ret i32 0\n";
    os << "err:\n";
    os << "  ret i32 -1\n";
    os << "}\n\n";

    // --- Tamarin Target Page Provider Helpers ---
    os << "define private ptr @__tamarin_page_reserve(i64 %size) {\n";
    os << "entry:\n";
    os << "  %res = call ptr @__tamarin_runtime_page_reserve(i64 %size)\n";
    os << "  ret ptr %res\n";
    os << "}\n\n";

    os << "define private ptr @__tamarin_page_commit(ptr %base, i64 %offset, i64 %size) {\n";
    os << "entry:\n";
    os << "  %res = call ptr @__tamarin_runtime_page_commit(ptr %base, i64 %offset, i64 %size)\n";
    os << "  ret ptr %res\n";
    os << "}\n\n";

    os << "define private i32 @__tamarin_page_decommit(ptr %base, i64 %offset, i64 %size) {\n";
    os << "entry:\n";
    os << "  %res = call i32 @__tamarin_runtime_page_decommit(ptr %base, i64 %offset, i64 %size)\n";
    os << "  ret i32 %res\n";
    os << "}\n\n";

    os << "define private i32 @__tamarin_page_release(ptr %base) {\n";
    os << "entry:\n";
    os << "  %res = call i32 @__tamarin_runtime_page_release(ptr %base)\n";
    os << "  ret i32 %res\n";
    os << "}\n\n";

    // --- Tamarin Region Allocator Runtime ---
    os << "define private void @__tamarin_region_oom_error() {\n";
    os << "entry:\n";
    os << "  %call = call i32 @__tamarin_raw_write(i32 2, ptr @__tamarin_str_region_oom, i32 44)\n";
    os << "  call void @exit(i32 1)\n";
    os << "  unreachable\n";
    os << "}\n\n";

    os << "define private void @__tamarin_region_init(ptr %region, i64 %initial_cap) {\n";
    os << "entry:\n";
    os << "  %is_default = icmp eq i64 %initial_cap, 0\n";
    os << "  br i1 %is_default, label %use_default, label %check_custom\n";
    os << "use_default:\n";
    os << "  %def_gran = call i64 @__tamarin_runtime_allocation_granularity()\n";
    os << "  br label %do_init\n";
    os << "check_custom:\n";
    os << "  %cap_pos = icmp sgt i64 %initial_cap, 0\n";
    os << "  %cap_lim = icmp ule i64 %initial_cap, 4611686018427387904\n";
    os << "  %cap_ok = and i1 %cap_pos, %cap_lim\n";
    os << "  br i1 %cap_ok, label %do_init, label %oom\n";
    os << "do_init:\n";
    os << "  %def_cap = phi i64 [ %def_gran, %use_default ], [ %initial_cap, %check_custom ]\n";
    os << "  call void @llvm.memset.p0.i64(ptr %region, i8 0, i64 120, i1 false)\n";
    os << "  %p_def = getelementptr inbounds %__tamarin_region, ptr %region, i32 0, i32 2\n";
    os << "  store i64 %def_cap, ptr %p_def, align 8\n";
    os << "  ret void\n";
    os << "oom:\n";
    os << "  call void @__tamarin_region_oom_error()\n";
    os << "  unreachable\n";
    os << "}\n\n";

    os << "define private ptr @__tamarin_region_alloc(ptr %region, i64 %size, i64 %align) {\n";
    os << "entry:\n";
    os << "  %p_out_res = alloca i64, align 8\n";
    os << "  %p_out_com = alloca i64, align 8\n";
    os << "  %is_1 = icmp eq i64 %align, 1\n";
    os << "  %is_2 = icmp eq i64 %align, 2\n";
    os << "  %is_4 = icmp eq i64 %align, 4\n";
    os << "  %is_8 = icmp eq i64 %align, 8\n";
    os << "  %is_16 = icmp eq i64 %align, 16\n";
    os << "  %is_32 = icmp eq i64 %align, 32\n";
    os << "  %is_64 = icmp eq i64 %align, 64\n";
    os << "  %or_12 = or i1 %is_1, %is_2\n";
    os << "  %or_34 = or i1 %is_4, %is_8\n";
    os << "  %or_1632 = or i1 %is_16, %is_32\n";
    os << "  %or_1234 = or i1 %or_12, %or_34\n";
    os << "  %or_base = or i1 %or_1234, %or_1632\n";
    os << "  %valid_align = or i1 %or_base, %is_64\n";
    os << "  br i1 %valid_align, label %check_size, label %oom\n";
    os << "check_size:\n";
    os << "  %size_pos = icmp sgt i64 %size, 0\n";
    os << "  %size_lim = icmp ule i64 %size, 4611686018427387904\n";
    os << "  %size_valid = and i1 %size_pos, %size_lim\n";
    os << "  br i1 %size_valid, label %check_first_alloc, label %oom\n";
    os << "check_first_alloc:\n";
    os << "  %p_cur = getelementptr inbounds %__tamarin_region, ptr %region, i32 0, i32 1\n";
    os << "  %cur = load ptr, ptr %p_cur, align 8\n";
    os << "  %is_first = icmp eq ptr %cur, null\n";
    os << "  br i1 %is_first, label %lazy_first_alloc, label %check_fit\n";
    os << "lazy_first_alloc:\n";
    os << "  %p_def_cap = getelementptr inbounds %__tamarin_region, ptr %region, i32 0, i32 2\n";
    os << "  %def_cap = load i64, ptr %p_def_cap, align 8\n";
    os << "  %needed_header_and_size = add i64 64, %size\n";
    os << "  %cmp_def = icmp ugt i64 %needed_header_and_size, %def_cap\n";
    os << "  %initial_res_raw = select i1 %cmp_def, i64 %needed_header_and_size, i64 %def_cap\n";
    os << "  %gran = call i64 @__tamarin_runtime_allocation_granularity()\n";
    os << "  %gran_mask = sub i64 %gran, 1\n";
    os << "  %gran_inv = xor i64 %gran_mask, -1\n";
    os << "  %res_add = add i64 %initial_res_raw, %gran_mask\n";
    os << "  %initial_res = and i64 %res_add, %gran_inv\n";
    os << "  store i64 0, ptr %p_out_res, align 8\n";
    os << "  store i64 0, ptr %p_out_com, align 8\n";
    os << "  %is_small_init = icmp ule i64 %initial_res, %gran\n";
    os << "  br i1 %is_small_init, label %try_small_cache, label %try_chunk_cache\n";
    os << "try_small_cache:\n";
    os << "  %sc_base = call ptr @__tamarin_runtime_rcache_acquire(i64 %initial_res, ptr %p_out_res, ptr %p_out_com)\n";
    os << "  br label %check_first_cached\n";
    os << "try_chunk_cache:\n";
    os << "  %cc_base = call ptr @__tamarin_runtime_chunk_cache_acquire(i64 %initial_res, ptr %p_out_res, ptr %p_out_com)\n";
    os << "  br label %check_first_cached\n";
    os << "check_first_cached:\n";
    os << "  %cached_base = phi ptr [ %sc_base, %try_small_cache ], [ %cc_base, %try_chunk_cache ]\n";
    os << "  %has_cache = icmp ne ptr %cached_base, null\n";
    os << "  br i1 %has_cache, label %cache_hit, label %do_sys_reserve\n";
    os << "cache_hit:\n";
    os << "  %c_res = load i64, ptr %p_out_res, align 8\n";
    os << "  %c_com = load i64, ptr %p_out_com, align 8\n";
    os << "  br label %prepare_first_commit\n";
    os << "do_sys_reserve:\n";
    os << "  %first_base_sys = call ptr @__tamarin_page_reserve(i64 %initial_res)\n";
    os << "  %p_r_rc_init = getelementptr inbounds %__tamarin_region, ptr %region, i32 0, i32 14\n";
    os << "  %cur_rc_init = load i64, ptr %p_r_rc_init, align 8\n";
    os << "  %next_rc_init = add i64 %cur_rc_init, 1\n";
    os << "  store i64 %next_rc_init, ptr %p_r_rc_init, align 8\n";
    os << "  br label %prepare_first_commit\n";
    os << "prepare_first_commit:\n";
    os << "  %first_base = phi ptr [ %cached_base, %cache_hit ], [ %first_base_sys, %do_sys_reserve ]\n";
    os << "  %eff_res = phi i64 [ %c_res, %cache_hit ], [ %initial_res, %do_sys_reserve ]\n";
    os << "  %already_com = phi i64 [ %c_com, %cache_hit ], [ 0, %do_sys_reserve ]\n";
    os << "  %fb_null = icmp eq ptr %first_base, null\n";
    os << "  br i1 %fb_null, label %oom, label %calc_first_commit\n";
    os << "calc_first_commit:\n";
    os << "  %page_sz = call i64 @__tamarin_runtime_page_size()\n";
    os << "  %page_mask = sub i64 %page_sz, 1\n";
    os << "  %page_inv = xor i64 %page_mask, -1\n";
    os << "  %com_add = add i64 %needed_header_and_size, %page_mask\n";
    os << "  %com_round = and i64 %com_add, %page_inv\n";
    os << "  %is_large_eff = icmp ugt i64 %eff_res, 65536\n";
    os << "  %init_min = select i1 %is_large_eff, i64 65536, i64 %com_round\n";
    os << "  %cmp_cand = icmp ugt i64 %init_min, %com_round\n";
    os << "  %com_cand = select i1 %cmp_cand, i64 %init_min, i64 %com_round\n";
    os << "  %cmp_com_res = icmp ugt i64 %com_cand, %eff_res\n";
    os << "  %target_commit = select i1 %cmp_com_res, i64 %eff_res, i64 %com_cand\n";
    os << "  %has_enough_com = icmp uge i64 %already_com, %target_commit\n";
    os << "  br i1 %has_enough_com, label %first_alloc_setup_cached, label %do_commit_first\n";
    os << "first_alloc_setup_cached:\n";
    os << "  br label %first_alloc_setup\n";
    os << "do_commit_first:\n";
    os << "  %missing_size = sub i64 %target_commit, %already_com\n";
    os << "  %com_res = call ptr @__tamarin_page_commit(ptr %first_base, i64 %already_com, i64 %missing_size)\n";
    os << "  %com_null = icmp eq ptr %com_res, null\n";
    os << "  br i1 %com_null, label %first_alloc_cleanup, label %first_alloc_setup_committed\n";
    os << "first_alloc_setup_committed:\n";
    os << "  br label %first_alloc_setup\n";
    os << "first_alloc_cleanup:\n";
    os << "  call i32 @__tamarin_page_release(ptr %first_base)\n";
    os << "  br label %oom\n";
    os << "first_alloc_setup:\n";
    os << "  %final_commit = phi i64 [ %already_com, %first_alloc_setup_cached ], [ %target_commit, %first_alloc_setup_committed ]\n";
    os << "  %cmc_inc = phi i64 [ 0, %first_alloc_setup_cached ], [ 1, %first_alloc_setup_committed ]\n";
    os << "  store ptr null, ptr %first_base, align 8\n";
    os << "  %p_fc_res = getelementptr inbounds %__tamarin_region_chunk, ptr %first_base, i32 0, i32 1\n";
    os << "  store i64 %eff_res, ptr %p_fc_res, align 8\n";
    os << "  %p_fc_com = getelementptr inbounds %__tamarin_region_chunk, ptr %first_base, i32 0, i32 2\n";
    os << "  store i64 %final_commit, ptr %p_fc_com, align 8\n";
    os << "  %first_used = add i64 64, %size\n";
    os << "  %p_fc_usd = getelementptr inbounds %__tamarin_region_chunk, ptr %first_base, i32 0, i32 3\n";
    os << "  store i64 %first_used, ptr %p_fc_usd, align 8\n";
    os << "  store ptr %first_base, ptr %region, align 8\n";
    os << "  store ptr %first_base, ptr %p_cur, align 8\n";
    os << "  %p_r_res = getelementptr inbounds %__tamarin_region, ptr %region, i32 0, i32 3\n";
    os << "  store i64 %eff_res, ptr %p_r_res, align 8\n";
    os << "  %p_r_com = getelementptr inbounds %__tamarin_region, ptr %region, i32 0, i32 4\n";
    os << "  store i64 %final_commit, ptr %p_r_com, align 8\n";
    os << "  %p_r_usd = getelementptr inbounds %__tamarin_region, ptr %region, i32 0, i32 5\n";
    os << "  store i64 %size, ptr %p_r_usd, align 8\n";
    os << "  %p_r_ac = getelementptr inbounds %__tamarin_region, ptr %region, i32 0, i32 9\n";
    os << "  store i64 1, ptr %p_r_ac, align 8\n";
    os << "  %p_r_cc = getelementptr inbounds %__tamarin_region, ptr %region, i32 0, i32 10\n";
    os << "  store i64 1, ptr %p_r_cc, align 8\n";
    os << "  %p_r_cmc = getelementptr inbounds %__tamarin_region, ptr %region, i32 0, i32 11\n";
    os << "  store i64 %cmc_inc, ptr %p_r_cmc, align 8\n";
    os << "  %p_first_payload = getelementptr inbounds i8, ptr %first_base, i64 64\n";
    os << "  ret ptr %p_first_payload\n";
    os << "check_fit:\n";
    os << "  %mask = sub i64 %align, 1\n";
    os << "  %inv_mask = xor i64 %mask, -1\n";
    os << "  %p_cur_usd = getelementptr inbounds %__tamarin_region_chunk, ptr %cur, i32 0, i32 3\n";
    os << "  %cur_usd = load i64, ptr %p_cur_usd, align 8\n";
    os << "  %add_mask = add i64 %cur_usd, %mask\n";
    os << "  %aligned_usd = and i64 %add_mask, %inv_mask\n";
    os << "  %needed = add i64 %aligned_usd, %size\n";
    os << "  %p_cur_com = getelementptr inbounds %__tamarin_region_chunk, ptr %cur, i32 0, i32 2\n";
    os << "  %cur_com = load i64, ptr %p_cur_com, align 8\n";
    os << "  %fits_com = icmp ule i64 %needed, %cur_com\n";
    os << "  br i1 %fits_com, label %fast_path, label %check_res\n";
    os << "fast_path:\n";
    os << "  store i64 %needed, ptr %p_cur_usd, align 8\n";
    os << "  %f_stats_flag = load i32, ptr @__tamarin_flag_memory_stats, align 4\n";
    os << "  %has_f_stats = icmp ne i32 %f_stats_flag, 0\n";
    os << "  br i1 %has_f_stats, label %record_fast_stats, label %fast_return\n";
    os << "record_fast_stats:\n";
    os << "  %fast_pad = sub i64 %aligned_usd, %cur_usd\n";
    os << "  %p_rf_pad = getelementptr inbounds %__tamarin_region, ptr %region, i32 0, i32 13\n";
    os << "  %rf_pad = load i64, ptr %p_rf_pad, align 8\n";
    os << "  %new_rf_pad = add i64 %rf_pad, %fast_pad\n";
    os << "  store i64 %new_rf_pad, ptr %p_rf_pad, align 8\n";
    os << "  %p_rf_usd = getelementptr inbounds %__tamarin_region, ptr %region, i32 0, i32 5\n";
    os << "  %rf_usd = load i64, ptr %p_rf_usd, align 8\n";
    os << "  %new_rf_usd = add i64 %rf_usd, %size\n";
    os << "  store i64 %new_rf_usd, ptr %p_rf_usd, align 8\n";
    os << "  %p_rf_ac = getelementptr inbounds %__tamarin_region, ptr %region, i32 0, i32 9\n";
    os << "  %rf_ac = load i64, ptr %p_rf_ac, align 8\n";
    os << "  %new_rf_ac = add i64 %rf_ac, 1\n";
    os << "  store i64 %new_rf_ac, ptr %p_rf_ac, align 8\n";
    os << "  br label %fast_return\n";
    os << "fast_return:\n";
    os << "  %alloc_ptr = getelementptr inbounds i8, ptr %cur, i64 %aligned_usd\n";
    os << "  ret ptr %alloc_ptr\n";
    os << "check_res:\n";
    os << "  %p_cur_res = getelementptr inbounds %__tamarin_region_chunk, ptr %cur, i32 0, i32 1\n";
    os << "  %cur_res = load i64, ptr %p_cur_res, align 8\n";
    os << "  %fits_res = icmp ule i64 %needed, %cur_res\n";
    os << "  br i1 %fits_res, label %page_commit_path, label %chunk_growth_path\n";
    os << "page_commit_path:\n";
    os << "  %page_sz_pc = call i64 @__tamarin_runtime_page_size()\n";
    os << "  %page_mask_pc = sub i64 %page_sz_pc, 1\n";
    os << "  %page_inv_pc = xor i64 %page_mask_pc, -1\n";
    os << "  %need_page_add = add i64 %needed, %page_mask_pc\n";
    os << "  %need_page_round = and i64 %need_page_add, %page_inv_pc\n";
    os << "  %double_com = shl i64 %cur_com, 1\n";
    os << "  %cmp_double = icmp ugt i64 %double_com, %need_page_round\n";
    os << "  %cand_com = select i1 %cmp_double, i64 %double_com, i64 %need_page_round\n";
    os << "  %cmp_pr = icmp ugt i64 %cand_com, %cur_res\n";
    os << "  %target_com = select i1 %cmp_pr, i64 %cur_res, i64 %cand_com\n";
    os << "  %additional_bytes = sub i64 %target_com, %cur_com\n";
    os << "  %pc_res = call ptr @__tamarin_page_commit(ptr %cur, i64 %cur_com, i64 %additional_bytes)\n";
    os << "  %pc_null = icmp eq ptr %pc_res, null\n";
    os << "  br i1 %pc_null, label %oom, label %page_commit_done\n";
    os << "page_commit_done:\n";
    os << "  store i64 %target_com, ptr %p_cur_com, align 8\n";
    os << "  store i64 %needed, ptr %p_cur_usd, align 8\n";
    os << "  %pc_pad = sub i64 %aligned_usd, %cur_usd\n";
    os << "  %p_rc_pad = getelementptr inbounds %__tamarin_region, ptr %region, i32 0, i32 13\n";
    os << "  %rc_pad = load i64, ptr %p_rc_pad, align 8\n";
    os << "  %new_rc_pad = add i64 %rc_pad, %pc_pad\n";
    os << "  store i64 %new_rc_pad, ptr %p_rc_pad, align 8\n";
    os << "  %p_rc_com = getelementptr inbounds %__tamarin_region, ptr %region, i32 0, i32 4\n";
    os << "  %rc_com = load i64, ptr %p_rc_com, align 8\n";
    os << "  %new_rc_com = add i64 %rc_com, %additional_bytes\n";
    os << "  store i64 %new_rc_com, ptr %p_rc_com, align 8\n";
    os << "  %p_rc_usd = getelementptr inbounds %__tamarin_region, ptr %region, i32 0, i32 5\n";
    os << "  %rc_usd = load i64, ptr %p_rc_usd, align 8\n";
    os << "  %new_rc_usd = add i64 %rc_usd, %size\n";
    os << "  store i64 %new_rc_usd, ptr %p_rc_usd, align 8\n";
    os << "  %p_rc_ac = getelementptr inbounds %__tamarin_region, ptr %region, i32 0, i32 9\n";
    os << "  %rc_ac = load i64, ptr %p_rc_ac, align 8\n";
    os << "  %new_rc_ac = add i64 %rc_ac, 1\n";
    os << "  store i64 %new_rc_ac, ptr %p_rc_ac, align 8\n";
    os << "  %p_rc_cmc = getelementptr inbounds %__tamarin_region, ptr %region, i32 0, i32 11\n";
    os << "  %rc_cmc = load i64, ptr %p_rc_cmc, align 8\n";
    os << "  %new_rc_cmc = add i64 %rc_cmc, 1\n";
    os << "  store i64 %new_rc_cmc, ptr %p_rc_cmc, align 8\n";
    os << "  %pc_alloc_ptr = getelementptr inbounds i8, ptr %cur, i64 %aligned_usd\n";
    os << "  ret ptr %pc_alloc_ptr\n";
    os << "chunk_growth_path:\n";
    os << "  %doubled = shl i64 %cur_res, 1\n";
    os << "  %needed_new_raw = add i64 64, %size\n";
    os << "  %cmp_grow = icmp ugt i64 %needed_new_raw, %doubled\n";
    os << "  %grow_cand = select i1 %cmp_grow, i64 %needed_new_raw, i64 %doubled\n";
    os << "  %gran_cg = call i64 @__tamarin_runtime_allocation_granularity()\n";
    os << "  %gran_mask_cg = sub i64 %gran_cg, 1\n";
    os << "  %gran_inv_cg = xor i64 %gran_mask_cg, -1\n";
    os << "  %grow_add = add i64 %grow_cand, %gran_mask_cg\n";
    os << "  %new_chunk_res = and i64 %grow_add, %gran_inv_cg\n";
    os << "  store i64 0, ptr %p_out_res, align 8\n";
    os << "  store i64 0, ptr %p_out_com, align 8\n";
    os << "  %cached_cg_base = call ptr @__tamarin_runtime_chunk_cache_acquire(i64 %new_chunk_res, ptr %p_out_res, ptr %p_out_com)\n";
    os << "  %has_cg_cache = icmp ne ptr %cached_cg_base, null\n";
    os << "  br i1 %has_cg_cache, label %cg_cache_hit, label %cg_do_sys_reserve\n";
    os << "cg_cache_hit:\n";
    os << "  %cg_c_res = load i64, ptr %p_out_res, align 8\n";
    os << "  %cg_c_com = load i64, ptr %p_out_com, align 8\n";
    os << "  br label %cg_prepare_commit\n";
    os << "cg_do_sys_reserve:\n";
    os << "  %new_chunk_base_sys = call ptr @__tamarin_page_reserve(i64 %new_chunk_res)\n";
    os << "  %p_r_rc_cg = getelementptr inbounds %__tamarin_region, ptr %region, i32 0, i32 14\n";
    os << "  %cur_rc_cg = load i64, ptr %p_r_rc_cg, align 8\n";
    os << "  %next_rc_cg = add i64 %cur_rc_cg, 1\n";
    os << "  store i64 %next_rc_cg, ptr %p_r_rc_cg, align 8\n";
    os << "  br label %cg_prepare_commit\n";
    os << "cg_prepare_commit:\n";
    os << "  %new_chunk_base = phi ptr [ %cached_cg_base, %cg_cache_hit ], [ %new_chunk_base_sys, %cg_do_sys_reserve ]\n";
    os << "  %eff_cg_res = phi i64 [ %cg_c_res, %cg_cache_hit ], [ %new_chunk_res, %cg_do_sys_reserve ]\n";
    os << "  %already_cg_com = phi i64 [ %cg_c_com, %cg_cache_hit ], [ 0, %cg_do_sys_reserve ]\n";
    os << "  %ncb_null = icmp eq ptr %new_chunk_base, null\n";
    os << "  br i1 %ncb_null, label %oom, label %commit_new_chunk\n";
    os << "commit_new_chunk:\n";
    os << "  %page_sz_cg = call i64 @__tamarin_runtime_page_size()\n";
    os << "  %page_mask_cg = sub i64 %page_sz_cg, 1\n";
    os << "  %page_inv_cg = xor i64 %page_mask_cg, -1\n";
    os << "  %nc_com_add = add i64 %needed_new_raw, %page_mask_cg\n";
    os << "  %nc_com_round = and i64 %nc_com_add, %page_inv_cg\n";
    os << "  %cmp_nc_com = icmp ugt i64 %nc_com_round, %eff_cg_res\n";
    os << "  %target_cg_commit = select i1 %cmp_nc_com, i64 %eff_cg_res, i64 %nc_com_round\n";
    os << "  %has_enough_cg_com = icmp uge i64 %already_cg_com, %target_cg_commit\n";
    os << "  br i1 %has_enough_cg_com, label %new_chunk_setup_cached, label %do_commit_cg\n";
    os << "new_chunk_setup_cached:\n";
    os << "  br label %new_chunk_setup\n";
    os << "do_commit_cg:\n";
    os << "  %missing_cg_size = sub i64 %target_cg_commit, %already_cg_com\n";
    os << "  %nc_com_res = call ptr @__tamarin_page_commit(ptr %new_chunk_base, i64 %already_cg_com, i64 %missing_cg_size)\n";
    os << "  %ncc_null = icmp eq ptr %nc_com_res, null\n";
    os << "  br i1 %ncc_null, label %new_chunk_cleanup, label %new_chunk_setup_committed\n";
    os << "new_chunk_setup_committed:\n";
    os << "  br label %new_chunk_setup\n";
    os << "new_chunk_cleanup:\n";
    os << "  call i32 @__tamarin_page_release(ptr %new_chunk_base)\n";
    os << "  br label %oom\n";
    os << "new_chunk_setup:\n";
    os << "  %final_chunk_commit = phi i64 [ %already_cg_com, %new_chunk_setup_cached ], [ %target_cg_commit, %new_chunk_setup_committed ]\n";
    os << "  %cg_cmc_inc = phi i64 [ 0, %new_chunk_setup_cached ], [ 1, %new_chunk_setup_committed ]\n";
    os << "  store ptr null, ptr %new_chunk_base, align 8\n";
    os << "  %p_nc_res = getelementptr inbounds %__tamarin_region_chunk, ptr %new_chunk_base, i32 0, i32 1\n";
    os << "  store i64 %eff_cg_res, ptr %p_nc_res, align 8\n";
    os << "  %p_nc_com = getelementptr inbounds %__tamarin_region_chunk, ptr %new_chunk_base, i32 0, i32 2\n";
    os << "  store i64 %final_chunk_commit, ptr %p_nc_com, align 8\n";
    os << "  %nc_used = add i64 64, %size\n";
    os << "  %p_nc_usd = getelementptr inbounds %__tamarin_region_chunk, ptr %new_chunk_base, i32 0, i32 3\n";
    os << "  store i64 %nc_used, ptr %p_nc_usd, align 8\n";
    os << "  store ptr %new_chunk_base, ptr %cur, align 8\n";
    os << "  store ptr %new_chunk_base, ptr %p_cur, align 8\n";
    os << "  %p_rg_res = getelementptr inbounds %__tamarin_region, ptr %region, i32 0, i32 3\n";
    os << "  %rg_res = load i64, ptr %p_rg_res, align 8\n";
    os << "  %new_rg_res = add i64 %rg_res, %eff_cg_res\n";
    os << "  store i64 %new_rg_res, ptr %p_rg_res, align 8\n";
    os << "  %p_rg_com = getelementptr inbounds %__tamarin_region, ptr %region, i32 0, i32 4\n";
    os << "  %rg_com = load i64, ptr %p_rg_com, align 8\n";
    os << "  %new_rg_com = add i64 %rg_com, %final_chunk_commit\n";
    os << "  store i64 %new_rg_com, ptr %p_rg_com, align 8\n";
    os << "  %p_rg_usd = getelementptr inbounds %__tamarin_region, ptr %region, i32 0, i32 5\n";
    os << "  %rg_usd = load i64, ptr %p_rg_usd, align 8\n";
    os << "  %new_rg_usd = add i64 %rg_usd, %size\n";
    os << "  store i64 %new_rg_usd, ptr %p_rg_usd, align 8\n";
    os << "  %p_rg_ac = getelementptr inbounds %__tamarin_region, ptr %region, i32 0, i32 9\n";
    os << "  %rg_ac = load i64, ptr %p_rg_ac, align 8\n";
    os << "  %new_rg_ac = add i64 %rg_ac, 1\n";
    os << "  store i64 %new_rg_ac, ptr %p_rg_ac, align 8\n";
    os << "  %p_rg_cc = getelementptr inbounds %__tamarin_region, ptr %region, i32 0, i32 10\n";
    os << "  %rg_cc = load i64, ptr %p_rg_cc, align 8\n";
    os << "  %new_rg_cc = add i64 %rg_cc, 1\n";
    os << "  store i64 %new_rg_cc, ptr %p_rg_cc, align 8\n";
    os << "  %p_rg_cmc = getelementptr inbounds %__tamarin_region, ptr %region, i32 0, i32 11\n";
    os << "  %rg_cmc = load i64, ptr %p_rg_cmc, align 8\n";
    os << "  %new_rg_cmc = add i64 %rg_cmc, %cg_cmc_inc\n";
    os << "  store i64 %new_rg_cmc, ptr %p_rg_cmc, align 8\n";
    os << "  %new_alloc_ptr = getelementptr inbounds i8, ptr %new_chunk_base, i64 64\n";
    os << "  ret ptr %new_alloc_ptr\n";
    os << "oom:\n";
    os << "  call void @__tamarin_region_oom_error()\n";
    os << "  unreachable\n";
    os << "}\n\n";

    os << "define private ptr @__tamarin_region_reserve_dtor(ptr %region) {\n";
    os << "entry:\n";
    os << "  %p_sc_cur = getelementptr inbounds %__tamarin_region, ptr %region, i32 0, i32 7\n";
    os << "  %sc_cur = load ptr, ptr %p_sc_cur, align 8\n";
    os << "  %sc_null = icmp eq ptr %sc_cur, null\n";
    os << "  br i1 %sc_null, label %alloc_first_sc, label %check_sc_full\n";
    os << "alloc_first_sc:\n";
    os << "  %sc_cached = call ptr @__tamarin_runtime_sidecar_cache_acquire()\n";
    os << "  %has_sc_cached = icmp ne ptr %sc_cached, null\n";
    os << "  br i1 %has_sc_cached, label %setup_first_sc, label %sc_sys_malloc\n";
    os << "sc_sys_malloc:\n";
    os << "  %sc_sys = call ptr @malloc(i64 184)\n";
    os << "  %fsc_null = icmp eq ptr %sc_sys, null\n";
    os << "  br i1 %fsc_null, label %oom, label %setup_first_sc\n";
    os << "setup_first_sc:\n";
    os << "  %first_sc = phi ptr [ %sc_cached, %alloc_first_sc ], [ %sc_sys, %sc_sys_malloc ]\n";
    os << "  store ptr null, ptr %first_sc, align 8\n";
    os << "  %p_fsc_cap = getelementptr inbounds %__tamarin_sidecar_chunk, ptr %first_sc, i32 0, i32 1\n";
    os << "  store i64 4, ptr %p_fsc_cap, align 8\n";
    os << "  %p_fsc_used = getelementptr inbounds %__tamarin_sidecar_chunk, ptr %first_sc, i32 0, i32 2\n";
    os << "  store i64 0, ptr %p_fsc_used, align 8\n";
    os << "  %p_sc_first = getelementptr inbounds %__tamarin_region, ptr %region, i32 0, i32 6\n";
    os << "  store ptr %first_sc, ptr %p_sc_first, align 8\n";
    os << "  store ptr %first_sc, ptr %p_sc_cur, align 8\n";
    os << "  br label %take_slot\n";
    os << "check_sc_full:\n";
    os << "  %p_used = getelementptr inbounds %__tamarin_sidecar_chunk, ptr %sc_cur, i32 0, i32 2\n";
    os << "  %used = load i64, ptr %p_used, align 8\n";
    os << "  %p_cap = getelementptr inbounds %__tamarin_sidecar_chunk, ptr %sc_cur, i32 0, i32 1\n";
    os << "  %cap = load i64, ptr %p_cap, align 8\n";
    os << "  %is_full = icmp uge i64 %used, %cap\n";
    os << "  br i1 %is_full, label %alloc_next_sc, label %take_slot\n";
    os << "alloc_next_sc:\n";
    os << "  %doubled = shl i64 %cap, 1\n";
    os << "  %cmp_lim = icmp ugt i64 %doubled, 128\n";
    os << "  %next_cap = select i1 %cmp_lim, i64 128, i64 %doubled\n";
    os << "  %rec_bytes = mul i64 %next_cap, 40\n";
    os << "  %alloc_bytes = add i64 24, %rec_bytes\n";
    os << "  %next_sc = call ptr @malloc(i64 %alloc_bytes)\n";
    os << "  %nsc_null = icmp eq ptr %next_sc, null\n";
    os << "  br i1 %nsc_null, label %oom, label %setup_next_sc\n";
    os << "setup_next_sc:\n";
    os << "  store ptr null, ptr %next_sc, align 8\n";
    os << "  %p_nsc_cap = getelementptr inbounds %__tamarin_sidecar_chunk, ptr %next_sc, i32 0, i32 1\n";
    os << "  store i64 %next_cap, ptr %p_nsc_cap, align 8\n";
    os << "  %p_nsc_used = getelementptr inbounds %__tamarin_sidecar_chunk, ptr %next_sc, i32 0, i32 2\n";
    os << "  store i64 0, ptr %p_nsc_used, align 8\n";
    os << "  store ptr %next_sc, ptr %sc_cur, align 8\n";
    os << "  store ptr %next_sc, ptr %p_sc_cur, align 8\n";
    os << "  br label %take_slot\n";
    os << "take_slot:\n";
    os << "  %active_sc = phi ptr [ %first_sc, %setup_first_sc ], [ %sc_cur, %check_sc_full ], [ %next_sc, %setup_next_sc ]\n";
    os << "  %p_act_used = getelementptr inbounds %__tamarin_sidecar_chunk, ptr %active_sc, i32 0, i32 2\n";
    os << "  %slot_idx = load i64, ptr %p_act_used, align 8\n";
    os << "  %next_act_used = add i64 %slot_idx, 1\n";
    os << "  store i64 %next_act_used, ptr %p_act_used, align 8\n";
    os << "  %rec_offset = mul i64 %slot_idx, 40\n";
    os << "  %rec_raw = add i64 24, %rec_offset\n";
    os << "  %slot_ptr = getelementptr inbounds i8, ptr %active_sc, i64 %rec_raw\n";
    os << "  store ptr null, ptr %slot_ptr, align 8\n";
    os << "  %p_obj = getelementptr inbounds %__tamarin_dtor_record, ptr %slot_ptr, i32 0, i32 1\n";
    os << "  store ptr null, ptr %p_obj, align 8\n";
    os << "  %p_fn = getelementptr inbounds %__tamarin_dtor_record, ptr %slot_ptr, i32 0, i32 2\n";
    os << "  store ptr null, ptr %p_fn, align 8\n";
    os << "  %p_cnt = getelementptr inbounds %__tamarin_dtor_record, ptr %slot_ptr, i32 0, i32 3\n";
    os << "  store i64 0, ptr %p_cnt, align 8\n";
    os << "  %p_str = getelementptr inbounds %__tamarin_dtor_record, ptr %slot_ptr, i32 0, i32 4\n";
    os << "  store i64 0, ptr %p_str, align 8\n";
    os << "  ret ptr %slot_ptr\n";
    os << "oom:\n";
    os << "  call void @__tamarin_region_oom_error()\n";
    os << "  unreachable\n";
    os << "}\n\n";

    os << "define private void @__tamarin_region_activate_dtor(ptr %region, ptr %slot, ptr %obj, ptr %dtor_fn, i64 %count, i64 %stride) {\n";
    os << "entry:\n";
    os << "  %slot_null = icmp eq ptr %slot, null\n";
    os << "  %cnt_zero = icmp eq i64 %count, 0\n";
    os << "  %skip = or i1 %slot_null, %cnt_zero\n";
    os << "  br i1 %skip, label %done, label %do_activate\n";
    os << "do_activate:\n";
    os << "  %p_obj = getelementptr inbounds %__tamarin_dtor_record, ptr %slot, i32 0, i32 1\n";
    os << "  store ptr %obj, ptr %p_obj, align 8\n";
    os << "  %p_fn = getelementptr inbounds %__tamarin_dtor_record, ptr %slot, i32 0, i32 2\n";
    os << "  store ptr %dtor_fn, ptr %p_fn, align 8\n";
    os << "  %p_cnt = getelementptr inbounds %__tamarin_dtor_record, ptr %slot, i32 0, i32 3\n";
    os << "  store i64 %count, ptr %p_cnt, align 8\n";
    os << "  %p_str = getelementptr inbounds %__tamarin_dtor_record, ptr %slot, i32 0, i32 4\n";
    os << "  store i64 %stride, ptr %p_str, align 8\n";
    os << "  %p_dtor_head = getelementptr inbounds %__tamarin_region, ptr %region, i32 0, i32 8\n";
    os << "  %head = load ptr, ptr %p_dtor_head, align 8\n";
    os << "  store ptr %head, ptr %slot, align 8\n";
    os << "  store ptr %slot, ptr %p_dtor_head, align 8\n";
    os << "  %p_r_dc = getelementptr inbounds %__tamarin_region, ptr %region, i32 0, i32 12\n";
    os << "  %r_dc = load i64, ptr %p_r_dc, align 8\n";
    os << "  %new_r_dc = add i64 %r_dc, 1\n";
    os << "  store i64 %new_r_dc, ptr %p_r_dc, align 8\n";
    os << "  br label %done\n";
    os << "done:\n";
    os << "  ret void\n";
    os << "}\n\n";

    os << "define private void @__tamarin_region_register_dtor(ptr %region, ptr %obj, ptr %dtor_fn, i64 %count, i64 %stride) {\n";
    os << "entry:\n";
    os << "  %slot = call ptr @__tamarin_region_reserve_dtor(ptr %region)\n";
    os << "  call void @__tamarin_region_activate_dtor(ptr %region, ptr %slot, ptr %obj, ptr %dtor_fn, i64 %count, i64 %stride)\n";
    os << "  ret void\n";
    os << "}\n\n";

    os << "define private void @__tamarin_region_destroy(ptr %region) {\n";
    os << "entry:\n";
    os << "  %p_dtor = getelementptr inbounds %__tamarin_region, ptr %region, i32 0, i32 8\n";
    os << "  %dtor_head = load ptr, ptr %p_dtor, align 8\n";
    os << "  br label %dtor_check\n";
    os << "dtor_check:\n";
    os << "  %cur_rec = phi ptr [ %dtor_head, %entry ], [ %next_rec, %dtor_next_step ]\n";
    os << "  %rec_null = icmp eq ptr %cur_rec, null\n";
    os << "  br i1 %rec_null, label %calc_sc_init, label %dtor_exec\n";
    os << "dtor_exec:\n";
    os << "  %p_obj = getelementptr inbounds %__tamarin_dtor_record, ptr %cur_rec, i32 0, i32 1\n";
    os << "  %obj = load ptr, ptr %p_obj, align 8\n";
    os << "  %p_fn = getelementptr inbounds %__tamarin_dtor_record, ptr %cur_rec, i32 0, i32 2\n";
    os << "  %fn = load ptr, ptr %p_fn, align 8\n";
    os << "  %p_cnt = getelementptr inbounds %__tamarin_dtor_record, ptr %cur_rec, i32 0, i32 3\n";
    os << "  %cnt = load i64, ptr %p_cnt, align 8\n";
    os << "  %p_str = getelementptr inbounds %__tamarin_dtor_record, ptr %cur_rec, i32 0, i32 4\n";
    os << "  %str = load i64, ptr %p_str, align 8\n";
    os << "  %fn_null = icmp eq ptr %fn, null\n";
    os << "  %cnt_0 = icmp eq i64 %cnt, 0\n";
    os << "  %skip_rec = or i1 %fn_null, %cnt_0\n";
    os << "  br i1 %skip_rec, label %dtor_next_step, label %dtor_dispatch\n";
    os << "dtor_dispatch:\n";
    os << "  %is_batch = icmp ugt i64 %cnt, 1\n";
    os << "  br i1 %is_batch, label %batch_loop_init, label %single_dtor\n";
    os << "batch_loop_init:\n";
    os << "  %init_idx = sub i64 %cnt, 1\n";
    os << "  br label %batch_loop\n";
    os << "batch_loop:\n";
    os << "  %loop_idx = phi i64 [ %init_idx, %batch_loop_init ], [ %prev_idx, %batch_loop_step ]\n";
    os << "  %elem_off = mul i64 %loop_idx, %str\n";
    os << "  %elem_ptr = getelementptr inbounds i8, ptr %obj, i64 %elem_off\n";
    os << "  call void %fn(ptr %elem_ptr)\n";
    os << "  %is_zero = icmp eq i64 %loop_idx, 0\n";
    os << "  br i1 %is_zero, label %dtor_next_step, label %batch_loop_step\n";
    os << "batch_loop_step:\n";
    os << "  %prev_idx = sub i64 %loop_idx, 1\n";
    os << "  br label %batch_loop\n";
    os << "single_dtor:\n";
    os << "  call void %fn(ptr %obj)\n";
    os << "  br label %dtor_next_step\n";
    os << "dtor_next_step:\n";
    os << "  %next_rec = load ptr, ptr %cur_rec, align 8\n";
    os << "  br label %dtor_check\n";
    os << "calc_sc_init:\n";
    os << "  %p_sc_first0 = getelementptr inbounds %__tamarin_region, ptr %region, i32 0, i32 6\n";
    os << "  %sc_calc_head = load ptr, ptr %p_sc_first0, align 8\n";
    os << "  br label %calc_sc_loop\n";
    os << "calc_sc_loop:\n";
    os << "  %sc_calc_cur = phi ptr [ %sc_calc_head, %calc_sc_init ], [ %sc_calc_next, %calc_sc_step ]\n";
    os << "  %sc_cap_acc = phi i64 [ 0, %calc_sc_init ], [ %sc_cap_acc_next, %calc_sc_step ]\n";
    os << "  %sc_usd_acc = phi i64 [ 0, %calc_sc_init ], [ %sc_usd_acc_next, %calc_sc_step ]\n";
    os << "  %sc_calc_null = icmp eq ptr %sc_calc_cur, null\n";
    os << "  br i1 %sc_calc_null, label %free_payload_chunks, label %calc_sc_step\n";
    os << "calc_sc_step:\n";
    os << "  %p_c_cap = getelementptr inbounds %__tamarin_sidecar_chunk, ptr %sc_calc_cur, i32 0, i32 1\n";
    os << "  %c_cap = load i64, ptr %p_c_cap, align 8\n";
    os << "  %p_c_usd = getelementptr inbounds %__tamarin_sidecar_chunk, ptr %sc_calc_cur, i32 0, i32 2\n";
    os << "  %c_usd = load i64, ptr %p_c_usd, align 8\n";
    os << "  %c_cap_rec_bytes = mul i64 %c_cap, 40\n";
    os << "  %c_cap_total_bytes = add i64 24, %c_cap_rec_bytes\n";
    os << "  %c_usd_rec_bytes = mul i64 %c_usd, 40\n";
    os << "  %c_usd_total_bytes = add i64 24, %c_usd_rec_bytes\n";
    os << "  %sc_cap_acc_next = add i64 %sc_cap_acc, %c_cap_total_bytes\n";
    os << "  %sc_usd_acc_next = add i64 %sc_usd_acc, %c_usd_total_bytes\n";
    os << "  %sc_calc_next = load ptr, ptr %sc_calc_cur, align 8\n";
    os << "  br label %calc_sc_loop\n";
    os << "free_payload_chunks:\n";
    os << "  %first_chunk = load ptr, ptr %region, align 8\n";
    os << "  %fc_null = icmp eq ptr %first_chunk, null\n";
    os << "  br i1 %fc_null, label %free_sidecar_chunks, label %check_fc_cache\n";
    os << "check_fc_cache:\n";
    os << "  %next_after_fc = load ptr, ptr %first_chunk, align 8\n";
    os << "  %p_fc_res = getelementptr inbounds %__tamarin_region_chunk, ptr %first_chunk, i32 0, i32 1\n";
    os << "  %fc_res = load i64, ptr %p_fc_res, align 8\n";
    os << "  %p_fc_com0 = getelementptr inbounds %__tamarin_region_chunk, ptr %first_chunk, i32 0, i32 2\n";
    os << "  %fc_com0 = load i64, ptr %p_fc_com0, align 8\n";
    os << "  %p_fc_usd0 = getelementptr inbounds %__tamarin_region_chunk, ptr %first_chunk, i32 0, i32 3\n";
    os << "  %fc_usd0 = load i64, ptr %p_fc_usd0, align 8\n";
    os << "  %gran_fc = call i64 @__tamarin_runtime_allocation_granularity()\n";
    os << "  %is_fc_small = icmp ule i64 %fc_res, %gran_fc\n";
    os << "  br i1 %is_fc_small, label %fc_put_small, label %fc_put_chunk\n";
    os << "fc_put_small:\n";
    os << "  call i32 @__tamarin_runtime_rcache_put(ptr %first_chunk, i64 %fc_res, i64 %fc_com0, i64 %fc_usd0)\n";
    os << "  br label %free_remaining_chunks\n";
    os << "fc_put_chunk:\n";
    os << "  call i32 @__tamarin_runtime_chunk_cache_put(ptr %first_chunk, i64 %fc_res, i64 %fc_com0, i64 %fc_usd0)\n";
    os << "  br label %free_remaining_chunks\n";
    os << "free_remaining_chunks:\n";
    os << "  br label %rem_loop_check\n";
    os << "rem_loop_check:\n";
    os << "  %rem_chunk = phi ptr [ %next_after_fc, %free_remaining_chunks ], [ %next_rem, %free_rem_step ]\n";
    os << "  %rem_null = icmp eq ptr %rem_chunk, null\n";
    os << "  br i1 %rem_null, label %free_sidecar_chunks, label %free_rem_step\n";
    os << "free_rem_step:\n";
    os << "  %next_rem = load ptr, ptr %rem_chunk, align 8\n";
    os << "  %p_rc_res = getelementptr inbounds %__tamarin_region_chunk, ptr %rem_chunk, i32 0, i32 1\n";
    os << "  %rc_res_val = load i64, ptr %p_rc_res, align 8\n";
    os << "  %p_rc_com_val = getelementptr inbounds %__tamarin_region_chunk, ptr %rem_chunk, i32 0, i32 2\n";
    os << "  %rc_com_val = load i64, ptr %p_rc_com_val, align 8\n";
    os << "  %p_rc_usd_val = getelementptr inbounds %__tamarin_region_chunk, ptr %rem_chunk, i32 0, i32 3\n";
    os << "  %rc_usd_val = load i64, ptr %p_rc_usd_val, align 8\n";
    os << "  call i32 @__tamarin_runtime_chunk_cache_put(ptr %rem_chunk, i64 %rc_res_val, i64 %rc_com_val, i64 %rc_usd_val)\n";
    os << "  br label %rem_loop_check\n";
    os << "free_sidecar_chunks:\n";
    os << "  %p_sc_first = getelementptr inbounds %__tamarin_region, ptr %region, i32 0, i32 6\n";
    os << "  %first_sc = load ptr, ptr %p_sc_first, align 8\n";
    os << "  br label %sc_loop_check\n";
    os << "sc_loop_check:\n";
    os << "  %cur_sc = phi ptr [ %first_sc, %free_sidecar_chunks ], [ %next_sc, %sc_loop_next ]\n";
    os << "  %sc_null = icmp eq ptr %cur_sc, null\n";
    os << "  br i1 %sc_null, label %check_stats, label %free_sc_step\n";
    os << "free_sc_step:\n";
    os << "  %next_sc = load ptr, ptr %cur_sc, align 8\n";
    os << "  %p_csc_cap = getelementptr inbounds %__tamarin_sidecar_chunk, ptr %cur_sc, i32 0, i32 1\n";
    os << "  %csc_cap = load i64, ptr %p_csc_cap, align 8\n";
    os << "  %is_cap4 = icmp eq i64 %csc_cap, 4\n";
    os << "  br i1 %is_cap4, label %sc_put_cache, label %sc_do_free\n";
    os << "sc_put_cache:\n";
    os << "  call void @__tamarin_runtime_sidecar_cache_put(ptr %cur_sc)\n";
    os << "  br label %sc_loop_next\n";
    os << "sc_do_free:\n";
    os << "  call void @free(ptr %cur_sc)\n";
    os << "  br label %sc_loop_next\n";
    os << "sc_loop_next:\n";
    os << "  br label %sc_loop_check\n";
    os << "check_stats:\n";
    os << "  %stats_on = load i32, ptr @__tamarin_flag_memory_stats, align 4\n";
    os << "  %is_stats = icmp ne i32 %stats_on, 0\n";
    os << "  br i1 %is_stats, label %emit_stats, label %cleanup_done\n";
    os << "emit_stats:\n";
    os << "  %p_def_cap = getelementptr inbounds %__tamarin_region, ptr %region, i32 0, i32 2\n";
    os << "  %stat_def = load i64, ptr %p_def_cap, align 8\n";
    os << "  %p_stat_res = getelementptr inbounds %__tamarin_region, ptr %region, i32 0, i32 3\n";
    os << "  %stat_res = load i64, ptr %p_stat_res, align 8\n";
    os << "  %p_stat_com = getelementptr inbounds %__tamarin_region, ptr %region, i32 0, i32 4\n";
    os << "  %stat_com = load i64, ptr %p_stat_com, align 8\n";
    os << "  %p_stat_usd = getelementptr inbounds %__tamarin_region, ptr %region, i32 0, i32 5\n";
    os << "  %stat_usd = load i64, ptr %p_stat_usd, align 8\n";
    os << "  %p_stat_ac = getelementptr inbounds %__tamarin_region, ptr %region, i32 0, i32 9\n";
    os << "  %stat_ac = load i64, ptr %p_stat_ac, align 8\n";
    os << "  %p_stat_cc = getelementptr inbounds %__tamarin_region, ptr %region, i32 0, i32 10\n";
    os << "  %stat_cc = load i64, ptr %p_stat_cc, align 8\n";
    os << "  %p_stat_cmc = getelementptr inbounds %__tamarin_region, ptr %region, i32 0, i32 11\n";
    os << "  %stat_cmc = load i64, ptr %p_stat_cmc, align 8\n";
    os << "  %p_stat_dc = getelementptr inbounds %__tamarin_region, ptr %region, i32 0, i32 12\n";
    os << "  %stat_dc = load i64, ptr %p_stat_dc, align 8\n";
    os << "  %p_stat_pad = getelementptr inbounds %__tamarin_region, ptr %region, i32 0, i32 13\n";
    os << "  %stat_pad = load i64, ptr %p_stat_pad, align 8\n";
    os << "  %p_stat_rc = getelementptr inbounds %__tamarin_region, ptr %region, i32 0, i32 14\n";
    os << "  %stat_rc = load i64, ptr %p_stat_rc, align 8\n";
    os << "  %chunk_headers = mul i64 %stat_cc, 64\n";
    os << "  %used_sum1 = add i64 %stat_usd, %stat_pad\n";
    os << "  %used_sum = add i64 %used_sum1, %chunk_headers\n";
    os << "  %has_com = icmp ugt i64 %stat_com, %used_sum\n";
    os << "  %unused_sub = sub i64 %stat_com, %used_sum\n";
    os << "  %unused_com = select i1 %has_com, i64 %unused_sub, i64 0\n";
    os << "  %stat_ws = call i64 @__tamarin_runtime_working_set_bytes()\n";
    os << "  %w1 = call i32 @__tamarin_raw_write(i32 2, ptr @__tamarin_str_region_stats_hdr, i32 41)\n";
    os << "  %buf = alloca [1024 x i8], align 1\n";
    os << "  %w_sn = call i32 (ptr, i64, ptr, ...) @snprintf(ptr %buf, i64 1024, ptr @__tamarin_str_region_stats_fmt, i64 %stat_def, i64 %stat_res, i64 %stat_com, i64 %stat_ws, i64 %stat_usd, i64 %stat_pad, i64 %unused_com, i64 %stat_cc, i64 %stat_rc, i64 %stat_cmc, i64 %sc_cap_acc, i64 %sc_usd_acc, i64 %stat_dc, i64 %stat_ac)\n";
    os << "  %w2 = call i32 @__tamarin_raw_write(i32 2, ptr %buf, i32 %w_sn)\n";
    os << "  br label %cleanup_done\n";
    os << "cleanup_done:\n";
    os << "  store ptr null, ptr %region, align 8\n";
    os << "  %p_cl_cur = getelementptr inbounds %__tamarin_region, ptr %region, i32 0, i32 1\n";
    os << "  store ptr null, ptr %p_cl_cur, align 8\n";
    os << "  %p_cl_res = getelementptr inbounds %__tamarin_region, ptr %region, i32 0, i32 3\n";
    os << "  store i64 0, ptr %p_cl_res, align 8\n";
    os << "  %p_cl_com = getelementptr inbounds %__tamarin_region, ptr %region, i32 0, i32 4\n";
    os << "  store i64 0, ptr %p_cl_com, align 8\n";
    os << "  %p_cl_usd = getelementptr inbounds %__tamarin_region, ptr %region, i32 0, i32 5\n";
    os << "  store i64 0, ptr %p_cl_usd, align 8\n";
    os << "  store ptr null, ptr %p_sc_first, align 8\n";
    os << "  %p_cl_sc2 = getelementptr inbounds %__tamarin_region, ptr %region, i32 0, i32 7\n";
    os << "  store ptr null, ptr %p_cl_sc2, align 8\n";
    os << "  store ptr null, ptr %p_dtor, align 8\n";
    os << "  %p_cl_pad = getelementptr inbounds %__tamarin_region, ptr %region, i32 0, i32 13\n";
    os << "  store i64 0, ptr %p_cl_pad, align 8\n";
    os << "  %p_cl_rc = getelementptr inbounds %__tamarin_region, ptr %region, i32 0, i32 14\n";
    os << "  store i64 0, ptr %p_cl_rc, align 8\n";
    os << "  ret void\n";
    os << "}\n\n";
}

void LLVMCodeGen::emitFunction(const ir::IRFunction& fn, std::ostream& os) {
    valueTypes_.clear();
    intConstants_.clear();
    floatConstants_.clear();
    boolConstants_.clear();
    isPointerValue_.clear();

    if (fn.isExtern || fn.blocks.empty()) {
        os << "declare " << llvmType(fn.returnType) << " @" << fn.name << "(";
        for (size_t i = 0; i < fn.params.size(); ++i) {
            if (i > 0) os << ", ";
            auto pTy = fn.params[i].type;
            if (pTy && pTy->isReference()) {
                os << "ptr";
            } else if (fn.linkage == "C" && isStructPassedByPointerInCABI(pTy)) {
                os << "ptr";
            } else if (fn.linkage == "C" && !getCABISmallStructIntType(pTy).empty()) {
                os << getCABISmallStructIntType(pTy);
            } else {
                os << llvmType(pTy);
            }
        }
        os << ")\n\n";
        return;
    }

    std::string linkagePrefix;
    if (fn.isExported && targetTriple_.isWindows()) {
        linkagePrefix = "dllexport ";
    } else if (fn.isLinkOnceODR) {
        linkagePrefix = "linkonce_odr ";
    }

    os << "define " << linkagePrefix << llvmType(fn.returnType) << " @" << fn.name << "(";

    for (size_t i = 0; i < fn.params.size(); ++i) {
        if (i > 0) os << ", ";
        auto pTy = fn.params[i].type;
        auto pVal = fn.params[i].valueId;
        valueTypes_[pVal] = pTy;
        if (pTy && pTy->isReference()) {
            isPointerValue_.insert(pVal);
        }
        os << llvmType(pTy) << " %v" << pVal;
    }
    std::string fnAttrs = "mustprogress nounwind";
    if (fn.isAlwaysInline) fnAttrs += " alwaysinline";
    if (fn.isNoInline) fnAttrs += " noinline";
    os << ") " << fnAttrs << " {\n";

    // Pre-pass: collect types and pointer values for all instructions
    for (auto& block : fn.blocks) {
        for (auto& inst : block->instructions) {
            if (inst.result != ir::InvalidValue && inst.type) {
                valueTypes_[inst.result] = inst.type;
            }
            if (inst.opcode == ir::Opcode::Alloca ||
                inst.opcode == ir::Opcode::ArrayAlloc ||
                inst.opcode == ir::Opcode::GetArrayElemPtr ||
                inst.opcode == ir::Opcode::GetFieldPtr ||
                inst.opcode == ir::Opcode::GetVTablePtr ||
                inst.opcode == ir::Opcode::SoAArrayAlloc ||
                inst.opcode == ir::Opcode::SoAGetFieldPtr ||
                inst.opcode == ir::Opcode::RegionAlloc ||
                inst.opcode == ir::Opcode::RegionReserveDestructor ||
                inst.opcode == ir::Opcode::VariantPayloadPtr ||
                inst.opcode == ir::Opcode::SlabGet ||
                inst.opcode == ir::Opcode::TaskScopeBegin ||
                inst.opcode == ir::Opcode::AsyncTaskCreate ||
                inst.opcode == ir::Opcode::AsyncTaskAwait ||
                ((inst.opcode == ir::Opcode::Load || inst.opcode == ir::Opcode::ArrayLoad || inst.opcode == ir::Opcode::Cast) && inst.type && inst.type->isReference())) {
                if (inst.result != ir::InvalidValue) {
                    isPointerValue_.insert(inst.result);
                }
            }
        }
    }

    bool isFirstBlock = true;
    for (auto& block : fn.blocks) {
        os << block->label << ":\n";
        if (isFirstBlock) {
            isFirstBlock = false;
            // Hoist all stack allocations to entry block to prevent loop stack growth
            for (auto& b : fn.blocks) {
                for (auto& inst : b->instructions) {
                    if (inst.opcode == ir::Opcode::Alloca) {
                        std::string tyStr = (inst.strImm == "__tamarin_region") ? "%__tamarin_region" : llvmType(inst.type);
                        os << "  %v" << inst.result << " = alloca " << tyStr << "\n";
                    } else if (inst.opcode == ir::Opcode::ArrayAlloc) {
                        auto elemTy = inst.type && inst.type->elementType ? inst.type->elementType : nullptr;
                        std::string elemStr = elemTy ? llvmType(elemTy) : "i32";
                        int64_t count = inst.intImm >= 0 ? inst.intImm : 0;
                        os << "  %v" << inst.result << " = alloca [" << count << " x " << elemStr << "], align 64\n";
                    } else if (inst.opcode == ir::Opcode::SoAArrayAlloc) {
                        auto elemTy = inst.type && inst.type->elementType ? inst.type->elementType : nullptr;
                        int64_t count = inst.intImm >= 0 ? inst.intImm : 0;
                        std::string bundleTy = "{ ";
                        if (elemTy && elemTy->isRecord()) {
                            for (size_t i = 0; i < elemTy->structFields.size(); ++i) {
                                if (i > 0) bundleTy += ", ";
                                bundleTy += "[" + std::to_string(count) + " x " + llvmType(elemTy->structFields[i].type) + "]";
                            }
                        }
                        bundleTy += " }";
                        os << "  %v" << inst.result << " = alloca " << bundleTy << ", align 64\n";
                    }
                }
            }
        }
        for (auto& inst : block->instructions) {
            emitInstruction(inst, os);
        }
    }

    os << "}\n\n";
}

void LLVMCodeGen::emitInstruction(const ir::Instruction& inst, std::ostream& os) {
    if (inst.result != ir::InvalidValue && inst.type) {
        valueTypes_[inst.result] = inst.type;
    }

    if (inst.opcode == ir::Opcode::Alloca ||
        inst.opcode == ir::Opcode::ArrayAlloc ||
        inst.opcode == ir::Opcode::GetArrayElemPtr ||
        inst.opcode == ir::Opcode::GetFieldPtr ||
        inst.opcode == ir::Opcode::GetVTablePtr ||
        inst.opcode == ir::Opcode::SoAArrayAlloc ||
        inst.opcode == ir::Opcode::SoAGetFieldPtr ||
        inst.opcode == ir::Opcode::RegionAlloc ||
        inst.opcode == ir::Opcode::RegionReserveDestructor ||
        inst.opcode == ir::Opcode::SlabGet ||
        inst.opcode == ir::Opcode::TaskScopeBegin ||
        inst.opcode == ir::Opcode::AsyncTaskCreate ||
        inst.opcode == ir::Opcode::AsyncTaskAwait ||
        ((inst.opcode == ir::Opcode::Load || inst.opcode == ir::Opcode::ArrayLoad || inst.opcode == ir::Opcode::Cast) && inst.type && inst.type->isReference())) {
        if (inst.result != ir::InvalidValue) {
            isPointerValue_.insert(inst.result);
        }
    }

    switch (inst.opcode) {
        case ir::Opcode::Nop:
            break;

        case ir::Opcode::IConst:
            intConstants_[inst.result] = inst.intImm;
            break;

        case ir::Opcode::FConst:
            floatConstants_[inst.result] = inst.floatImm;
            break;

        case ir::Opcode::BoolConst:
            boolConstants_[inst.result] = (inst.intImm != 0);
            break;

        case ir::Opcode::StringConst: {
            int64_t idx = inst.intImm;
            int64_t len = inst.op1;
            int64_t totalBytes = len + 1;
            os << "  %gep_str_" << inst.result << " = getelementptr inbounds ["
               << totalBytes << " x i8], ptr @.str." << idx << ", i64 0, i64 0\n";
            os << "  %view0_" << inst.result << " = insertvalue { ptr, i64 } undef, ptr %gep_str_"
               << inst.result << ", 0\n";
            os << "  %v" << inst.result << " = insertvalue { ptr, i64 } %view0_"
               << inst.result << ", i64 " << len << ", 1\n";
            break;
        }

        case ir::Opcode::Add:
            os << "  %v" << inst.result << " = add " << llvmType(inst.type)
               << " " << formatOperand(inst.op1, inst.type)
               << ", " << formatOperand(inst.op2, inst.type) << "\n";
            break;

        case ir::Opcode::Sub:
            os << "  %v" << inst.result << " = sub " << llvmType(inst.type)
               << " " << formatOperand(inst.op1, inst.type)
               << ", " << formatOperand(inst.op2, inst.type) << "\n";
            break;

        case ir::Opcode::Mul:
            os << "  %v" << inst.result << " = mul " << llvmType(inst.type)
               << " " << formatOperand(inst.op1, inst.type)
               << ", " << formatOperand(inst.op2, inst.type) << "\n";
            break;

        case ir::Opcode::SDiv:
            os << "  %v" << inst.result << " = sdiv " << llvmType(inst.type)
               << " " << formatOperand(inst.op1, inst.type)
               << ", " << formatOperand(inst.op2, inst.type) << "\n";
            break;

        case ir::Opcode::UDiv:
            os << "  %v" << inst.result << " = udiv " << llvmType(inst.type)
               << " " << formatOperand(inst.op1, inst.type)
               << ", " << formatOperand(inst.op2, inst.type) << "\n";
            break;

        case ir::Opcode::SRem:
            os << "  %v" << inst.result << " = srem " << llvmType(inst.type)
               << " " << formatOperand(inst.op1, inst.type)
               << ", " << formatOperand(inst.op2, inst.type) << "\n";
            break;

        case ir::Opcode::URem:
            os << "  %v" << inst.result << " = urem " << llvmType(inst.type)
               << " " << formatOperand(inst.op1, inst.type)
               << ", " << formatOperand(inst.op2, inst.type) << "\n";
            break;

        case ir::Opcode::FAdd:
            os << "  %v" << inst.result << " = fadd " << (fastMath_ ? "fast " : "") << llvmType(inst.type)
               << " " << formatOperand(inst.op1, inst.type)
               << ", " << formatOperand(inst.op2, inst.type) << "\n";
            break;

        case ir::Opcode::FSub:
            os << "  %v" << inst.result << " = fsub " << (fastMath_ ? "fast " : "") << llvmType(inst.type)
               << " " << formatOperand(inst.op1, inst.type)
               << ", " << formatOperand(inst.op2, inst.type) << "\n";
            break;

        case ir::Opcode::FMul:
            os << "  %v" << inst.result << " = fmul " << (fastMath_ ? "fast " : "") << llvmType(inst.type)
               << " " << formatOperand(inst.op1, inst.type)
               << ", " << formatOperand(inst.op2, inst.type) << "\n";
            break;

        case ir::Opcode::FDiv:
            os << "  %v" << inst.result << " = fdiv " << (fastMath_ ? "fast " : "") << llvmType(inst.type)
               << " " << formatOperand(inst.op1, inst.type)
               << ", " << formatOperand(inst.op2, inst.type) << "\n";
            break;

        case ir::Opcode::And:
            os << "  %v" << inst.result << " = and " << llvmType(inst.type)
               << " " << formatOperand(inst.op1, inst.type)
               << ", " << formatOperand(inst.op2, inst.type) << "\n";
            break;

        case ir::Opcode::Or:
            os << "  %v" << inst.result << " = or " << llvmType(inst.type)
               << " " << formatOperand(inst.op1, inst.type)
               << ", " << formatOperand(inst.op2, inst.type) << "\n";
            break;

        case ir::Opcode::Xor:
            os << "  %v" << inst.result << " = xor " << llvmType(inst.type)
               << " " << formatOperand(inst.op1, inst.type)
               << ", " << formatOperand(inst.op2, inst.type) << "\n";
            break;

        case ir::Opcode::Shl:
            os << "  %v" << inst.result << " = shl " << llvmType(inst.type)
               << " " << formatOperand(inst.op1, inst.type)
               << ", " << formatOperand(inst.op2, inst.type) << "\n";
            break;

        case ir::Opcode::AShr:
            os << "  %v" << inst.result << " = ashr " << llvmType(inst.type)
               << " " << formatOperand(inst.op1, inst.type)
               << ", " << formatOperand(inst.op2, inst.type) << "\n";
            break;

        case ir::Opcode::LShr:
            os << "  %v" << inst.result << " = lshr " << llvmType(inst.type)
               << " " << formatOperand(inst.op1, inst.type)
               << ", " << formatOperand(inst.op2, inst.type) << "\n";
            break;

        case ir::Opcode::ICmpSLt: {
            auto cmpTy = getValueType(inst.op1);
            std::string tyStr = cmpTy ? llvmType(cmpTy) : "i32";
            os << "  %v" << inst.result << " = icmp slt " << tyStr << " "
               << formatOperand(inst.op1, cmpTy) << ", " << formatOperand(inst.op2, cmpTy) << "\n";
            break;
        }
        case ir::Opcode::ICmpSLe: {
            auto cmpTy = getValueType(inst.op1);
            std::string tyStr = cmpTy ? llvmType(cmpTy) : "i32";
            os << "  %v" << inst.result << " = icmp sle " << tyStr << " "
               << formatOperand(inst.op1, cmpTy) << ", " << formatOperand(inst.op2, cmpTy) << "\n";
            break;
        }
        case ir::Opcode::ICmpSGt: {
            auto cmpTy = getValueType(inst.op1);
            std::string tyStr = cmpTy ? llvmType(cmpTy) : "i32";
            os << "  %v" << inst.result << " = icmp sgt " << tyStr << " "
               << formatOperand(inst.op1, cmpTy) << ", " << formatOperand(inst.op2, cmpTy) << "\n";
            break;
        }
        case ir::Opcode::ICmpSGe: {
            auto cmpTy = getValueType(inst.op1);
            std::string tyStr = cmpTy ? llvmType(cmpTy) : "i32";
            os << "  %v" << inst.result << " = icmp sge " << tyStr << " "
               << formatOperand(inst.op1, cmpTy) << ", " << formatOperand(inst.op2, cmpTy) << "\n";
            break;
        }
        case ir::Opcode::ICmpULt: {
            auto cmpTy = getValueType(inst.op1);
            std::string tyStr = cmpTy ? llvmType(cmpTy) : "i32";
            os << "  %v" << inst.result << " = icmp ult " << tyStr << " "
               << formatOperand(inst.op1, cmpTy) << ", " << formatOperand(inst.op2, cmpTy) << "\n";
            break;
        }
        case ir::Opcode::ICmpULe: {
            auto cmpTy = getValueType(inst.op1);
            std::string tyStr = cmpTy ? llvmType(cmpTy) : "i32";
            os << "  %v" << inst.result << " = icmp ule " << tyStr << " "
               << formatOperand(inst.op1, cmpTy) << ", " << formatOperand(inst.op2, cmpTy) << "\n";
            break;
        }
        case ir::Opcode::ICmpUGt: {
            auto cmpTy = getValueType(inst.op1);
            std::string tyStr = cmpTy ? llvmType(cmpTy) : "i32";
            os << "  %v" << inst.result << " = icmp ugt " << tyStr << " "
               << formatOperand(inst.op1, cmpTy) << ", " << formatOperand(inst.op2, cmpTy) << "\n";
            break;
        }
        case ir::Opcode::ICmpUGe: {
            auto cmpTy = getValueType(inst.op1);
            std::string tyStr = cmpTy ? llvmType(cmpTy) : "i32";
            os << "  %v" << inst.result << " = icmp uge " << tyStr << " "
               << formatOperand(inst.op1, cmpTy) << ", " << formatOperand(inst.op2, cmpTy) << "\n";
            break;
        }
        case ir::Opcode::ICmpEq: {
            auto cmpTy = getValueType(inst.op1);
            std::string tyStr = cmpTy ? llvmType(cmpTy) : "i32";
            os << "  %v" << inst.result << " = icmp eq " << tyStr << " "
               << formatOperand(inst.op1, cmpTy) << ", " << formatOperand(inst.op2, cmpTy) << "\n";
            break;
        }
        case ir::Opcode::ICmpNe: {
            auto cmpTy = getValueType(inst.op1);
            std::string tyStr = cmpTy ? llvmType(cmpTy) : "i32";
            os << "  %v" << inst.result << " = icmp ne " << tyStr << " "
               << formatOperand(inst.op1, cmpTy) << ", " << formatOperand(inst.op2, cmpTy) << "\n";
            break;
        }

        case ir::Opcode::FCmpEq: {
            auto cmpTy = getValueType(inst.op1);
            std::string tyStr = cmpTy ? llvmType(cmpTy) : "double";
            os << "  %v" << inst.result << " = fcmp oeq " << tyStr << " "
               << formatOperand(inst.op1, cmpTy) << ", " << formatOperand(inst.op2, cmpTy) << "\n";
            break;
        }
        case ir::Opcode::FCmpNe: {
            auto cmpTy = getValueType(inst.op1);
            std::string tyStr = cmpTy ? llvmType(cmpTy) : "double";
            os << "  %v" << inst.result << " = fcmp one " << tyStr << " "
               << formatOperand(inst.op1, cmpTy) << ", " << formatOperand(inst.op2, cmpTy) << "\n";
            break;
        }
        case ir::Opcode::FCmpLt: {
            auto cmpTy = getValueType(inst.op1);
            std::string tyStr = cmpTy ? llvmType(cmpTy) : "double";
            os << "  %v" << inst.result << " = fcmp olt " << tyStr << " "
               << formatOperand(inst.op1, cmpTy) << ", " << formatOperand(inst.op2, cmpTy) << "\n";
            break;
        }
        case ir::Opcode::FCmpLe: {
            auto cmpTy = getValueType(inst.op1);
            std::string tyStr = cmpTy ? llvmType(cmpTy) : "double";
            os << "  %v" << inst.result << " = fcmp ole " << tyStr << " "
               << formatOperand(inst.op1, cmpTy) << ", " << formatOperand(inst.op2, cmpTy) << "\n";
            break;
        }
        case ir::Opcode::FCmpGt: {
            auto cmpTy = getValueType(inst.op1);
            std::string tyStr = cmpTy ? llvmType(cmpTy) : "double";
            os << "  %v" << inst.result << " = fcmp ogt " << tyStr << " "
               << formatOperand(inst.op1, cmpTy) << ", " << formatOperand(inst.op2, cmpTy) << "\n";
            break;
        }
        case ir::Opcode::FCmpGe: {
            auto cmpTy = getValueType(inst.op1);
            std::string tyStr = cmpTy ? llvmType(cmpTy) : "double";
            os << "  %v" << inst.result << " = fcmp oge " << tyStr << " "
               << formatOperand(inst.op1, cmpTy) << ", " << formatOperand(inst.op2, cmpTy) << "\n";
            break;
        }

        case ir::Opcode::Alloca: {
            std::string tyStr = llvmType(inst.type);
            os << "  store " << tyStr << " " << zeroValue(inst.type) << ", ptr %v" << inst.result << "\n";
            break;
        }

        case ir::Opcode::Load: {
            auto loadedTy = inst.type ? llvmType(inst.type) : "i32";
            os << "  %v" << inst.result << " = load " << loadedTy
               << ", ptr %v" << inst.op1 << "\n";
            break;
        }

        case ir::Opcode::Store: {
            auto valTy = getValueType(inst.op2);
            std::string tyStr = "i32";
            if (isPointerValue_.count(inst.op2) || (valTy && (valTy->isReference() || valTy->isArray()))) {
                tyStr = "ptr";
            } else if (valTy) {
                tyStr = llvmType(valTy);
            }
            os << "  store " << tyStr << " " << formatOperand(inst.op2, valTy)
               << ", ptr %v" << inst.op1 << "\n";
            break;
        }

        case ir::Opcode::GetFieldPtr: {
            auto baseTy = getValueType(inst.op1);
            if (baseTy && baseTy->isReference()) baseTy = baseTy->elementType;
            if (baseTy && baseTy->isTuple()) {
                os << "  %v" << inst.result << " = getelementptr inbounds " << llvmType(baseTy)
                   << ", ptr %v" << inst.op1 << ", i32 0, i32 " << inst.intImm << "\n";
            } else if (!inst.strImm.empty() && inst.strImm.front() == '{') {
                os << "  %v" << inst.result << " = getelementptr inbounds " << inst.strImm
                   << ", ptr %v" << inst.op1 << ", i32 0, i32 " << inst.intImm << "\n";
            } else {
                std::string sName = inst.strImm.empty() ? "struct" : inst.strImm;
                os << "  %v" << inst.result << " = getelementptr inbounds %struct." << sName
                   << ", ptr %v" << inst.op1 << ", i32 0, i32 " << inst.intImm << "\n";
            }
            break;
        }

        case ir::Opcode::TagGet: {
            const sema::Type* eTy = (inst.type && inst.type->isEnum()) ? inst.type : getValueType(inst.op1);
            if (eTy && eTy->isNicheOptimized) {
                os << "  %v" << inst.result << "_cmp = icmp eq i64 " << formatOperand(inst.op1) << ", 0\n";
                os << "  %v" << inst.result << " = select i1 %v" << inst.result << "_cmp, i32 1, i32 0\n";
            } else {
                os << "  %v" << inst.result << " = load i32, ptr %v" << inst.op1 << "\n";
            }
            break;
        }

        case ir::Opcode::TagSet: {
            const sema::Type* eTy = (inst.type && inst.type->isEnum()) ? inst.type : getValueType(inst.op1);
            if (eTy && eTy->isNicheOptimized) {
                if (inst.intImm == 1) {
                    os << "  store i64 0, ptr %v" << inst.op1 << "\n";
                }
            } else {
                os << "  store i32 " << inst.intImm << ", ptr %v" << inst.op1 << "\n";
            }
            break;
        }

        case ir::Opcode::VariantPayloadPtr: {
            const sema::Type* eTy = (inst.type && inst.type->isEnum()) ? inst.type : getValueType(inst.op1);
            if (eTy && eTy->isNicheOptimized) {
                os << "  %v" << inst.result << " = getelementptr i8, ptr %v" << inst.op1 << ", i64 0\n";
            } else {
                size_t align = eTy ? eTy->maxPayloadAlignment : 4;
                size_t payloadOffset = std::max((size_t)4, align);
                size_t totalOffset = payloadOffset + inst.intImm2;
                os << "  %v" << inst.result << " = getelementptr inbounds i8, ptr %v" << inst.op1
                   << ", i64 " << totalOffset << "\n";
            }
            break;
        }

        case ir::Opcode::Jump: {
            os << "  br label %" << inst.label1;
            LoopMetaKey key{inst.unrollCount, inst.vectorizeWidth, inst.interleaveCount, inst.vectorizeEnable};
            if (key.hasHints()) {
                auto it = loopMetadataMap_.find(key);
                if (it == loopMetadataMap_.end()) {
                    int32_t nextId = 100 + static_cast<int32_t>(loopMetadataMap_.size());
                    it = loopMetadataMap_.emplace(key, nextId).first;
                }
                os << ", !llvm.loop !" << it->second;
            }
            os << "\n";
            break;
        }

        case ir::Opcode::Branch: {
            os << "  br i1 " << formatOperand(inst.op1) << ", label %" << inst.label1
               << ", label %" << inst.label2;
            LoopMetaKey key{inst.unrollCount, inst.vectorizeWidth, inst.interleaveCount, inst.vectorizeEnable};
            if (key.hasHints()) {
                auto it = loopMetadataMap_.find(key);
                if (it == loopMetadataMap_.end()) {
                    int32_t nextId = 100 + static_cast<int32_t>(loopMetadataMap_.size());
                    it = loopMetadataMap_.emplace(key, nextId).first;
                }
                os << ", !llvm.loop !" << it->second;
            }
            os << "\n";
            break;
        }

        case ir::Opcode::Switch: {
            auto valTy = getValueType(inst.op1);
            std::string tyStr = (valTy && valTy->isInteger()) ? llvmType(valTy) : "i32";
            os << "  switch " << tyStr << " " << formatOperand(inst.op1, valTy)
               << ", label %" << inst.label1 << " [\n";
            for (const auto& [caseVal, caseLabel] : inst.switchCases) {
                os << "    " << tyStr << " " << caseVal << ", label %" << caseLabel << "\n";
            }
            os << "  ]\n";
            break;
        }

        case ir::Opcode::Return:
            if (inst.op1 != ir::InvalidValue) {
                auto retTy = getValueType(inst.op1);
                std::string tyStr = retTy ? llvmType(retTy) : "i32";
                os << "  ret " << tyStr << " " << formatOperand(inst.op1, retTy) << "\n";
            } else {
                os << "  ret void\n";
            }
            break;

        case ir::Opcode::Call: {
            if (inst.op1 != ir::InvalidValue) {
                uint32_t cId = inst.result != ir::InvalidValue ? inst.result : (callOpId_++);
                os << "  %callee_fn_" << cId << " = extractvalue { ptr, ptr } %v" << inst.op1 << ", 0\n";
                os << "  %callee_env_" << cId << " = extractvalue { ptr, ptr } %v" << inst.op1 << ", 1\n";
                os << "  ";
                if (inst.result != ir::InvalidValue) {
                    os << "%v" << inst.result << " = ";
                }
                os << "call " << llvmType(inst.type) << " %callee_fn_" << cId << "(ptr %callee_env_" << cId;
                for (size_t i = 0; i < inst.callArgs.size(); ++i) {
                    os << ", ";
                    auto argTy = getValueType(inst.callArgs[i]);
                    std::string tyStr = "i32";
                    if (isPointerValue_.count(inst.callArgs[i]) || (argTy && argTy->isReference())) {
                        tyStr = "ptr";
                    } else if (argTy) {
                        tyStr = llvmType(argTy);
                    }
                    os << tyStr << " " << formatOperand(inst.callArgs[i], argTy);
                }
                os << ")\n";
            } else {
                const ir::IRFunction* targetFn = nullptr;
                auto it = functionMap_.find(inst.strImm);
                if (it != functionMap_.end()) {
                    targetFn = it->second;
                }
                bool isCABI = targetFn && (targetFn->isExtern || targetFn->blocks.empty()) && targetFn->linkage == "C";

                std::vector<std::string> argTokens;
                for (size_t i = 0; i < inst.callArgs.size(); ++i) {
                    const sema::Type* expectedParamTy = (targetFn && i < targetFn->params.size()) ? targetFn->params[i].type : nullptr;
                    auto argTy = getValueType(inst.callArgs[i]);
                    if (!argTy && expectedParamTy) argTy = expectedParamTy;

                    const sema::Type* effTy = expectedParamTy ? expectedParamTy : argTy;
                    if (isCABI && isStructPassedByPointerInCABI(effTy)) {
                        if (isPointerValue_.count(inst.callArgs[i])) {
                            argTokens.push_back("ptr %v" + std::to_string(inst.callArgs[i]));
                        } else {
                            uint32_t tmpId = callOpId_++;
                            std::string stType = llvmType(argTy ? argTy : effTy);
                            os << "  %cabi_arg_" << tmpId << " = alloca " << stType << ", align 8\n";
                            os << "  store " << stType << " " << formatOperand(inst.callArgs[i], argTy) << ", ptr %cabi_arg_" << tmpId << ", align 8\n";
                            argTokens.push_back("ptr %cabi_arg_" + std::to_string(tmpId));
                        }
                    } else if (isCABI && !getCABISmallStructIntType(effTy).empty()) {
                        std::string intTy = getCABISmallStructIntType(effTy);
                        uint32_t tmpId = callOpId_++;
                        std::string stType = llvmType(argTy ? argTy : effTy);
                        if (isPointerValue_.count(inst.callArgs[i])) {
                            os << "  %cabi_cast_" << tmpId << " = load " << intTy << ", ptr %v" << inst.callArgs[i] << ", align 1\n";
                        } else {
                            os << "  %cabi_arg_" << tmpId << " = alloca " << stType << ", align 8\n";
                            os << "  store " << stType << " " << formatOperand(inst.callArgs[i], argTy) << ", ptr %cabi_arg_" << tmpId << ", align 8\n";
                            os << "  %cabi_cast_" << tmpId << " = load " << intTy << ", ptr %cabi_arg_" << tmpId << ", align 1\n";
                        }
                        argTokens.push_back(intTy + " %cabi_cast_" + std::to_string(tmpId));
                    } else {
                        std::string tyStr = "i32";
                        if (isPointerValue_.count(inst.callArgs[i]) || (argTy && argTy->isReference())) {
                            tyStr = "ptr";
                        } else if (argTy) {
                            tyStr = llvmType(argTy);
                        }
                        argTokens.push_back(tyStr + " " + formatOperand(inst.callArgs[i], argTy));
                    }
                }

                os << "  ";
                if (inst.result != ir::InvalidValue) {
                    os << "%v" << inst.result << " = ";
                }
                os << "call " << llvmType(inst.type) << " @" << inst.strImm << "(";
                for (size_t i = 0; i < argTokens.size(); ++i) {
                    if (i > 0) os << ", ";
                    os << argTokens[i];
                }
                os << ")\n";
            }
            break;
        }

        case ir::Opcode::FunctionPtr: {
            os << "  %raw_fn_" << inst.result << " = getelementptr i8, ptr @" << inst.strImm << ", i64 0\n";
            os << "  %f0_" << inst.result << " = insertvalue { ptr, ptr } undef, ptr %raw_fn_" << inst.result << ", 0\n";
            if (inst.op1 != ir::InvalidValue) {
                os << "  %v" << inst.result << " = insertvalue { ptr, ptr } %f0_" << inst.result << ", ptr %v" << inst.op1 << ", 1\n";
            } else {
                os << "  %v" << inst.result << " = insertvalue { ptr, ptr } %f0_" << inst.result << ", ptr null, 1\n";
            }
            break;
        }

        case ir::Opcode::GlobalPtr: {
            os << "  %v" << inst.result << " = getelementptr i8, ptr @" << inst.strImm << ", i64 0\n";
            break;
        }

        case ir::Opcode::ArrayAlloc: {
            auto elemTy = inst.type && inst.type->elementType ? inst.type->elementType : nullptr;
            int64_t count = inst.intImm >= 0 ? inst.intImm : 0;
            if (count > 0) {
                int64_t byteSize = count * (elemTy ? elemTy->size() : 4);
                os << "  call void @llvm.memset.p0.i64(ptr %v" << inst.result
                   << ", i8 0, i64 " << byteSize << ", i1 false)\n";
            }
            break;
        }

        case ir::Opcode::GetArrayElemPtr: {
            auto elemTy = inst.type;
            std::string elemStr = elemTy ? llvmType(elemTy) : "i32";
            auto arrTy = getValueType(inst.op1);
            auto idxTy = getValueType(inst.op2);
            std::string idxTyStr = idxTy ? llvmType(idxTy) : "i64";

            if (arrTy && arrTy->isArrayView()) {
                os << "  %arr_ptr_gep_" << inst.result << " = extractvalue { ptr, i64 } %v" << inst.op1 << ", 0\n";
                os << "  %v" << inst.result << " = getelementptr inbounds " << elemStr << ", ptr %arr_ptr_gep_"
                   << inst.result << ", " << idxTyStr << " " << formatOperand(inst.op2, idxTy) << "\n";
            } else {
                os << "  %v" << inst.result << " = getelementptr inbounds " << elemStr << ", ptr %v"
                   << inst.op1 << ", " << idxTyStr << " " << formatOperand(inst.op2, idxTy) << "\n";
            }
            break;
        }

        case ir::Opcode::ArrayLoad: {
            auto elemTy = inst.type;
            std::string elemStr = elemTy ? llvmType(elemTy) : "i32";
            auto arrTy = getValueType(inst.op1);
            auto idxTy = getValueType(inst.op2);
            std::string idxTyStr = idxTy ? llvmType(idxTy) : "i64";

            if (arrTy && arrTy->isArrayView()) {
                os << "  %arr_ptr_" << inst.result << " = extractvalue { ptr, i64 } %v" << inst.op1 << ", 0\n";
                os << "  %gep_" << inst.result << " = getelementptr inbounds " << elemStr << ", ptr %arr_ptr_"
                   << inst.result << ", " << idxTyStr << " " << formatOperand(inst.op2, idxTy) << "\n";
            } else {
                os << "  %gep_" << inst.result << " = getelementptr inbounds " << elemStr << ", ptr %v"
                   << inst.op1 << ", " << idxTyStr << " " << formatOperand(inst.op2, idxTy) << "\n";
            }
            os << "  %v" << inst.result << " = load " << elemStr << ", ptr %gep_" << inst.result << "\n";
            break;
        }

        case ir::Opcode::ArrayStore: {
            uint32_t gepId = gepStoreId_++;
            auto arrTy = getValueType(inst.op1);
            auto valTy = getValueType(inst.op3);
            auto elemTy = (arrTy && arrTy->elementType) ? arrTy->elementType : (valTy ? valTy : nullptr);
            std::string elemStr = elemTy ? llvmType(elemTy) : "i32";
            std::string valTyStr = "i32";
            if (isPointerValue_.count(inst.op3) || (elemTy && elemTy->isReference()) || (valTy && valTy->isReference())) {
                valTyStr = "ptr";
            } else if (valTy) {
                valTyStr = llvmType(valTy);
            } else {
                valTyStr = elemStr;
            }
            auto idxTy = getValueType(inst.op2);
            std::string idxTyStr = idxTy ? llvmType(idxTy) : "i64";

            if (arrTy && arrTy->isArrayView()) {
                os << "  %arr_ptr_st_" << gepId << " = extractvalue { ptr, i64 } %v" << inst.op1 << ", 0\n";
                os << "  %gep_store_" << gepId << " = getelementptr inbounds " << elemStr << ", ptr %arr_ptr_st_"
                   << gepId << ", " << idxTyStr << " " << formatOperand(inst.op2, idxTy) << "\n";
            } else {
                os << "  %gep_store_" << gepId << " = getelementptr inbounds " << elemStr << ", ptr %v"
                   << inst.op1 << ", " << idxTyStr << " " << formatOperand(inst.op2, idxTy) << "\n";
            }
            os << "  store " << valTyStr << " " << formatOperand(inst.op3, valTy)
               << ", ptr %gep_store_" << gepId << "\n";
            break;
        }

        case ir::Opcode::BoundsCheck: {
            auto arrTy = getValueType(inst.op2);
            if (arrTy && !arrTy->isArrayView() && arrTy->arraySize < 0) {
                break;
            }
            uint32_t chkId = boundsCheckId_++;
            auto idxTy = getValueType(inst.op1);
            bool isSigned = !idxTy || idxTy->isSignedInteger();
            std::string idxTyStr = idxTy ? llvmType(idxTy) : "i32";
            std::string idxStr = formatOperand(inst.op1, idxTy);

            os << "  ; Array bounds validation\n";
            if (arrTy && arrTy->isArrayView()) {
                os << "  %chk_len64_" << chkId << " = extractvalue { ptr, i64 } %v" << inst.op2 << ", 1\n";
                std::string idx64 = idxStr;
                if (idxTyStr != "i64") {
                    if (isSigned) {
                        os << "  %chk_idx64_" << chkId << " = sext " << idxTyStr << " " << idxStr << " to i64\n";
                    } else {
                        os << "  %chk_idx64_" << chkId << " = zext " << idxTyStr << " " << idxStr << " to i64\n";
                    }
                    idx64 = "%chk_idx64_" + std::to_string(chkId);
                }
                os << "  %chk_fail_" << chkId << " = icmp uge i64 " << idx64 << ", %chk_len64_" << chkId << "\n";
            } else {
                int64_t fixedLen = (arrTy && arrTy->arraySize >= 0) ? arrTy->arraySize : 0;
                if (idxTyStr == "i32" && fixedLen <= INT32_MAX) {
                    os << "  %chk_fail_" << chkId << " = icmp uge i32 " << idxStr << ", " << fixedLen << "\n";
                } else if (idxTyStr == "i64") {
                    os << "  %chk_fail_" << chkId << " = icmp uge i64 " << idxStr << ", " << fixedLen << "\n";
                } else {
                    std::string idx64 = idxStr;
                    if (isSigned) {
                        os << "  %chk_idx64_" << chkId << " = sext " << idxTyStr << " " << idxStr << " to i64\n";
                    } else {
                        os << "  %chk_idx64_" << chkId << " = zext " << idxTyStr << " " << idxStr << " to i64\n";
                    }
                    idx64 = "%chk_idx64_" + std::to_string(chkId);
                    os << "  %chk_fail_" << chkId << " = icmp uge i64 " << idx64 << ", " << fixedLen << "\n";
                }
            }

            os << "  br i1 %chk_fail_" << chkId << ", label %bchk_fail_" << chkId << ", label %bchk_ok_" << chkId << ", !prof !1\n";
            os << "bchk_fail_" << chkId << ":\n";
            os << "  call void @__tamarin_bounds_error()\n";
            os << "  unreachable\n";
            os << "bchk_ok_" << chkId << ":\n";
            break;
        }

        case ir::Opcode::GetLen: {
            auto arrTy = getValueType(inst.op1);
            if (arrTy && arrTy->isArrayView()) {
                os << "  %v" << inst.result << " = extractvalue { ptr, i64 } %v" << inst.op1 << ", 1\n";
            } else {
                int64_t len = (arrTy && arrTy->arraySize >= 0) ? arrTy->arraySize : 0;
                intConstants_[inst.result] = len;
            }
            break;
        }

        case ir::Opcode::MakeView: {
            os << "  %view0_" << inst.result << " = insertvalue { ptr, i64 } undef, ptr %v" << inst.op1 << ", 0\n";
            auto it = intConstants_.find(inst.op2);
            if (it != intConstants_.end()) {
                os << "  %v" << inst.result << " = insertvalue { ptr, i64 } %view0_" << inst.result
                   << ", i64 " << it->second << ", 1\n";
            } else {
                auto lenTy = getValueType(inst.op2);
                std::string lenTyStr = lenTy ? llvmType(lenTy) : "i64";
                if (lenTyStr == "i64") {
                    os << "  %v" << inst.result << " = insertvalue { ptr, i64 } %view0_" << inst.result
                       << ", i64 %v" << inst.op2 << ", 1\n";
                } else {
                    os << "  %len64_mk_" << inst.result << " = zext " << lenTyStr << " %v" << inst.op2 << " to i64\n";
                    os << "  %v" << inst.result << " = insertvalue { ptr, i64 } %view0_" << inst.result
                       << ", i64 %len64_mk_" << inst.result << ", 1\n";
                }
            }
            break;
        }

        case ir::Opcode::Cast: {
            auto fromTy = getValueType(inst.op1);
            auto toTy = inst.type;
            std::string fromTyStr = fromTy ? llvmType(fromTy) : "i32";
            std::string toTyStr = toTy ? llvmType(toTy) : "i32";
            std::string opStr = formatOperand(inst.op1, fromTy);

            bool isFromPtr = isPointerValue_.count(inst.op1) || (fromTy && fromTy->isReference());
            bool isToPtr = (toTy && toTy->isReference());
            if (isFromPtr) fromTyStr = "ptr";
            if (isToPtr) toTyStr = "ptr";

            if (isFromPtr && isToPtr) {
                os << "  %v" << inst.result << " = bitcast ptr " << opStr << " to ptr\n";
            } else if (isFromPtr && toTy && (toTy->isInteger() || toTy->isHandle())) {
                os << "  %v" << inst.result << " = ptrtoint ptr " << opStr << " to " << toTyStr << "\n";
            } else if (fromTy && (fromTy->isInteger() || fromTy->isHandle()) && isToPtr) {
                os << "  %v" << inst.result << " = inttoptr " << fromTyStr << " " << opStr << " to ptr\n";
            } else if (fromTyStr == toTyStr) {
                if (fromTy && fromTy->isArrayView()) {
                    os << "  %v_ptrtmp_" << inst.result << " = extractvalue { ptr, i64 } " << opStr << ", 0\n";
                    os << "  %v_lentmp_" << inst.result << " = extractvalue { ptr, i64 } " << opStr << ", 1\n";
                    os << "  %v_bld_" << inst.result << " = insertvalue { ptr, i64 } undef, ptr %v_ptrtmp_" << inst.result << ", 0\n";
                    os << "  %v" << inst.result << " = insertvalue { ptr, i64 } %v_bld_" << inst.result << ", i64 %v_lentmp_" << inst.result << ", 1\n";
                } else if (fromTy && fromTy->isFloat()) {
                    os << "  %v" << inst.result << " = fadd " << fromTyStr << " " << opStr << ", 0.0\n";
                } else if (fromTy && fromTy->isBool()) {
                    os << "  %v" << inst.result << " = xor i1 " << opStr << ", 0\n";
                } else {
                    os << "  %v" << inst.result << " = add " << fromTyStr << " " << opStr << ", 0\n";
                }
            } else if (fromTy && fromTy->isBool() && toTy && (toTy->isInteger() || toTy->isHandle())) {
                os << "  %v" << inst.result << " = zext i1 " << opStr << " to " << toTyStr << "\n";
            } else if (fromTy && (fromTy->isInteger() || fromTy->isHandle()) && toTy && toTy->isBool()) {
                os << "  %v" << inst.result << " = icmp ne " << fromTyStr << " " << opStr << ", 0\n";
            } else if (fromTy && fromTy->isFloat() && toTy && toTy->isBool()) {
                os << "  %v" << inst.result << " = fcmp one " << fromTyStr << " " << opStr << ", 0.0\n";
            } else if (fromTy && (fromTy->isInteger() || fromTy->isHandle()) && toTy && (toTy->isInteger() || toTy->isHandle())) {
                if (toTy->size() > fromTy->size()) {
                    if (fromTy->isSignedInteger()) {
                        os << "  %v" << inst.result << " = sext " << fromTyStr << " " << opStr << " to " << toTyStr << "\n";
                    } else {
                        os << "  %v" << inst.result << " = zext " << fromTyStr << " " << opStr << " to " << toTyStr << "\n";
                    }
                } else {
                    os << "  %v" << inst.result << " = trunc " << fromTyStr << " " << opStr << " to " << toTyStr << "\n";
                }
            } else if (fromTy && fromTy->isInteger() && toTy && toTy->isFloat()) {
                if (fromTy->isSignedInteger()) {
                    os << "  %v" << inst.result << " = sitofp " << fromTyStr << " " << opStr << " to " << toTyStr << "\n";
                } else {
                    os << "  %v" << inst.result << " = uitofp " << fromTyStr << " " << opStr << " to " << toTyStr << "\n";
                }
            } else if (fromTy && fromTy->isFloat() && toTy && toTy->isInteger()) {
                if (toTy->isSignedInteger()) {
                    os << "  %v" << inst.result << " = fptosi " << fromTyStr << " " << opStr << " to " << toTyStr << "\n";
                } else {
                    os << "  %v" << inst.result << " = fptoui " << fromTyStr << " " << opStr << " to " << toTyStr << "\n";
                }
            } else if (fromTy && fromTy->isFloat() && toTy && toTy->isFloat()) {
                if (toTy->size() > fromTy->size()) {
                    os << "  %v" << inst.result << " = fpext " << fromTyStr << " " << opStr << " to " << toTyStr << "\n";
                } else {
                    os << "  %v" << inst.result << " = fptrunc " << fromTyStr << " " << opStr << " to " << toTyStr << "\n";
                }
            } else if ((fromTyStr == "i8" || fromTyStr == "i16" || fromTyStr == "i32" || fromTyStr == "i64") &&
                       (toTyStr == "i8" || toTyStr == "i16" || toTyStr == "i32" || toTyStr == "i64")) {
                int fromBits = std::stoi(fromTyStr.substr(1));
                int toBits = std::stoi(toTyStr.substr(1));
                if (toBits > fromBits) {
                    if (fromTy && fromTy->isSignedInteger()) {
                        os << "  %v" << inst.result << " = sext " << fromTyStr << " " << opStr << " to " << toTyStr << "\n";
                    } else {
                        os << "  %v" << inst.result << " = zext " << fromTyStr << " " << opStr << " to " << toTyStr << "\n";
                    }
                } else if (toBits < fromBits) {
                    os << "  %v" << inst.result << " = trunc " << fromTyStr << " " << opStr << " to " << toTyStr << "\n";
                } else {
                    os << "  %v" << inst.result << " = add " << fromTyStr << " " << opStr << ", 0\n";
                }
            } else {
                os << "  %v" << inst.result << " = bitcast " << fromTyStr << " " << opStr << " to " << toTyStr << "\n";
            }
            break;
        }

        case ir::Opcode::Phi: {
            std::string resTy = inst.type ? llvmType(inst.type) : "i32";
            if (isPointerValue_.count(inst.op1) || (inst.type && inst.type->isReference())) {
                resTy = "ptr";
            }
            os << "  %v" << inst.result << " = phi " << resTy << " [ " << formatOperand(inst.op1, inst.type)
               << ", %" << inst.label1 << " ]";
            if (inst.op2 != ir::InvalidValue) {
                os << ", [ " << formatOperand(inst.op2, inst.type) << ", %" << inst.label2 << " ]";
            }
            os << "\n";
            break;
        }

        case ir::Opcode::GetVTablePtr: {
            os << "  %v" << inst.result << " = getelementptr inbounds ptr, ptr @_vtable_" << inst.strImm << ", i32 0\n";
            break;
        }

        case ir::Opcode::CallVirtual: {
            uint32_t cvId = inst.result != ir::InvalidValue ? inst.result : boundsCheckId_++;
            os << "  %vptr_" << cvId << " = load ptr, ptr %v" << inst.op1 << ", align 8, !invariant.load !0\n";
            os << "  %slot_ptr_" << cvId << " = getelementptr inbounds ptr, ptr %vptr_" << cvId << ", i32 " << inst.intImm << "\n";
            os << "  %fn_ptr_" << cvId << " = load ptr, ptr %slot_ptr_" << cvId << ", align 8, !invariant.load !0\n";
            os << "  ";
            if (inst.result != ir::InvalidValue) {
                os << "%v" << inst.result << " = ";
            }
            os << "call " << llvmType(inst.type) << " %fn_ptr_" << cvId << "(";
            for (size_t i = 0; i < inst.callArgs.size(); ++i) {
                if (i > 0) os << ", ";
                auto argTy = getValueType(inst.callArgs[i]);
                std::string tyStr = "i32";
                if (isPointerValue_.count(inst.callArgs[i]) || (argTy && argTy->isReference())) {
                    tyStr = "ptr";
                } else if (argTy) {
                    tyStr = llvmType(argTy);
                }
                os << tyStr << " " << formatOperand(inst.callArgs[i], argTy);
            }
            os << ")\n";
            break;
        }

        case ir::Opcode::SoAArrayAlloc: {
            auto elemTy = inst.type && inst.type->elementType ? inst.type->elementType : nullptr;
            int64_t count = inst.intImm >= 0 ? inst.intImm : 0;
            int64_t totalBytes = 0;
            if (elemTy && elemTy->isRecord()) {
                for (size_t i = 0; i < elemTy->structFields.size(); ++i) {
                    totalBytes += count * elemTy->structFields[i].type->size();
                }
            }
            if (totalBytes > 0) {
                os << "  call void @llvm.memset.p0.i64(ptr %v" << inst.result
                   << ", i8 0, i64 " << totalBytes << ", i1 false)\n";
            }
            break;
        }

        case ir::Opcode::SoAGetFieldPtr: {
            auto arrTy = getValueType(inst.op1);
            auto elemTy = arrTy && arrTy->elementType ? arrTy->elementType : nullptr;
            int64_t count = arrTy && arrTy->arraySize >= 0 ? arrTy->arraySize : 0;

            std::string bundleTy = "{ ";
            if (elemTy && elemTy->isRecord()) {
                for (size_t i = 0; i < elemTy->structFields.size(); ++i) {
                    if (i > 0) bundleTy += ", ";
                    bundleTy += "[" + std::to_string(count) + " x " + llvmType(elemTy->structFields[i].type) + "]";
                }
            }
            bundleTy += " }";

            // Bounds check on index op2 against count
            uint32_t chkId = boundsCheckId_++;
            auto idxTy = getValueType(inst.op2);
            bool isSigned = !idxTy || idxTy->isSignedInteger();
            std::string idxTyStr = idxTy ? llvmType(idxTy) : "i32";
            std::string idxStr = formatOperand(inst.op2, idxTy);

            std::string idx64 = idxStr;
            if (isSigned) {
                if (idxTyStr == "i32" && count <= INT32_MAX) {
                    os << "  %chk_fail_" << chkId << " = icmp uge i32 " << idxStr << ", " << count << "\n";
                    os << "  %chk_idx64_" << chkId << " = zext i32 " << idxStr << " to i64\n";
                    idx64 = "%chk_idx64_" + std::to_string(chkId);
                } else {
                    if (idxTyStr != "i64") {
                        os << "  %chk_idx64_" << chkId << " = sext " << idxTyStr << " " << idxStr << " to i64\n";
                        idx64 = "%chk_idx64_" + std::to_string(chkId);
                    }
                    os << "  %chk_fail_" << chkId << " = icmp uge i64 " << idx64 << ", " << count << "\n";
                }
            } else {
                if (idxTyStr == "i32" && count <= INT32_MAX) {
                    os << "  %chk_fail_" << chkId << " = icmp uge i32 " << idxStr << ", " << count << "\n";
                    os << "  %chk_idx64_" << chkId << " = zext i32 " << idxStr << " to i64\n";
                    idx64 = "%chk_idx64_" + std::to_string(chkId);
                } else {
                    if (idxTyStr != "i64") {
                        os << "  %chk_zext_" << chkId << " = zext " << idxTyStr << " " << idxStr << " to i64\n";
                        idx64 = "%chk_zext_" + std::to_string(chkId);
                    }
                    os << "  %chk_fail_" << chkId << " = icmp uge i64 " << idx64 << ", " << count << "\n";
                }
            }

            os << "  br i1 %chk_fail_" << chkId << ", label %bchk_fail_" << chkId << ", label %bchk_ok_" << chkId << ", !prof !1\n";
            os << "bchk_fail_" << chkId << ":\n";
            os << "  call void @__tamarin_bounds_error()\n";
            os << "  unreachable\n";
            os << "bchk_ok_" << chkId << ":\n";

            os << "  %v" << inst.result << " = getelementptr inbounds " << bundleTy
               << ", ptr %v" << inst.op1 << ", i32 0, i32 " << inst.intImm << ", i64 " << idx64 << "\n";
            break;
        }

        case ir::Opcode::RegionInit: {
            os << "  call void @__tamarin_region_init(ptr %v" << inst.op1 << ", i64 " << inst.intImm << ")\n";
            break;
        }

        case ir::Opcode::RegionAlloc: {
            int64_t align = 16;
            if (!inst.strImm.empty()) {
                try { align = std::stoll(inst.strImm); } catch (...) { align = 16; }
            }
            os << "  %v" << inst.result << " = call ptr @__tamarin_region_alloc(ptr %v" << inst.op1
               << ", i64 " << inst.intImm << ", i64 " << align << ")\n";
            break;
        }

        case ir::Opcode::RegionReserveDestructor: {
            os << "  %v" << inst.result << " = call ptr @__tamarin_region_reserve_dtor(ptr %v" << inst.op1 << ")\n";
            break;
        }

        case ir::Opcode::RegionActivateDestructor: {
            os << "  call void @__tamarin_region_activate_dtor(ptr %v" << inst.op1
               << ", ptr %v" << inst.op2
               << ", ptr %v" << inst.op3
               << ", ptr @" << inst.strImm
               << ", i64 " << inst.intImm
               << ", i64 " << inst.intImm2 << ")\n";
            break;
        }

        case ir::Opcode::RegionRegisterDestructor: {
            os << "  call void @__tamarin_region_register_dtor(ptr %v" << inst.op1
               << ", ptr %v" << inst.op2
               << ", ptr @" << inst.strImm
               << ", i64 " << inst.intImm
               << ", i64 " << inst.intImm2 << ")\n";
            break;
        }

        case ir::Opcode::RegionDestroy: {
            os << "  call void @__tamarin_region_destroy(ptr %v" << inst.op1 << ")\n";
            break;
        }

        case ir::Opcode::SlabInit: {
            int64_t align = 8;
            if (!inst.strImm.empty()) {
                try { align = std::stoll(inst.strImm); } catch (...) { align = 8; }
            }
            os << "  call void @__tamarin_slab_init(ptr %v" << inst.op1
               << ", i64 " << inst.intImm2
               << ", i64 " << align
               << ", i64 " << inst.intImm << ")\n";
            break;
        }

        case ir::Opcode::SlabInsert: {
            os << "  %v" << inst.result << " = call i64 @__tamarin_slab_insert(ptr %v" << inst.op1
               << ", ptr %v" << inst.op2 << ")\n";
            break;
        }

        case ir::Opcode::SlabGet: {
            auto hTy = getValueType(inst.op2);
            os << "  %v" << inst.result << " = call ptr @__tamarin_slab_get_fast(ptr %v" << inst.op1
               << ", i64 " << formatOperand(inst.op2, hTy) << ")\n";
            break;
        }

        case ir::Opcode::SlabIsValid: {
            auto hTy = getValueType(inst.op2);
            os << "  %call_val_" << inst.result << " = call i32 @__tamarin_slab_is_valid(ptr %v" << inst.op1
               << ", i64 " << formatOperand(inst.op2, hTy) << ")\n";
            os << "  %v" << inst.result << " = icmp ne i32 %call_val_" << inst.result << ", 0\n";
            break;
        }

        case ir::Opcode::SlabTryGet: {
            auto hTy = getValueType(inst.op2);
            os << "  %call_tg_" << inst.result << " = call i32 @__tamarin_slab_try_get(ptr %v" << inst.op1
               << ", i64 " << formatOperand(inst.op2, hTy)
               << ", ptr %v" << inst.op3 << ")\n";
            os << "  %v" << inst.result << " = icmp ne i32 %call_tg_" << inst.result << ", 0\n";
            break;
        }

        case ir::Opcode::SlabRemove: {
            auto hTy = getValueType(inst.op2);
            std::string dtorArg = inst.strImm.empty() ? "ptr null" : ("ptr @" + inst.strImm);
            os << "  %call_rm_" << inst.result << " = call i32 @__tamarin_slab_remove(ptr %v" << inst.op1
               << ", i64 " << formatOperand(inst.op2, hTy)
               << ", " << dtorArg << ")\n";
            os << "  %v" << inst.result << " = icmp ne i32 %call_rm_" << inst.result << ", 0\n";
            break;
        }

        case ir::Opcode::SlabCount: {
            os << "  %v" << inst.result << " = call i64 @__tamarin_slab_count(ptr %v" << inst.op1 << ")\n";
            break;
        }

        case ir::Opcode::SlabCapacity: {
            os << "  %v" << inst.result << " = call i64 @__tamarin_slab_capacity(ptr %v" << inst.op1 << ")\n";
            break;
        }

        case ir::Opcode::SlabClear: {
            std::string dtorArg = inst.strImm.empty() ? "ptr null" : ("ptr @" + inst.strImm);
            os << "  call void @__tamarin_slab_clear(ptr %v" << inst.op1 << ", " << dtorArg << ")\n";
            break;
        }

        case ir::Opcode::SlabDestroy: {
            std::string dtorArg = inst.strImm.empty() ? "ptr null" : ("ptr @" + inst.strImm);
            os << "  call void @__tamarin_slab_destroy(ptr %v" << inst.op1 << ", " << dtorArg << ")\n";
            break;
        }

        case ir::Opcode::MemAlloc: {
            uint32_t opId = memOpId_++;
            auto elemTy = inst.type && inst.type->elementType ? inst.type->elementType : nullptr;
            size_t elemSize = (elemTy && elemTy->size() > 0) ? elemTy->size() : 4;
            auto countTy = getValueType(inst.op1);
            std::string countTyStr = countTy ? llvmType(countTy) : "i64";
            std::string countOp = formatOperand(inst.op1, countTy);
            if (countTyStr == "i64") {
                os << "  %cnt64_alloc_" << opId << " = add i64 " << countOp << ", 0\n";
            } else {
                os << "  %cnt64_alloc_" << opId << " = sext " << countTyStr << " " << countOp << " to i64\n";
            }
            uint64_t maxCount = (uint64_t)(0x7FFFFFFFFFFFFFFFLL / (int64_t)elemSize);
            os << "  %neg_chk_" << opId << " = icmp slt i64 %cnt64_alloc_" << opId << ", 0\n";
            os << "  %ovf_chk_" << opId << " = icmp ugt i64 %cnt64_alloc_" << opId << ", " << maxCount << "\n";
            os << "  %bad_alloc_" << opId << " = or i1 %neg_chk_" << opId << ", %ovf_chk_" << opId << "\n";
            os << "  br i1 %bad_alloc_" << opId << ", label %alloc_bad_" << opId << ", label %alloc_ok_" << opId << "\n";
            os << "alloc_bad_" << opId << ":\n";
            os << "  call void @__tamarin_alloc_error()\n";
            os << "  unreachable\n";
            os << "alloc_ok_" << opId << ":\n";
            os << "  %bytes_alloc_" << opId << " = mul i64 %cnt64_alloc_" << opId << ", " << elemSize << "\n";
            os << "  %ptr_alloc_" << opId << " = call ptr @malloc(i64 %bytes_alloc_" << opId << ")\n";
            os << "  %null_chk_" << opId << " = icmp eq ptr %ptr_alloc_" << opId << ", null\n";
            os << "  %cnt_pos_" << opId << " = icmp sgt i64 %cnt64_alloc_" << opId << ", 0\n";
            os << "  %null_failed_" << opId << " = and i1 %null_chk_" << opId << ", %cnt_pos_" << opId << "\n";
            os << "  br i1 %null_failed_" << opId << ", label %alloc_null_" << opId << ", label %alloc_done_" << opId << "\n";
            os << "alloc_null_" << opId << ":\n";
            os << "  call void @__tamarin_alloc_error()\n";
            os << "  unreachable\n";
            os << "alloc_done_" << opId << ":\n";
            os << "  %v0_alloc_" << opId << " = insertvalue { ptr, i64 } undef, ptr %ptr_alloc_" << opId << ", 0\n";
            os << "  %v" << inst.result << " = insertvalue { ptr, i64 } %v0_alloc_" << opId << ", i64 %cnt64_alloc_" << opId << ", 1\n";
            break;
        }

        case ir::Opcode::MemRealloc: {
            uint32_t opId = memOpId_++;
            auto elemTy = inst.type && inst.type->elementType ? inst.type->elementType : nullptr;
            size_t elemSize = (elemTy && elemTy->size() > 0) ? elemTy->size() : 4;
            auto countTy = getValueType(inst.op2);
            std::string countTyStr = countTy ? llvmType(countTy) : "i64";
            std::string countOp = formatOperand(inst.op2, countTy);
            os << "  %old_ptr_realloc_" << opId << " = extractvalue { ptr, i64 } %v" << inst.op1 << ", 0\n";
            if (countTyStr == "i64") {
                os << "  %cnt64_realloc_" << opId << " = add i64 " << countOp << ", 0\n";
            } else {
                os << "  %cnt64_realloc_" << opId << " = sext " << countTyStr << " " << countOp << " to i64\n";
            }
            uint64_t maxCount = (uint64_t)(0x7FFFFFFFFFFFFFFFLL / (int64_t)elemSize);
            os << "  %realloc_neg_chk_" << opId << " = icmp slt i64 %cnt64_realloc_" << opId << ", 0\n";
            os << "  %realloc_ovf_chk_" << opId << " = icmp ugt i64 %cnt64_realloc_" << opId << ", " << maxCount << "\n";
            os << "  %realloc_bad_" << opId << " = or i1 %realloc_neg_chk_" << opId << ", %realloc_ovf_chk_" << opId << "\n";
            os << "  br i1 %realloc_bad_" << opId << ", label %realloc_bad_lbl_" << opId << ", label %realloc_ok_lbl_" << opId << "\n";
            os << "realloc_bad_lbl_" << opId << ":\n";
            os << "  call void @__tamarin_alloc_error()\n";
            os << "  unreachable\n";
            os << "realloc_ok_lbl_" << opId << ":\n";
            os << "  %bytes_realloc_" << opId << " = mul i64 %cnt64_realloc_" << opId << ", " << elemSize << "\n";
            os << "  %new_ptr_realloc_" << opId << " = call ptr @realloc(ptr %old_ptr_realloc_" << opId << ", i64 %bytes_realloc_" << opId << ")\n";
            os << "  %realloc_null_chk_" << opId << " = icmp eq ptr %new_ptr_realloc_" << opId << ", null\n";
            os << "  %realloc_cnt_pos_" << opId << " = icmp sgt i64 %cnt64_realloc_" << opId << ", 0\n";
            os << "  %realloc_null_failed_" << opId << " = and i1 %realloc_null_chk_" << opId << ", %realloc_cnt_pos_" << opId << "\n";
            os << "  br i1 %realloc_null_failed_" << opId << ", label %realloc_null_lbl_" << opId << ", label %realloc_done_lbl_" << opId << "\n";
            os << "realloc_null_lbl_" << opId << ":\n";
            os << "  call void @__tamarin_alloc_error()\n";
            os << "  unreachable\n";
            os << "realloc_done_lbl_" << opId << ":\n";
            os << "  %v0_realloc_" << opId << " = insertvalue { ptr, i64 } undef, ptr %new_ptr_realloc_" << opId << ", 0\n";
            os << "  %v" << inst.result << " = insertvalue { ptr, i64 } %v0_realloc_" << opId << ", i64 %cnt64_realloc_" << opId << ", 1\n";
            break;
        }

        case ir::Opcode::MemFree: {
            uint32_t opId = memOpId_++;
            os << "  %free_ptr_" << opId << " = extractvalue { ptr, i64 } %v" << inst.op1 << ", 0\n";
            os << "  call void @free(ptr %free_ptr_" << opId << ")\n";
            break;
        }

        case ir::Opcode::MemCopy: {
            uint32_t opId = memOpId_++;
            auto elemTy = inst.type;
            size_t elemSize = (elemTy && elemTy->size() > 0) ? elemTy->size() : 1;
            std::string elemStr = elemTy ? llvmType(elemTy) : "i8";

            auto dstOffTy = getValueType(inst.callArgs[1]);
            auto srcOffTy = getValueType(inst.callArgs[3]);
            auto cntTy = getValueType(inst.callArgs[4]);

            std::string dstOffStr = formatOperand(inst.callArgs[1], dstOffTy);
            std::string srcOffStr = formatOperand(inst.callArgs[3], srcOffTy);
            std::string cntStr = formatOperand(inst.callArgs[4], cntTy);

            std::string dstOffTyStr = dstOffTy ? llvmType(dstOffTy) : "i64";
            std::string srcOffTyStr = srcOffTy ? llvmType(srcOffTy) : "i64";
            std::string cntTyStr = cntTy ? llvmType(cntTy) : "i64";

            os << "  %dst_len_cp_" << opId << " = extractvalue { ptr, i64 } %v" << inst.callArgs[0] << ", 1\n";
            os << "  %src_len_cp_" << opId << " = extractvalue { ptr, i64 } %v" << inst.callArgs[2] << ", 1\n";
            if (dstOffTyStr == "i64") {
                os << "  %dst_off_s_" << opId << " = add i64 " << dstOffStr << ", 0\n";
            } else {
                os << "  %dst_off_s_" << opId << " = sext " << dstOffTyStr << " " << dstOffStr << " to i64\n";
            }
            if (srcOffTyStr == "i64") {
                os << "  %src_off_s_" << opId << " = add i64 " << srcOffStr << ", 0\n";
            } else {
                os << "  %src_off_s_" << opId << " = sext " << srcOffTyStr << " " << srcOffStr << " to i64\n";
            }
            if (cntTyStr == "i64") {
                os << "  %cnt64_cp_" << opId << " = add i64 " << cntStr << ", 0\n";
            } else {
                os << "  %cnt64_cp_" << opId << " = sext " << cntTyStr << " " << cntStr << " to i64\n";
            }
            os << "  %dst_end_cp_" << opId << " = add i64 %dst_off_s_" << opId << ", %cnt64_cp_" << opId << "\n";
            os << "  %src_end_cp_" << opId << " = add i64 %src_off_s_" << opId << ", %cnt64_cp_" << opId << "\n";
            os << "  %cp_bad1_" << opId << " = icmp slt i64 %cnt64_cp_" << opId << ", 0\n";
            os << "  %cp_bad2_" << opId << " = icmp slt i64 %dst_off_s_" << opId << ", 0\n";
            os << "  %cp_bad3_" << opId << " = icmp slt i64 %src_off_s_" << opId << ", 0\n";
            os << "  %cp_bad4_" << opId << " = icmp ugt i64 %dst_end_cp_" << opId << ", %dst_len_cp_" << opId << "\n";
            os << "  %cp_bad5_" << opId << " = icmp ugt i64 %src_end_cp_" << opId << ", %src_len_cp_" << opId << "\n";
            os << "  %cp_b12_" << opId << " = or i1 %cp_bad1_" << opId << ", %cp_bad2_" << opId << "\n";
            os << "  %cp_b34_" << opId << " = or i1 %cp_bad3_" << opId << ", %cp_bad4_" << opId << "\n";
            os << "  %cp_b14_" << opId << " = or i1 %cp_b12_" << opId << ", %cp_b34_" << opId << "\n";
            os << "  %cp_bad_" << opId << " = or i1 %cp_b14_" << opId << ", %cp_bad5_" << opId << "\n";
            os << "  br i1 %cp_bad_" << opId << ", label %cp_fail_" << opId << ", label %cp_ok_" << opId << "\n";
            os << "cp_fail_" << opId << ":\n";
            os << "  call void @__tamarin_bounds_error()\n";
            os << "  unreachable\n";
            os << "cp_ok_" << opId << ":\n";
            os << "  %dst_raw_" << opId << " = extractvalue { ptr, i64 } %v" << inst.callArgs[0] << ", 0\n";
            os << "  %dst_ptr_" << opId << " = getelementptr inbounds " << elemStr << ", ptr %dst_raw_" << opId << ", i64 %dst_off_s_" << opId << "\n";
            os << "  %src_raw_" << opId << " = extractvalue { ptr, i64 } %v" << inst.callArgs[2] << ", 0\n";
            os << "  %src_ptr_" << opId << " = getelementptr inbounds " << elemStr << ", ptr %src_raw_" << opId << ", i64 %src_off_s_" << opId << "\n";
            os << "  %bytes_cp_" << opId << " = mul i64 %cnt64_cp_" << opId << ", " << elemSize << "\n";
            os << "  call void @llvm.memcpy.p0.p0.i64(ptr %dst_ptr_" << opId << ", ptr %src_ptr_" << opId << ", i64 %bytes_cp_" << opId << ", i1 false)\n";
            break;
        }

        case ir::Opcode::MemMove: {
            uint32_t opId = memOpId_++;
            auto elemTy = inst.type;
            size_t elemSize = (elemTy && elemTy->size() > 0) ? elemTy->size() : 1;
            std::string elemStr = elemTy ? llvmType(elemTy) : "i8";

            auto dstOffTy = getValueType(inst.callArgs[1]);
            auto srcOffTy = getValueType(inst.callArgs[3]);
            auto cntTy = getValueType(inst.callArgs[4]);

            std::string dstOffStr = formatOperand(inst.callArgs[1], dstOffTy);
            std::string srcOffStr = formatOperand(inst.callArgs[3], srcOffTy);
            std::string cntStr = formatOperand(inst.callArgs[4], cntTy);

            std::string dstOffTyStr = dstOffTy ? llvmType(dstOffTy) : "i64";
            std::string srcOffTyStr = srcOffTy ? llvmType(srcOffTy) : "i64";
            std::string cntTyStr = cntTy ? llvmType(cntTy) : "i64";

            os << "  %dst_len_mv_" << opId << " = extractvalue { ptr, i64 } %v" << inst.callArgs[0] << ", 1\n";
            os << "  %src_len_mv_" << opId << " = extractvalue { ptr, i64 } %v" << inst.callArgs[2] << ", 1\n";
            if (dstOffTyStr == "i64") {
                os << "  %dst_off_mv_s_" << opId << " = add i64 " << dstOffStr << ", 0\n";
            } else {
                os << "  %dst_off_mv_s_" << opId << " = sext " << dstOffTyStr << " " << dstOffStr << " to i64\n";
            }
            if (srcOffTyStr == "i64") {
                os << "  %src_off_mv_s_" << opId << " = add i64 " << srcOffStr << ", 0\n";
            } else {
                os << "  %src_off_mv_s_" << opId << " = sext " << srcOffTyStr << " " << srcOffStr << " to i64\n";
            }
            if (cntTyStr == "i64") {
                os << "  %cnt64_mv_" << opId << " = add i64 " << cntStr << ", 0\n";
            } else {
                os << "  %cnt64_mv_" << opId << " = sext " << cntTyStr << " " << cntStr << " to i64\n";
            }
            os << "  %dst_end_mv_" << opId << " = add i64 %dst_off_mv_s_" << opId << ", %cnt64_mv_" << opId << "\n";
            os << "  %src_end_mv_" << opId << " = add i64 %src_off_mv_s_" << opId << ", %cnt64_mv_" << opId << "\n";
            os << "  %mv_bad1_" << opId << " = icmp slt i64 %cnt64_mv_" << opId << ", 0\n";
            os << "  %mv_bad2_" << opId << " = icmp slt i64 %dst_off_mv_s_" << opId << ", 0\n";
            os << "  %mv_bad3_" << opId << " = icmp slt i64 %src_off_mv_s_" << opId << ", 0\n";
            os << "  %mv_bad4_" << opId << " = icmp ugt i64 %dst_end_mv_" << opId << ", %dst_len_mv_" << opId << "\n";
            os << "  %mv_bad5_" << opId << " = icmp ugt i64 %src_end_mv_" << opId << ", %src_len_mv_" << opId << "\n";
            os << "  %mv_b12_" << opId << " = or i1 %mv_bad1_" << opId << ", %mv_bad2_" << opId << "\n";
            os << "  %mv_b34_" << opId << " = or i1 %mv_bad3_" << opId << ", %mv_bad4_" << opId << "\n";
            os << "  %mv_b14_" << opId << " = or i1 %mv_b12_" << opId << ", %mv_b34_" << opId << "\n";
            os << "  %mv_bad_" << opId << " = or i1 %mv_b14_" << opId << ", %mv_bad5_" << opId << "\n";
            os << "  br i1 %mv_bad_" << opId << ", label %mv_fail_" << opId << ", label %mv_ok_" << opId << "\n";
            os << "mv_fail_" << opId << ":\n";
            os << "  call void @__tamarin_bounds_error()\n";
            os << "  unreachable\n";
            os << "mv_ok_" << opId << ":\n";
            os << "  %dst_raw_mv_" << opId << " = extractvalue { ptr, i64 } %v" << inst.callArgs[0] << ", 0\n";
            os << "  %dst_ptr_mv_" << opId << " = getelementptr inbounds " << elemStr << ", ptr %dst_raw_mv_" << opId << ", i64 %dst_off_mv_s_" << opId << "\n";
            os << "  %src_raw_mv_" << opId << " = extractvalue { ptr, i64 } %v" << inst.callArgs[2] << ", 0\n";
            os << "  %src_ptr_mv_" << opId << " = getelementptr inbounds " << elemStr << ", ptr %src_raw_mv_" << opId << ", i64 %src_off_mv_s_" << opId << "\n";
            os << "  %bytes_mv_" << opId << " = mul i64 %cnt64_mv_" << opId << ", " << elemSize << "\n";
            os << "  call void @llvm.memmove.p0.p0.i64(ptr %dst_ptr_mv_" << opId << ", ptr %src_ptr_mv_" << opId << ", i64 %bytes_mv_" << opId << ", i1 false)\n";
            break;
        }

        case ir::Opcode::MemSet: {
            uint32_t opId = memOpId_++;
            auto elemTy = inst.type;
            size_t elemSize = (elemTy && elemTy->size() > 0) ? elemTy->size() : 1;
            std::string elemStr = elemTy ? llvmType(elemTy) : "i8";

            auto dstOffTy = getValueType(inst.callArgs[1]);
            auto valTy = getValueType(inst.callArgs[2]);
            auto cntTy = getValueType(inst.callArgs[3]);

            std::string dstOffStr = formatOperand(inst.callArgs[1], dstOffTy);
            std::string valStr = formatOperand(inst.callArgs[2], valTy);
            std::string cntStr = formatOperand(inst.callArgs[3], cntTy);

            std::string dstOffTyStr = dstOffTy ? llvmType(dstOffTy) : "i64";
            std::string valTyStr = valTy ? llvmType(valTy) : "i8";
            std::string cntTyStr = cntTy ? llvmType(cntTy) : "i64";

            os << "  %dst_len_st_" << opId << " = extractvalue { ptr, i64 } %v" << inst.callArgs[0] << ", 1\n";
            if (dstOffTyStr == "i64") {
                os << "  %dst_off_st_s_" << opId << " = add i64 " << dstOffStr << ", 0\n";
            } else {
                os << "  %dst_off_st_s_" << opId << " = sext " << dstOffTyStr << " " << dstOffStr << " to i64\n";
            }
            if (cntTyStr == "i64") {
                os << "  %cnt64_st_" << opId << " = add i64 " << cntStr << ", 0\n";
            } else {
                os << "  %cnt64_st_" << opId << " = sext " << cntTyStr << " " << cntStr << " to i64\n";
            }
            os << "  %dst_end_st_" << opId << " = add i64 %dst_off_st_s_" << opId << ", %cnt64_st_" << opId << "\n";
            os << "  %st_bad1_" << opId << " = icmp slt i64 %cnt64_st_" << opId << ", 0\n";
            os << "  %st_bad2_" << opId << " = icmp slt i64 %dst_off_st_s_" << opId << ", 0\n";
            os << "  %st_bad3_" << opId << " = icmp ugt i64 %dst_end_st_" << opId << ", %dst_len_st_" << opId << "\n";
            os << "  %st_b12_" << opId << " = or i1 %st_bad1_" << opId << ", %st_bad2_" << opId << "\n";
            os << "  %st_bad_" << opId << " = or i1 %st_b12_" << opId << ", %st_bad3_" << opId << "\n";
            os << "  br i1 %st_bad_" << opId << ", label %st_fail_" << opId << ", label %st_ok_" << opId << "\n";
            os << "st_fail_" << opId << ":\n";
            os << "  call void @__tamarin_bounds_error()\n";
            os << "  unreachable\n";
            os << "st_ok_" << opId << ":\n";
            os << "  %dst_raw_st_" << opId << " = extractvalue { ptr, i64 } %v" << inst.callArgs[0] << ", 0\n";
            os << "  %dst_ptr_st_" << opId << " = getelementptr inbounds " << elemStr << ", ptr %dst_raw_st_" << opId << ", i64 %dst_off_st_s_" << opId << "\n";

            if (valTyStr != "i8") {
                os << "  %byte8_st_" << opId << " = trunc " << valTyStr << " " << valStr << " to i8\n";
                valStr = "%byte8_st_" + std::to_string(opId);
            }

            os << "  %bytes_st_" << opId << " = mul i64 %cnt64_st_" << opId << ", " << elemSize << "\n";
            os << "  call void @llvm.memset.p0.i64(ptr %dst_ptr_st_" << opId << ", i8 " << valStr << ", i64 %bytes_st_" << opId << ", i1 false)\n";
            break;
        }

        case ir::Opcode::VecSplat: {
            auto vecTy = inst.type;
            uint32_t width = vecTy && vecTy->isSIMDVector() ? vecTy->vectorWidth : 4;
            std::string vecTyStr = llvmType(vecTy);
            std::string elemTyStr = (vecTy && vecTy->elementType) ? llvmType(vecTy->elementType) : "float";
            std::string scalarStr = formatOperand(inst.op1, vecTy ? vecTy->elementType : nullptr);
            uint32_t sId = memOpId_++;

            os << "  %splat_ins_" << sId << " = insertelement " << vecTyStr << " poison, "
               << elemTyStr << " " << scalarStr << ", i32 0\n";
            os << "  %v" << inst.result << " = shufflevector " << vecTyStr << " %splat_ins_" << sId
               << ", " << vecTyStr << " poison, <" << width << " x i32> zeroinitializer\n";
            break;
        }

        case ir::Opcode::VecExtract: {
            auto vecTy = getValueType(inst.op1);
            std::string vecTyStr = vecTy ? llvmType(vecTy) : "<4 x float>";
            std::string laneStr = std::to_string(inst.intImm);
            if (inst.op2 != ir::InvalidValue) {
                auto idxTy = getValueType(inst.op2);
                laneStr = formatOperand(inst.op2, idxTy);
            }
            os << "  %v" << inst.result << " = extractelement " << vecTyStr
               << " %v" << inst.op1 << ", i32 " << laneStr << "\n";
            break;
        }

        case ir::Opcode::VecInsert: {
            auto vecTy = inst.type ? inst.type : getValueType(inst.op1);
            std::string vecTyStr = llvmType(vecTy);
            std::string elemTyStr = (vecTy && vecTy->elementType) ? llvmType(vecTy->elementType) : "float";
            std::string valStr = formatOperand(inst.op2, vecTy ? vecTy->elementType : nullptr);
            std::string laneStr = std::to_string(inst.intImm);
            if (inst.op3 != ir::InvalidValue) {
                auto idxTy = getValueType(inst.op3);
                laneStr = formatOperand(inst.op3, idxTy);
            }
            os << "  %v" << inst.result << " = insertelement " << vecTyStr
               << " %v" << inst.op1 << ", " << elemTyStr << " " << valStr << ", i32 " << laneStr << "\n";
            break;
        }

        case ir::Opcode::VecShuffle: {
            auto vecTy = getValueType(inst.op1);
            std::string vecTyStr = vecTy ? llvmType(vecTy) : "<4 x i32>";
            std::string op2Str = (inst.op2 != ir::InvalidValue) ? ("%v" + std::to_string(inst.op2)) : "undef";
            os << "  %v" << inst.result << " = shufflevector " << vecTyStr << " %v" << inst.op1
               << ", " << vecTyStr << " " << op2Str << ", <4 x i32> <i32 0, i32 1, i32 2, i32 3>\n";
            break;
        }

        case ir::Opcode::VecReduceAdd: {
            auto vecTy = getValueType(inst.op1);
            bool isFloat = (vecTy && vecTy->elementType && vecTy->elementType->isFloat()) ||
                           (inst.type && inst.type->isFloat());
            uint32_t width = vecTy && vecTy->isSIMDVector() ? vecTy->vectorWidth : 4;
            std::string vecTyStr = vecTy ? llvmType(vecTy) : (isFloat ? "<4 x float>" : "<4 x i32>");

            if (isFloat) {
                std::string intrinsic = (width == 8) ? "@llvm.vector.reduce.fadd.v8f32" : "@llvm.vector.reduce.fadd.v4f32";
                os << "  %v" << inst.result << " = call " << (fastMath_ ? "fast " : "") << "float " << intrinsic
                   << "(float -0.000000e+00, " << vecTyStr << " %v" << inst.op1 << ")\n";
            } else {
                std::string intrinsic = (width == 16) ? "@llvm.vector.reduce.add.v16i32" :
                                        ((width == 8) ? "@llvm.vector.reduce.add.v8i32" : "@llvm.vector.reduce.add.v4i32");
                os << "  %v" << inst.result << " = call i32 " << intrinsic
                   << "(" << vecTyStr << " %v" << inst.op1 << ")\n";
            }
            break;
        }

        case ir::Opcode::VecDot: {
            auto vecTy = getValueType(inst.op1);
            bool isFloat = (vecTy && vecTy->elementType && vecTy->elementType->isFloat()) ||
                           (inst.type && inst.type->isFloat());
            uint32_t width = vecTy && vecTy->isSIMDVector() ? vecTy->vectorWidth : 4;
            std::string vecTyStr = vecTy ? llvmType(vecTy) : (isFloat ? "<4 x float>" : "<4 x i32>");
            uint32_t dId = memOpId_++;

            if (isFloat) {
                os << "  %dot_mul_" << dId << " = fmul " << (fastMath_ ? "fast " : "")
                   << vecTyStr << " %v" << inst.op1 << ", %v" << inst.op2 << "\n";
                std::string intrinsic = (width == 8) ? "@llvm.vector.reduce.fadd.v8f32" : "@llvm.vector.reduce.fadd.v4f32";
                os << "  %v" << inst.result << " = call " << (fastMath_ ? "fast " : "") << "float " << intrinsic
                   << "(float -0.000000e+00, " << vecTyStr << " %dot_mul_" << dId << ")\n";
            } else {
                os << "  %dot_mul_" << dId << " = mul " << vecTyStr << " %v" << inst.op1 << ", %v" << inst.op2 << "\n";
                std::string intrinsic = (width == 16) ? "@llvm.vector.reduce.add.v16i32" :
                                        ((width == 8) ? "@llvm.vector.reduce.add.v8i32" : "@llvm.vector.reduce.add.v4i32");
                os << "  %v" << inst.result << " = call i32 " << intrinsic
                   << "(" << vecTyStr << " %dot_mul_" << dId << ")\n";
            }
            break;
        }

        case ir::Opcode::ParallelFor: {
            std::string startVal = formatOperand(inst.op1);
            std::string endVal = formatOperand(inst.op2);
            std::string grainVal = (inst.op3 != ir::InvalidValue) ? formatOperand(inst.op3) : "0";
            std::string ctxVal = (!inst.callArgs.empty()) ? ("%v" + std::to_string(inst.callArgs[0])) : "null";
            os << "  call void @tamarin_parallel_for(i64 " << startVal << ", i64 " << endVal << ", i64 " << grainVal
               << ", ptr @" << inst.strImm << ", ptr " << ctxVal << ")\n";
            break;
        }

        case ir::Opcode::TaskScopeBegin: {
            os << "  %v" << inst.result << " = call ptr @tamarin_task_scope_begin()\n";
            break;
        }

        case ir::Opcode::TaskScopeEnd: {
            os << "  call void @tamarin_task_scope_end(ptr %v" << inst.op1 << ")\n";
            break;
        }

        case ir::Opcode::TaskSpawn: {
            std::string ctxVal = (inst.op2 != ir::InvalidValue) ? ("%v" + std::to_string(inst.op2)) : "null";
            os << "  call void @tamarin_task_spawn(ptr %v" << inst.op1 << ", ptr @" << inst.strImm << ", ptr " << ctxVal << ")\n";
            break;
        }

        case ir::Opcode::AsyncTaskCreate: {
            std::string ctxVal = (inst.op1 != ir::InvalidValue) ? ("%v" + std::to_string(inst.op1)) : "null";
            std::string ctxSize = (inst.op2 != ir::InvalidValue) ? formatOperand(inst.op2) : "0";
            std::string resSize = (inst.op3 != ir::InvalidValue) ? formatOperand(inst.op3) : "0";
            os << "  %v" << inst.result << " = call ptr @tamarin_async_task_create(ptr @" << inst.strImm
               << ", ptr " << ctxVal << ", i64 " << ctxSize << ", i64 " << resSize << ")\n";
            break;
        }

        case ir::Opcode::AsyncTaskAwait: {
            os << "  %v" << inst.result << " = call ptr @tamarin_async_task_await(ptr %v" << inst.op1 << ")\n";
            break;
        }

        case ir::Opcode::AsyncTaskIsReady: {
            os << "  %v" << inst.result << " = call i32 @tamarin_async_task_is_ready(ptr %v" << inst.op1 << ")\n";
            break;
        }

        case ir::Opcode::AsyncTaskFree: {
            os << "  call void @tamarin_async_task_free(ptr %v" << inst.op1 << ")\n";
            break;
        }

        case ir::Opcode::AtomicLoad: {
            auto loadedTy = inst.type ? llvmType(inst.type) : "i32";
            int align = inst.type ? (int)inst.type->alignment() : 4;
            if (align < 1) align = 4;
            os << "  %v" << inst.result << " = load atomic " << loadedTy
               << ", ptr %v" << inst.op1 << " seq_cst, align " << align << "\n";
            break;
        }

        case ir::Opcode::AtomicStore: {
            auto valTy = inst.type ? inst.type : getValueType(inst.op2);
            std::string tyStr = valTy ? llvmType(valTy) : "i32";
            int align = valTy ? (int)valTy->alignment() : 4;
            if (align < 1) align = 4;
            os << "  store atomic " << tyStr << " " << formatOperand(inst.op2, valTy)
               << ", ptr %v" << inst.op1 << " seq_cst, align " << align << "\n";
            break;
        }

        case ir::Opcode::AtomicRMW: {
            auto valTy = inst.type ? inst.type : getValueType(inst.op2);
            std::string tyStr = valTy ? llvmType(valTy) : "i32";
            bool isFloat = valTy && valTy->isFloat();
            bool isUnsigned = valTy && valTy->isUnsignedInteger();
            std::string opStr = "add";
            switch (inst.intImm) {
                case 0: opStr = isFloat ? "fadd" : "add"; break;
                case 1: opStr = isFloat ? "fsub" : "sub"; break;
                case 3: opStr = isFloat ? "fmin" : (isUnsigned ? "umin" : "min"); break;
                case 4: opStr = isFloat ? "fmax" : (isUnsigned ? "umax" : "max"); break;
                case 5: opStr = "and"; break;
                case 6: opStr = "or"; break;
                case 7: opStr = "xor"; break;
                default: opStr = isFloat ? "fadd" : "add"; break;
            }
            if (inst.result != ir::InvalidValue) {
                os << "  %v" << inst.result << " = atomicrmw " << opStr << " ptr %v" << inst.op1
                   << ", " << tyStr << " " << formatOperand(inst.op2, valTy) << " seq_cst\n";
            } else {
                uint32_t discardId = memOpId_++;
                os << "  %rmw_res_" << discardId << " = atomicrmw " << opStr << " ptr %v" << inst.op1
                   << ", " << tyStr << " " << formatOperand(inst.op2, valTy) << " seq_cst\n";
            }
            break;
        }

        case ir::Opcode::AtomicCmpXchg: {
            auto valTy = getValueType(inst.op2);
            if (!valTy) valTy = getValueType(inst.op3);
            std::string tyStr = valTy ? llvmType(valTy) : "i32";
            uint32_t cmpxId = memOpId_++;
            os << "  %cmpxchg_res_" << cmpxId << " = cmpxchg ptr %v" << inst.op1
               << ", " << tyStr << " " << formatOperand(inst.op2, valTy)
               << ", " << tyStr << " " << formatOperand(inst.op3, valTy) << " seq_cst seq_cst\n";
            os << "  %v" << inst.result << " = extractvalue { " << tyStr << ", i1 } %cmpxchg_res_" << cmpxId << ", 1\n";
            break;
        }

        default:
            os << "  ; unhandled opcode\n";
            break;
    }
}

bool LLVMCodeGen::emitLLVMIR(std::ostream& os) {
    loopUnrollsUsed_.clear();
    loopMetadataMap_.clear();
    functionMap_.clear();
    for (const auto& fn : module_.functions) {
        functionMap_[fn->name] = fn.get();
    }
    emitHeader(os);
    emitRuntimeHelpers(os);
    for (auto& fn : module_.functions) {
        emitFunction(*fn, os);
    }
    os << "!0 = !{}\n";
    os << "!1 = !{!\"branch_weights\", i32 1, i32 1048576}\n";
    for (const auto& [key, metaId] : loopMetadataMap_) {
        os << "!" << metaId << " = distinct !{!" << metaId;
        if (key.vectorizeEnable == 1 || key.vectorizeWidth > 0 || key.interleaveCount > 0) {
            os << ", !{!\"llvm.loop.vectorize.enable\", i1 true}";
            if (key.vectorizeWidth > 0) {
                os << ", !{!\"llvm.loop.vectorize.width\", i32 " << key.vectorizeWidth << "}";
            }
            if (key.interleaveCount > 0) {
                os << ", !{!\"llvm.loop.interleave.count\", i32 " << key.interleaveCount << "}";
            }
        } else if (key.vectorizeEnable == -1) {
            os << ", !{!\"llvm.loop.vectorize.enable\", i1 false}";
        }

        if (key.unrollCount == 1) {
            os << ", !{!\"llvm.loop.unroll.enable\"}";
        } else if (key.unrollCount > 1) {
            os << ", !{!\"llvm.loop.unroll.count\", i32 " << key.unrollCount << "}";
        } else if (key.unrollCount == -1) {
            os << ", !{!\"llvm.loop.unroll.disable\"}";
        }
        os << "}\n";
    }
    return true;
}

bool LLVMCodeGen::emitLLVMIRToFile(const std::filesystem::path& path) {
    std::ofstream out(path);
    if (!out) {
        diag_.report(Severity::Error, "failed to open output file: " + path.string());
        return false;
    }
    return emitLLVMIR(out);
}

static bool isSafeArchString(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_' && c != '.') {
            return false;
        }
    }
    return true;
}

static std::string resolveArchFlag(const std::string& targetCPU) {
    if (!targetCPU.empty() && isSafeArchString(targetCPU)) {
        return " -march=" + targetCPU;
    }
    const char* envCpu = std::getenv("TAMARIN_TARGET_CPU");
    if (!envCpu || !*envCpu) envCpu = std::getenv("TAMARIN_ARCH");
    if (envCpu && *envCpu && isSafeArchString(envCpu)) {
        return " -march=" + std::string(envCpu);
    }
    return "";
}

bool LLVMCodeGen::optimizeLLVMIR(const std::filesystem::path& inputLL,
                                const std::filesystem::path& outputLL,
                                int optLevel) {
    std::string optFlag = "-O" + std::to_string(optLevel);
    std::string archFlag = resolveArchFlag(targetCPU_);
    std::string fastMath = fastMath_ ? " -ffast-math" : "";
    std::string extraOpt = ((optLevel >= 3) ? " -mllvm -inline-threshold=2500 -mllvm -unroll-threshold=800 -mllvm -vectorize-loops=true -mllvm -vectorize-slp=true" : ((optLevel >= 2) ? " -mllvm -inline-threshold=1000 -mllvm -vectorize-loops=true" : "")) + fastMath;
    std::string cmd = "\"" + clangPath_ + "\" -x ir " + optFlag + archFlag + extraOpt +
                      " -S -emit-llvm --target=" + targetTriple_.str() + " -Wno-override-module -o \"" +
                      outputLL.string() + "\" \"" + inputLL.string() + "\"";

    int ret = runSystemCommand(cmd);
    if (ret != 0) {
        diag_.report(Severity::Error, "LLVM optimization pipeline failed with code " + std::to_string(ret));
        return false;
    }
    return true;
}

bool LLVMCodeGen::compileToAssembly(const std::filesystem::path& llPath,
                                   const std::filesystem::path& asmPath,
                                   int optLevel) {
    std::string optFlag = "-O" + std::to_string(optLevel);
    std::string archFlag = resolveArchFlag(targetCPU_);
    std::string fastMath = fastMath_ ? " -ffast-math" : "";
    std::string extraOpt = ((optLevel >= 3) ? " -mllvm -inline-threshold=2500 -mllvm -unroll-threshold=800 -mllvm -vectorize-loops=true -mllvm -vectorize-slp=true" : ((optLevel >= 2) ? " -mllvm -inline-threshold=1000 -mllvm -vectorize-loops=true" : "")) + fastMath;
    std::string cmd = "\"" + clangPath_ + "\" -x ir " + optFlag + archFlag + extraOpt +
                      " -S --target=" + targetTriple_.str() + " -Wno-override-module -o \"" +
                      asmPath.string() + "\" \"" + llPath.string() + "\"";

    int ret = runSystemCommand(cmd);
    if (ret != 0) {
        diag_.report(Severity::Error, "clang assembly generation failed with code " + std::to_string(ret));
        return false;
    }
    return true;
}

bool LLVMCodeGen::isStructPassedByPointerInCABI(const sema::Type* ty) const {
    if (!ty || ty->isReference()) return false;
    if (!ty->isRecord() && !ty->isStruct()) {
        return false;
    }
    size_t sz = ty->size();
    if (targetTriple_.isWindows()) {
        // In MSVC x64 ABI, structs of size 1, 2, 4, or 8 bytes are passed as integers.
        // All other structs are passed by invisible pointer.
        return (sz != 1 && sz != 2 && sz != 4 && sz != 8);
    } else {
        // System V AMD64 / AAPCS64: structs > 16 bytes are passed by pointer/memory.
        return sz > 16;
    }
}

std::string LLVMCodeGen::getCABISmallStructIntType(const sema::Type* ty) const {
    if (!ty || ty->isReference()) return "";
    if (!ty->isRecord() && !ty->isStruct()) {
        return "";
    }
    if (targetTriple_.isWindows()) {
        size_t sz = ty->size();
        if (sz == 1) return "i8";
        if (sz == 2) return "i16";
        if (sz == 4) return "i32";
        if (sz == 8) return "i64";
    }
    return "";
}

std::string LLVMCodeGen::formatNativeLinkFlags() const {
    std::string nativeLinkFlags;
    for (const auto& dir : linkDirs_) {
        nativeLinkFlags += " -L\"" + dir + "\"";
        if (!targetTriple_.isWindows()) {
            nativeLinkFlags += " -Wl,-rpath,\"" + dir + "\"";
        }
    }
    for (const auto& lib : linkLibs_) {
        if (lib.empty()) continue;
        if (lib.starts_with("-l") || lib.starts_with("-framework")) {
            nativeLinkFlags += " " + lib;
        } else if (lib.ends_with(".lib") || lib.ends_with(".a") || lib.ends_with(".so") ||
                   lib.ends_with(".dylib") || lib.ends_with(".dll") ||
                   lib.find('/') != std::string::npos || lib.find('\\') != std::string::npos) {
            nativeLinkFlags += " \"" + lib + "\"";
        } else {
            nativeLinkFlags += " -l" + lib;
        }
    }
    for (const auto& flag : linkerFlags_) {
        if (!flag.empty()) {
            nativeLinkFlags += " " + flag;
        }
    }
    return nativeLinkFlags;
}

bool LLVMCodeGen::compileToExecutable(const std::filesystem::path& llPath,
                                     const std::filesystem::path& exePath,
                                     int optLevel) {
    std::string optFlag = "-O" + std::to_string(optLevel);
    std::string archFlag = resolveArchFlag(targetCPU_);
    std::string fastMath = fastMath_ ? " -ffast-math" : "";
    std::string extraOpt = ((optLevel >= 3) ? " -mllvm -inline-threshold=2500 -mllvm -unroll-threshold=800 -mllvm -vectorize-loops=true -mllvm -vectorize-slp=true" : ((optLevel >= 2) ? " -mllvm -inline-threshold=1000 -mllvm -vectorize-loops=true" : "")) + fastMath;
    std::string crtFlag;
    if (targetTriple_.isWindows()) {
        if (crtLinkage_ == CrtLinkage::Static) {
            crtFlag = " -fuse-ld=lld -fms-runtime-lib=static -Xlinker /DEFAULTLIB:libcmt.lib -Xlinker /NODEFAULTLIB:msvcrt.lib -Xlinker /NODEFAULTLIB:msvcprt.lib";
        } else {
            crtFlag = " -fuse-ld=lld -fms-runtime-lib=dll -Xlinker /DEFAULTLIB:msvcrt.lib -Xlinker /NODEFAULTLIB:libcmt.lib";
        }
    } else if (targetTriple_.isMacOS()) {
        crtFlag = "";
    } else {
        crtFlag = (crtLinkage_ == CrtLinkage::Static) ? " -static-libstdc++ -static-libgcc" : "";
    }
    std::string rtLib = findRuntimeLibrary(crtLinkage_ == CrtLinkage::Static);
    std::string rtFlag;
    if (targetTriple_.isWindows()) {
        rtFlag = !rtLib.empty() ? (" \"" + rtLib + "\"") : "";
    } else {
        std::string cxxLibs = targetTriple_.cxxStdLibLinkFlags();
        rtFlag = (!rtLib.empty() ? (" \"" + rtLib + "\" ") : "") + cxxLibs;
    }
    std::string nativeFlags = formatNativeLinkFlags();
    std::string cmd = "\"" + clangPath_ + "\" " + optFlag + archFlag + extraOpt + crtFlag +
                      " --target=" + targetTriple_.str() + " -Wno-override-module -o \"" +
                      exePath.string() + "\" \"" + llPath.string() + "\"" + rtFlag + nativeFlags;

    int ret = runSystemCommand(cmd);
    if (ret != 0) {
        diag_.report(Severity::Error, "clang compilation failed with code " + std::to_string(ret));
        return false;
    }
    return true;
}

bool LLVMCodeGen::compileToObject(const std::filesystem::path& llPath,
                                 const std::filesystem::path& objPath,
                                 int optLevel) {
    std::string optFlag = "-O" + std::to_string(optLevel);
    std::string archFlag = resolveArchFlag(targetCPU_);
    std::string fastMath = fastMath_ ? " -ffast-math" : "";
    std::string extraOpt = ((optLevel >= 3) ? " -mllvm -inline-threshold=2500 -mllvm -unroll-threshold=800 -mllvm -vectorize-loops=true -mllvm -vectorize-slp=true" : ((optLevel >= 2) ? " -mllvm -inline-threshold=1000 -mllvm -vectorize-loops=true" : "")) + fastMath;
    std::string cmd = "\"" + clangPath_ + "\" -x ir " + optFlag + archFlag + extraOpt +
                      " -c --target=" + targetTriple_.str() + " -Wno-override-module -o \"" +
                      objPath.string() + "\" \"" + llPath.string() + "\"";

    int ret = runSystemCommand(cmd);
    if (ret != 0) {
        diag_.report(Severity::Error, "clang object compilation failed with code " + std::to_string(ret));
        return false;
    }
    return true;
}

bool LLVMCodeGen::compileToSharedLibrary(const std::filesystem::path& llPath,
                                        const std::filesystem::path& dylibPath,
                                        int optLevel) {
    std::string optFlag = "-O" + std::to_string(optLevel);
    std::string archFlag = resolveArchFlag(targetCPU_);
    std::string fastMath = fastMath_ ? " -ffast-math" : "";
    std::string extraOpt = ((optLevel >= 3) ? " -mllvm -inline-threshold=2500 -mllvm -unroll-threshold=800 -mllvm -vectorize-loops=true -mllvm -vectorize-slp=true" : ((optLevel >= 2) ? " -mllvm -inline-threshold=1000 -mllvm -vectorize-loops=true" : "")) + fastMath;
    std::string crtFlag;
    if (targetTriple_.isWindows()) {
        crtFlag = " -fuse-ld=lld -fms-runtime-lib=dll -Xlinker /DEFAULTLIB:msvcrt.lib -Xlinker /NODEFAULTLIB:libcmt.lib -Xlinker /DEFAULTLIB:kernel32.lib";
    }
    std::string rtLib = findRuntimeLibrary();
    std::string rtFlag;
    if (targetTriple_.isWindows()) {
        rtFlag = !rtLib.empty() ? (" \"" + rtLib + "\"") : "";
    } else {
        std::string cxxLibs = targetTriple_.cxxStdLibLinkFlags();
        rtFlag = (!rtLib.empty() ? (" \"" + rtLib + "\" ") : "") + cxxLibs;
    }
    std::string fpic = targetTriple_.isWindows() ? "" : " -fPIC";
    std::string nativeFlags = formatNativeLinkFlags();
    std::string cmd = "\"" + clangPath_ + "\" " + optFlag + archFlag + extraOpt + crtFlag + fpic +
                      " -shared --target=" + targetTriple_.str() + " -Wno-override-module -o \"" +
                      dylibPath.string() + "\" \"" + llPath.string() + "\"" + rtFlag + nativeFlags;

    int ret = runSystemCommand(cmd);
    if (ret != 0) {
        diag_.report(Severity::Error, "clang shared library compilation failed with code " + std::to_string(ret));
        return false;
    }
    return true;
}

bool LLVMCodeGen::compileToStaticLibrary(const std::filesystem::path& llPath,
                                        const std::filesystem::path& libPath,
                                        int optLevel) {
    auto tempObj = libPath;
    tempObj.replace_extension(targetTriple_.objectExtension());
    if (!compileToObject(llPath, tempObj, optLevel)) {
        return false;
    }

    std::filesystem::path clangP(clangPath_);
    std::filesystem::path llvmAr = clangP.parent_path() / "llvm-ar";
#ifdef _WIN32
    if (!std::filesystem::exists(llvmAr)) {
        llvmAr = clangP.parent_path() / "llvm-ar.exe";
    }
#endif

    std::string arCmd;
    if (std::filesystem::exists(llvmAr)) {
        arCmd = "\"" + llvmAr.string() + "\" rcs \"" + libPath.string() + "\" \"" + tempObj.string() + "\"";
    } else {
#ifdef _WIN32
        arCmd = "lib.exe /NOLOGO /OUT:\"" + libPath.string() + "\" \"" + tempObj.string() + "\"";
#else
        arCmd = "ar rcs \"" + libPath.string() + "\" \"" + tempObj.string() + "\"";
#endif
    }

    int ret = runSystemCommand(arCmd);
    std::error_code ec;
    std::filesystem::remove(tempObj, ec);

    if (ret != 0) {
        diag_.report(Severity::Error, "static library archive creation failed with code " + std::to_string(ret));
        return false;
    }
    return true;
}

} // namespace tamarin::backend
