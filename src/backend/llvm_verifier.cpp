// ============================================================================
// llvm_verifier.cpp - LLVM IR module and function verification
// ============================================================================

#include "tamarin/backend/llvm_verifier.h"
#include "tamarin/config.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace tamarin::backend {

LLVMVerifier::LLVMVerifier() {
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

LLVMVerifier& LLVMVerifier::instance() {
    static LLVMVerifier inst;
    return inst;
}

void LLVMVerifier::setClangPath(std::string path) {
    if (!path.empty()) {
        clangPath_ = std::move(path);
    }
}

const std::string& LLVMVerifier::getClangPath() const {
    return clangPath_;
}

void LLVMVerifier::setTargetTriple(std::string triple) {
    targetTriple_ = std::move(triple);
}

const std::string& LLVMVerifier::getTargetTriple() const {
    return targetTriple_;
}

static std::string runCommand(const std::string& cmd, int& exitCode) {
    std::string result;
    FILE* pipe = _popen(cmd.c_str(), "r");
    if (!pipe) {
        exitCode = -1;
        return "failed to open process pipe";
    }

    char buffer[512];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }
    exitCode = _pclose(pipe);
    return result;
}

VerificationResult LLVMVerifier::verifyFile(const std::filesystem::path& path) {
    VerificationResult res;
    std::string triple = targetTriple_;
    if (triple.empty()) {
#if defined(_WIN32)
        triple = "x86_64-pc-windows-msvc";
#elif defined(__APPLE__)
        triple = "x86_64-apple-darwin";
#else
        triple = "x86_64-unknown-linux-gnu";
#endif
    }
#ifdef _WIN32
    std::string devNull = "NUL";
#else
    std::string devNull = "/dev/null";
#endif
    std::string cmd = "\"\"" + clangPath_ + "\" -x ir -c --target=" + triple +
                      " -Wno-override-module \"" + path.string() + "\" -o " + devNull + " 2>&1\"";

    int exitCode = 0;
    std::string output = runCommand(cmd, exitCode);

    if (exitCode != 0) {
        res.ok = false;
        res.errorMessage = output.empty() ? ("verification exited with code " + std::to_string(exitCode)) : output;
    } else {
        res.ok = true;
    }
    return res;
}

VerificationResult LLVMVerifier::verifyIR(const std::string& irCode) {
    static std::atomic<uint64_t> counter{0};
    int pid = 0;
#ifdef _WIN32
    pid = _getpid();
#else
    pid = getpid();
#endif
    std::string filename = "tamarin_verify_" + std::to_string(pid) + "_" +
                           std::to_string(++counter) + ".ll";
    auto tempPath = std::filesystem::temp_directory_path() / filename;
    {
        std::ofstream out(tempPath);
        if (!out) {
            return VerificationResult{false, "failed to create temporary file for verification"};
        }
        out << irCode;
    }

    auto res = verifyFile(tempPath);

    std::error_code ec;
    std::filesystem::remove(tempPath, ec);
    return res;
}

bool LLVMVerifier::verifyAndReport(const std::filesystem::path& path, DiagnosticEngine& diag) {
    auto res = verifyFile(path);
    if (!res.ok) {
        diag.report(Severity::Error, "LLVM module verification failed:\n" + res.errorMessage);
        return false;
    }
    return true;
}

} // namespace tamarin::backend
