// ============================================================================
// driver.h - Compiler orchestration pipeline (check, build, run)
// ============================================================================
#pragma once

#include "tamarin/source/source_manager.h"
#include "tamarin/diagnostics/diagnostic_engine.h"

#include <filesystem>
#include <string>

namespace tamarin::driver {

struct DriverOptions {
    enum class Command { Check, Build, Run, Help, Version };
    enum class OutputKind { Exe, Obj, StaticLib, SharedLib };
    enum class CrtLinkage { Dynamic, Static };

    Command command = Command::Build;
    OutputKind outputKind = OutputKind::Exe;
    CrtLinkage crtLinkage = CrtLinkage::Dynamic;
    std::string inputFile;
    std::string outputFile;
    std::string clangPath;
    std::string targetCPU;
    std::string targetTriple; // e.g. "x86_64-pc-windows-msvc", "x86_64-unknown-linux-gnu", "aarch64-apple-darwin"
    std::string headerPath;   // for --emit-header <path.h>
    int  optLevel = 0;
    bool fastMath = false;
    bool emitIR   = false;
    bool emitTIR  = false;
    bool emitLLVM = false;
    bool emitOptimizedLLVM = false;
    bool emitAsm  = false;
    bool memoryStats = false;
    bool emitMemoryReport = false;
    bool verbose  = false;
    std::vector<std::string> includeDirs;
    std::vector<std::string> linkLibs;
    std::vector<std::string> linkDirs;
    std::vector<std::string> linkerFlags;
};

class Driver {
public:
    explicit Driver(DriverOptions options);

    /// Execute the requested driver command. Returns process exit code (0 = success).
    int run();

    /// Semantic check only (no code generation).
    int check();

    /// Full compilation to native executable.
    int build();

    /// Compile and execute.
    int execute();

private:
    DriverOptions     options_;
    SourceManager     sm_;
    DiagnosticEngine  diag_;
};

} // namespace tamarin::driver

