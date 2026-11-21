// ============================================================================
// llvm_codegen.h - Lowers Tamarin IR to LLVM IR and produces native executables
// ============================================================================
#pragma once

#include "tamarin/ir/ir.h"
#include "tamarin/diagnostics/diagnostic_engine.h"
#include "tamarin/target/target_triple.h"

#include <filesystem>
#include <map>
#include <ostream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace tamarin::backend {

struct LoopMetaKey {
    int32_t unrollCount = 0;
    int32_t vectorizeWidth = 0;
    int32_t interleaveCount = 0;
    int32_t vectorizeEnable = 0;

    bool operator<(const LoopMetaKey& o) const {
        return std::tie(unrollCount, vectorizeWidth, interleaveCount, vectorizeEnable) <
               std::tie(o.unrollCount, o.vectorizeWidth, o.interleaveCount, o.vectorizeEnable);
    }
    bool hasHints() const {
        return unrollCount != 0 || vectorizeWidth != 0 || interleaveCount != 0 || vectorizeEnable != 0;
    }
};

enum class CrtLinkage {
    Dynamic,
    Static
};

class LLVMCodeGen {
public:
    explicit LLVMCodeGen(const ir::IRModule& module, DiagnosticEngine& diag, std::string clangPath = "");

    void setClangPath(std::string path);
    const std::string& getClangPath() const;

    void setTargetTriple(target::TargetTriple triple) { targetTriple_ = std::move(triple); }
    void setTargetTriple(std::string_view tripleStr) { targetTriple_ = target::TargetTriple::parse(tripleStr); }
    const target::TargetTriple& getTargetTriple() const { return targetTriple_; }

    void setTargetCPU(std::string cpu) { targetCPU_ = std::move(cpu); }
    const std::string& getTargetCPU() const { return targetCPU_; }

    /// Emit LLVM IR assembly (.ll) to the given output stream.
    bool emitLLVMIR(std::ostream& os);

    /// Emit LLVM IR to a file.
    bool emitLLVMIRToFile(const std::filesystem::path& path);

    /// Run LLVM optimization pipeline at the given level (-O1, -O2, -O3) and emit optimized IR.
    bool optimizeLLVMIR(const std::filesystem::path& inputLL,
                        const std::filesystem::path& outputLL,
                        int optLevel);

    /// Compile LLVM IR to native x86-64 assembly via clang (-S).
    bool compileToAssembly(const std::filesystem::path& llPath,
                           const std::filesystem::path& asmPath,
                           int optLevel = 0);

    /// Compile LLVM IR to a native executable via clang at the specified optimization level.
    bool compileToExecutable(const std::filesystem::path& llPath,
                             const std::filesystem::path& exePath,
                             int optLevel = 0);

    /// Compile LLVM IR to a native object file (.obj on Windows, .o on Linux/Darwin).
    bool compileToObject(const std::filesystem::path& llPath,
                         const std::filesystem::path& objPath,
                         int optLevel = 0);

    /// Compile LLVM IR to a dynamic/shared library (.dll on Windows, .so on Linux, .dylib on Darwin).
    bool compileToSharedLibrary(const std::filesystem::path& llPath,
                                const std::filesystem::path& dylibPath,
                                int optLevel = 0);

    /// Compile LLVM IR to a static library archive (.lib on Windows, .a on Linux/Darwin).
    bool compileToStaticLibrary(const std::filesystem::path& llPath,
                                const std::filesystem::path& libPath,
                                int optLevel = 0);

    void setEnableMemoryStats(bool enable) { enableMemoryStats_ = enable; }
    bool isMemoryStatsEnabled() const { return enableMemoryStats_; }

    void setFastMath(bool enable) { fastMath_ = enable; }
    bool isFastMath() const { return fastMath_; }

    void setCrtLinkage(CrtLinkage linkage) { crtLinkage_ = linkage; }
    CrtLinkage getCrtLinkage() const { return crtLinkage_; }
    bool isStaticLinkage() const { return crtLinkage_ == CrtLinkage::Static; }

    void setLinkLibraries(std::vector<std::string> libs) { linkLibs_ = std::move(libs); }
    const std::vector<std::string>& getLinkLibraries() const { return linkLibs_; }

    void setLinkDirectories(std::vector<std::string> dirs) { linkDirs_ = std::move(dirs); }
    const std::vector<std::string>& getLinkDirectories() const { return linkDirs_; }

    void setLinkerFlags(std::vector<std::string> flags) { linkerFlags_ = std::move(flags); }
    const std::vector<std::string>& getLinkerFlags() const { return linkerFlags_; }

    /// Formats native library directories, library names, and linker flags for the compiler/linker command.
    std::string formatNativeLinkFlags() const;

private:
    void emitHeader(std::ostream& os);
    void emitRuntimeHelpers(std::ostream& os);
    void emitFunction(const ir::IRFunction& fn, std::ostream& os);
    void emitInstruction(const ir::Instruction& inst, std::ostream& os);

    std::string llvmType(const sema::Type* ty) const;
    std::string sanitizeEnumName(const std::string& name) const;
    const sema::Type* getValueType(ir::ValueId id) const;
    std::string formatOperand(ir::ValueId id, const sema::Type* ty = nullptr) const;
    std::string zeroValue(const sema::Type* ty) const;

    const ir::IRModule& module_;
    DiagnosticEngine&   diag_;
    std::string         clangPath_;
    std::unordered_map<ir::ValueId, const sema::Type*> valueTypes_;
    std::unordered_map<ir::ValueId, int64_t> intConstants_;
    std::unordered_map<ir::ValueId, double>  floatConstants_;
    std::unordered_map<ir::ValueId, bool>    boolConstants_;
    std::unordered_set<ir::ValueId>          isPointerValue_;
    uint32_t boundsCheckId_ = 0;
    uint32_t gepStoreId_ = 0;
    uint32_t memOpId_ = 0;
    uint32_t callOpId_ = 0;
    bool enableMemoryStats_{false};
    bool fastMath_{false};
    CrtLinkage crtLinkage_{CrtLinkage::Dynamic};
    target::TargetTriple targetTriple_ = target::TargetTriple::getHost();
    std::string targetCPU_;
    std::vector<std::string> linkLibs_;
    std::vector<std::string> linkDirs_;
    std::vector<std::string> linkerFlags_;
    std::unordered_set<int32_t> loopUnrollsUsed_;
    std::map<LoopMetaKey, int32_t> loopMetadataMap_;
};

} // namespace tamarin::backend
