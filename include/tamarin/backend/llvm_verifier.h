// ============================================================================
// llvm_verifier.h - LLVM IR module and function verification
// ============================================================================
#pragma once

#include "tamarin/diagnostics/diagnostic_engine.h"

#include <filesystem>
#include <string>

namespace tamarin::backend {

struct VerificationResult {
    bool ok = true;
    std::string errorMessage;
};

class LLVMVerifier {
public:
    static LLVMVerifier& instance();

    /// Configure explicit clang executable path.
    void setClangPath(std::string path);
    const std::string& getClangPath() const;

    /// Configure target triple for verification.
    void setTargetTriple(std::string triple);
    const std::string& getTargetTriple() const;

    /// Verify an LLVM IR module file.
    VerificationResult verifyFile(const std::filesystem::path& path);

    /// Verify an LLVM IR module string in memory.
    VerificationResult verifyIR(const std::string& irCode);

    /// Verify and report to a DiagnosticEngine. Returns true if verification succeeded.
    bool verifyAndReport(const std::filesystem::path& path, DiagnosticEngine& diag);

private:
    LLVMVerifier();
    std::string clangPath_;
    std::string targetTriple_;
};

} // namespace tamarin::backend
